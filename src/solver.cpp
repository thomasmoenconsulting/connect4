#include "solver.hpp"
#include <algorithm>
#include <climits>
#include <chrono>
#include <mutex>
#include <random>
#include <omp.h>
#include <array>

// Score convention (negamax, from current player's view):
//   win  in N half-moves from root:  MAX_SCORE - N/2
//   loss in N half-moves from root: -(MAX_SCORE - N/2)
//   draw: 0

// Killer-move tables: two moves per ply that previously caused beta cutoffs.
// thread_local so each OMP worker keeps its own table — killers are heuristic,
// not correctness, so cross-thread sharing isn't needed. CELLS+1 is enough
// for any reachable remaining-depth value.
static thread_local int killer_moves_[CELLS + 1][2];

static void reset_killers() {
    for (int d = 0; d <= CELLS; ++d) {
        killer_moves_[d][0] = -1;
        killer_moves_[d][1] = -1;
    }
}

static void record_killer(int depth, int col) {
    if (killer_moves_[depth][0] == col) return;
    killer_moves_[depth][1] = killer_moves_[depth][0];
    killer_moves_[depth][0] = col;
}

Solver::Solver(Difficulty diff, size_t tt_mb)
    : diff_(diff), tt_(tt_mb) {}

bool Solver::load_book(const std::string& path) {
    return book_.load(path);
}

// Probe the opening book for a position. Returns MISS if the book isn't
// loaded or the position is deeper than the book covers.
static int probe_book(const OpeningBook& book, const Board& board) {
    if (!book.loaded()) return OpeningBook::MISS;
    if (board.moves_played() > book.max_plies()) return OpeningBook::MISS;
    uint64_t k = std::min(board.key(), board.key_mirrored());
    return book.probe(k);
}

// ---------------------------------------------------------------------------
// SearchResult helpers
// ---------------------------------------------------------------------------

Outcome SearchResult::outcome(int human_player) const {
    // score is from player_to_move's perspective; flip if the human was NOT to move
    int s = (player_to_move == human_player) ? score : -score;
    if (s == 0) return Outcome::DRAW;
    if (s >  0) return Outcome::WIN;
    return Outcome::LOSS;
}

int SearchResult::moves_to_end() const {
    if (!is_exact || score == 0) return 0;
    int abs_score = score > 0 ? score : -score;
    return 2 * (Solver::MAX_SCORE - abs_score) + 1;
}

int SearchResult::confidence_pct(int moves_played) const {
    if (is_exact) return 100;
    int remaining = CELLS - moves_played;
    if (remaining <= 0) return 100;
    return std::min(100, depth_reached * 100 / remaining);
}

// ---------------------------------------------------------------------------
// Depth limits per difficulty
// ---------------------------------------------------------------------------

int Solver::max_depth_for(const Board& board) const {
    int remaining = CELLS - board.moves_played();
    switch (diff_) {
        case Difficulty::EASY:    return std::min(3, remaining);
        case Difficulty::MEDIUM:  return std::min(7, remaining);
        case Difficulty::STRONG:  return remaining;  // iterative deepening handles time
        case Difficulty::PERFECT: return remaining;
    }
    return remaining;
}

// ---------------------------------------------------------------------------
// Negamax with alpha-beta and transposition table
// ---------------------------------------------------------------------------

int Solver::negamax(Board& board, int alpha, int beta, int depth) {
    // Periodically check for abort signal (set by timer thread for STRONG mode)
    long n = nodes_.fetch_add(1, std::memory_order_relaxed);
    if ((n & 0xFF) == 0 && abort_.load(std::memory_order_relaxed))
        return 0;

    // Terminal: previous player won
    if (board.last_player_won())
        return -(MAX_SCORE - (board.moves_played() - 1) / 2);

    // Terminal: draw
    if (board.is_full())
        return 0;

    // Depth limit reached — use heuristic in limited modes.
    // Clamp to a position-specific ceiling that is provably below any exact
    // win/loss score for this node.  An exact win at this node in d more plies
    // scores MAX_SCORE - (moves+d-1)/2 ≥ (CELLS-moves)/2.  Clamping heuristics
    // to (CELLS-moves)/2 - 1 creates a strict gap, letting the ID loop detect
    // proven forced results and stop early.
    if (depth == 0) {
        int s     = eval_.score(board);
        int bound = std::max(0, (CELLS - board.moves_played()) / 2 - 1);
        if (s >  bound) s =  bound;
        if (s < -bound) s = -bound;
        return s;
    }

    // Upper bound on score given remaining moves
    int upper = (CELLS - board.moves_played()) / 2;
    if (beta > upper) {
        beta = upper;
        if (alpha >= beta) return beta;
    }

    // Transposition table probe
    bool    flipped   = false;
    uint64_t key      = board.key();
    uint64_t key_mir  = board.key_mirrored();
    uint64_t canon    = std::min(key, key_mir);
    flipped = (canon == key_mir && key != key_mir);

    int  tt_score = 0, tt_col = -1;
    bool tt_hit   = tt_.probe(canon, depth, alpha, beta, tt_score, tt_col);
    // probe() may set tt_col on key match even when it returns false
    // (depth too low / bound unusable). Mirror unconditionally so the
    // move-ordering hint below is in our orientation.
    if (flipped && tt_col >= 0) tt_col = Board::mirror_col(tt_col);
    if (tt_hit) return tt_score;

    // Build move list. Priority: TT move, then killers, then centre-out.
    std::array<int, COLS> moves;
    int n_moves = 0;
    auto add_if_new = [&](int c) {
        if (c < 0 || !board.is_valid(c)) return;
        for (int i = 0; i < n_moves; ++i)
            if (moves[i] == c) return;
        moves[n_moves++] = c;
    };
    add_if_new(tt_col);
    add_if_new(killer_moves_[depth][0]);
    add_if_new(killer_moves_[depth][1]);
    for (int c : Board::MOVE_ORDER)
        add_if_new(c);

    int  best_score = INT_MIN;
    int  best_col   = moves[0];
    Bound bound     = Bound::UPPER;

    for (int i = 0; i < n_moves; ++i) {
        int col = moves[i];
        board.make_move(col);

        // PVS: first move full window; later moves probed with a null window,
        // re-searched only if they raise alpha. Null-window searches return
        // a tight bound very quickly when most moves are worse than the PV move.
        int s;
        if (i == 0) {
            s = -negamax(board, -beta, -alpha, depth - 1);
        } else {
            s = -negamax(board, -alpha - 1, -alpha, depth - 1);
            if (s > alpha && s < beta)
                s = -negamax(board, -beta, -alpha, depth - 1);
        }
        board.undo_move(col);

        if (s > best_score) {
            best_score = s;
            best_col   = col;
        }
        if (s > alpha) {
            alpha = s;
            bound = Bound::EXACT;
        }
        if (alpha >= beta) {
            bound = Bound::LOWER;
            record_killer(depth, col);
            break;
        }
    }

    // Don't pollute the TT with results from aborted subtrees — child
    // negamax calls returned 0 on abort, so best_score is meaningless.
    if (abort_.load(std::memory_order_relaxed)) return 0;

    // Store in TT (translate col back to canonical orientation)
    int store_col = flipped ? Board::mirror_col(best_col) : best_col;
    tt_.store(canon, depth, best_score, bound, store_col);

    return best_score;
}

// ---------------------------------------------------------------------------
// Public solve() — root search with OpenMP parallelism
// ---------------------------------------------------------------------------

SearchResult Solver::solve(Board& board, ProgressCb on_depth) {
    nodes_.store(0, std::memory_order_relaxed);
    // Clear abort flag up-front. A timer from a previous solve() could race
    // past its generation check and set abort_ between calls; clearing here
    // (rather than at end-of-solve) guarantees we start with a clean slate.
    abort_.store(false, std::memory_order_relaxed);
    // Killers from a previous solve() are stale. Reset on the calling thread.
    // OMP worker threads may carry over stale entries from a prior parallel
    // region, but add_if_new() validates each candidate with is_valid() and
    // killers are pure heuristic — wrong hints only degrade ordering, never
    // correctness, and self-correct quickly as the new search records cutoffs.
    reset_killers();

    SearchResult result;
    int max_depth = max_depth_for(board);

    // Collect valid moves in centre-out order
    std::array<int, COLS> moves;
    int n_moves = 0;
    for (int c : Board::MOVE_ORDER)
        if (board.is_valid(c))
            moves[n_moves++] = c;

    // EASY: shuffle root moves so equally-scored options vary across games.
    // The strict `s > best_score` tiebreak always picks the first-encountered
    // move, which combined with fixed centre-out ordering makes EASY play the
    // same opening every time.
    if (diff_ == Difficulty::EASY) {
        static thread_local std::mt19937 rng{std::random_device{}()};
        std::shuffle(moves.begin(), moves.begin() + n_moves, rng);
    }

    if (n_moves == 0) return result;
    if (n_moves == 1) {
        result.best_col      = moves[0];
        result.depth_reached = 0;
        result.is_exact      = false;
        return result;
    }

    // Opening-book early return. We require the children to also be queryable
    // (ply < max_plies) so we can pick best_col without searching. For the
    // boundary case (ply == max_plies) we fall through to normal search.
    if (book_.loaded() && board.moves_played() < book_.max_plies()) {
        int my_score = probe_book(book_, board);
        if (my_score != OpeningBook::MISS) {
            int best_c = -1;
            int best_s = -MAX_SCORE - 1;
            for (int c : Board::MOVE_ORDER) {
                if (!board.is_valid(c)) continue;
                board.make_move(c);
                if (board.last_player_won()) {
                    board.undo_move(c);
                    // Immediate win is always the best possible — short-circuit.
                    best_c = c;
                    break;
                }
                int child = probe_book(book_, board);
                board.undo_move(c);
                if (child == OpeningBook::MISS) continue;
                int s = -child;  // child score is from the opponent's view
                if (s > best_s) { best_s = s; best_c = c; }
            }
            if (best_c >= 0) {
                result.best_col        = best_c;
                result.score           = my_score;
                result.depth_reached   = CELLS - board.moves_played();
                result.nodes_evaluated = 0;
                result.is_exact        = true;
                result.player_to_move  = board.current_player();
                if (on_depth) on_depth(result.depth_reached, result.depth_reached, 0);
                return result;
            }
        }
    }

    // Iterative deepening: warms the TT for move ordering at each subsequent
    // depth. Skip for EASY so its root shuffle isn't overruled by TT hits
    // from the depth-1 pass (and depth 3 is cheap enough that ID overhead
    // would dwarf the search itself).
    int start_depth = (diff_ == Difficulty::EASY) ? max_depth : 1;

    // Timer thread fires after the budget and sets the abort flag.
    // STRONG uses time_budget_ms_; PERFECT uses perfect_max_ms_ as a safety
    // ceiling so an early-game search can't run for hours. EASY/MEDIUM have
    // fixed shallow depths and don't need a timer.
    // A generation counter ensures a timer from a previous call doesn't interfere.
    uint64_t my_gen = solve_gen_.fetch_add(1, std::memory_order_relaxed) + 1;
    int budget_ms = 0;
    if (diff_ == Difficulty::STRONG)  budget_ms = time_budget_ms_;
    if (diff_ == Difficulty::PERFECT) budget_ms = perfect_max_ms_;
    if (budget_ms > 0) {
        std::thread([this, my_gen, budget_ms]() {
            std::this_thread::sleep_for(std::chrono::milliseconds(budget_ms));
            if (solve_gen_.load(std::memory_order_relaxed) == my_gen)
                abort_.store(true, std::memory_order_relaxed);
        }).detach();
    }

    // best_col/best_score track the result of the last *completed* depth.
    // Each completed depth REPLACES (never max-merges) the previous depth's result.
    int  best_col   = moves[0];
    int  best_score = -MAX_SCORE - 1;
    int  prev_score = 0;
    bool aborted    = false;

    for (int depth = start_depth; depth <= max_depth && !aborted; ++depth) {
        // Aspiration windows: start with a narrow band around the previous
        // depth's score; widen on fail-low / fail-high. Skip aspiration at
        // shallow depths where scores are noisy. Cap retries so we always
        // fall back to a full-window search before giving up.
        int  aspiration = 3;
        int  retries    = 0;
        int  d_best_score = -MAX_SCORE - 1;
        int  d_best_col   = moves[0];
        bool depth_done = false;

        while (!depth_done && !aborted) {
            int alpha_win, beta_win;
            if (depth < 3 || retries >= 2) {
                alpha_win = -MAX_SCORE - 1;
                beta_win  =  MAX_SCORE + 1;
            } else {
                alpha_win = std::max(prev_score - aspiration, -MAX_SCORE - 1);
                beta_win  = std::min(prev_score + aspiration,  MAX_SCORE + 1);
                if (alpha_win >= beta_win) {  // collapsed — fall back to full
                    alpha_win = -MAX_SCORE - 1;
                    beta_win  =  MAX_SCORE + 1;
                }
            }

            int alpha = alpha_win;
            int beta  = beta_win;

            d_best_score = -MAX_SCORE - 1;
            d_best_col   = moves[0];

            // Search first move serially to establish alpha (Young Brothers Wait)
            board.make_move(moves[0]);
            int s0 = -negamax(board, -beta, -alpha, depth - 1);
            board.undo_move(moves[0]);

            if (abort_.load(std::memory_order_relaxed)) { aborted = true; break; }

            d_best_score = s0;
            d_best_col   = moves[0];
            if (s0 > alpha) alpha = s0;

            if (alpha < beta) {
            // Search remaining moves in parallel (Young Brothers Wait).
            // Protect (score, col) updates with a mutex so the two writes are atomic.
            std::mutex        best_mtx;
            std::atomic<int>  shared_alpha{alpha};
            std::atomic<bool> cutoff{false};

            #pragma omp parallel for schedule(dynamic)
            for (int i = 1; i < n_moves; ++i) {
                if (cutoff.load(std::memory_order_relaxed)) continue;
                if (abort_.load(std::memory_order_relaxed)) continue;

                int local_alpha = shared_alpha.load(std::memory_order_relaxed);
                if (local_alpha >= beta) { cutoff.store(true, std::memory_order_relaxed); continue; }

                Board local = board;
                local.make_move(moves[i]);
                int s = -negamax(local, -beta, -local_alpha, depth - 1);

                {
                    std::lock_guard<std::mutex> lk(best_mtx);
                    if (s > d_best_score) {
                        d_best_score = s;
                        d_best_col   = moves[i];
                    }
                }

                int cur_alpha = shared_alpha.load(std::memory_order_relaxed);
                while (s > cur_alpha &&
                       !shared_alpha.compare_exchange_weak(
                           cur_alpha, s, std::memory_order_relaxed));

                if (shared_alpha.load(std::memory_order_relaxed) >= beta)
                    cutoff.store(true, std::memory_order_relaxed);
            }
            } // end OMP section

            if (abort_.load(std::memory_order_relaxed)) { aborted = true; break; }

            // Aspiration check (only meaningful when window was narrow).
            if (d_best_score <= alpha_win && alpha_win > -MAX_SCORE - 1) {
                aspiration *= 4;
                ++retries;
                continue;
            }
            if (d_best_score >= beta_win && beta_win < MAX_SCORE + 1) {
                aspiration *= 4;
                ++retries;
                continue;
            }
            depth_done = true;
        }

        if (aborted) break;

        // Commit this depth's result
        best_score = d_best_score;
        best_col   = d_best_col;
        prev_score = d_best_score;
        result.depth_reached = depth;
        if (on_depth)
            on_depth(depth, max_depth, nodes_.load(std::memory_order_relaxed));

        // If best_score exceeds the heuristic ceiling for depth-D leaves,
        // it came from an exact terminal node — a forced win/loss is proven.
        // No deeper search can change the result; stop iterating.
        // Heuristics at depth-D leaves are clamped to (CELLS-M-D)/2-1 (strict
        // lower bound); exact scores are ≥ (CELLS-M-D)/2.
        int remaining = CELLS - board.moves_played();
        int exact_threshold = (remaining - depth) / 2;
        if (std::abs(best_score) >= exact_threshold) {
            result.is_exact = true;
            break;
        }
    }

    // Bump generation so any still-sleeping timer thread won't set abort_
    // for this solve. The next solve() resets abort_ at entry, which also
    // covers the race where a timer set abort_ between this bump and entry.
    solve_gen_.fetch_add(1, std::memory_order_relaxed);

    result.player_to_move  = board.current_player();
    result.best_col        = best_col;
    result.score           = best_score;
    result.nodes_evaluated = nodes_.load(std::memory_order_relaxed);
    // is_exact is set either by the forced-result early-exit above or by
    // reaching full depth; don't overwrite it if already set.
    if (!result.is_exact)
        result.is_exact = result.depth_reached >= CELLS - board.moves_played();

    return result;
}

// ---------------------------------------------------------------------------
// score_moves — per-column evaluation using the warm TT
// ---------------------------------------------------------------------------

std::array<int, COLS> Solver::score_moves(Board& board, int depth) {
    std::array<int, COLS> scores;
    scores.fill(INT_MIN);
    if (depth < 1) return scores;
    // Clear any stale abort flag before re-entering negamax.
    abort_.store(false, std::memory_order_relaxed);
    for (int c = 0; c < COLS; ++c) {
        if (!board.is_valid(c)) continue;
        board.make_move(c);
        // Immediate win and book lookup both yield exact scores more cheaply
        // than a shallow negamax probe, and look right in the UI.
        if (board.last_player_won()) {
            scores[c] = MAX_SCORE;  // winning move from the pre-move mover's view
        } else {
            int b = probe_book(book_, board);
            if (b != OpeningBook::MISS) {
                scores[c] = -b;  // flip from opponent's view back to current mover's view
            } else {
                scores[c] = -negamax(board, -MAX_SCORE - 1, MAX_SCORE + 1, depth - 1);
            }
        }
        board.undo_move(c);
    }
    return scores;
}
