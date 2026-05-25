#include "solver.hpp"
#include <algorithm>
#include <climits>
#include <chrono>
#include <omp.h>

// Score convention (negamax, from current player's view):
//   win  in N half-moves from root:  MAX_SCORE - N/2
//   loss in N half-moves from root: -(MAX_SCORE - N/2)
//   draw: 0

Solver::Solver(Difficulty diff, size_t tt_mb)
    : diff_(diff), tt_(tt_mb) {}

// ---------------------------------------------------------------------------
// SearchResult helpers
// ---------------------------------------------------------------------------

Outcome SearchResult::outcome(int human_player) const {
    // score is from the perspective of the player who just called solve(),
    // which is the current player at the time of the call.
    // We normalise: positive = human wins.
    if (score == 0)  return Outcome::DRAW;
    if (score >  0)  return Outcome::WIN;
    return Outcome::LOSS;
    (void)human_player;
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
    if ((n & 0x3FF) == 0 && abort_.load(std::memory_order_relaxed))
        return 0;

    // Terminal: previous player won
    if (board.last_player_won())
        return -(MAX_SCORE - (board.moves_played() - 1) / 2);

    // Terminal: draw
    if (board.is_full())
        return 0;

    // Depth limit reached — use heuristic in limited modes
    if (depth == 0)
        return eval_.score(board);

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

    int tt_score = 0, tt_col = -1;
    if (tt_.probe(canon, depth, alpha, beta, tt_score, tt_col)) {
        if (flipped && tt_col >= 0) tt_col = Board::mirror_col(tt_col);
        return tt_score;
    }

    // Build move list, seeded with TT best move first
    std::array<int, COLS> moves;
    int n_moves = 0;
    if (tt_col >= 0 && board.is_valid(tt_col))
        moves[n_moves++] = tt_col;
    for (int c : Board::MOVE_ORDER)
        if (c != tt_col && board.is_valid(c))
            moves[n_moves++] = c;

    int  best_score = INT_MIN;
    int  best_col   = moves[0];
    Bound bound     = Bound::UPPER;

    for (int i = 0; i < n_moves; ++i) {
        int col = moves[i];
        board.make_move(col);
        int s = -negamax(board, -beta, -alpha, depth - 1);
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
            break;
        }
    }

    // Store in TT (translate col back to canonical orientation)
    int store_col = flipped ? Board::mirror_col(best_col) : best_col;
    tt_.store(canon, depth, best_score, bound, store_col);

    return best_score;
}

// ---------------------------------------------------------------------------
// Public solve() — root search with OpenMP parallelism
// ---------------------------------------------------------------------------

SearchResult Solver::solve(Board& board) {
    nodes_.store(0, std::memory_order_relaxed);

    SearchResult result;
    int max_depth = max_depth_for(board);

    // Collect valid moves in centre-out order
    std::array<int, COLS> moves;
    int n_moves = 0;
    for (int c : Board::MOVE_ORDER)
        if (board.is_valid(c))
            moves[n_moves++] = c;

    if (n_moves == 0) return result;
    if (n_moves == 1) {
        result.best_col      = moves[0];
        result.depth_reached = 0;
        result.is_exact      = false;
        return result;
    }

    // Iterative deepening for PERFECT/STRONG: builds TT for move ordering,
    // making the final full-depth search much faster.
    int start_depth = (diff_ == Difficulty::STRONG || diff_ == Difficulty::PERFECT) ? 1 : max_depth;

    auto t_start = std::chrono::steady_clock::now();
    constexpr int STRONG_BUDGET_MS = 5000;

    abort_.store(false, std::memory_order_relaxed);

    // STRONG mode: timer thread fires after the budget and sets the abort flag.
    // A generation counter ensures a timer from a previous call doesn't interfere.
    uint64_t my_gen = solve_gen_.fetch_add(1, std::memory_order_relaxed) + 1;
    if (diff_ == Difficulty::STRONG) {
        std::thread([this, my_gen, STRONG_BUDGET_MS]() {
            std::this_thread::sleep_for(std::chrono::milliseconds(STRONG_BUDGET_MS));
            if (solve_gen_.load(std::memory_order_relaxed) == my_gen)
                abort_.store(true, std::memory_order_relaxed);
        }).detach();
    }

    int  best_col   = moves[0];
    int  best_score = -MAX_SCORE - 1;

    for (int depth = start_depth; depth <= max_depth; ++depth) {
        int alpha = -MAX_SCORE - 1;
        int beta  =  MAX_SCORE + 1;

        // Search first move serially to establish alpha
        board.make_move(moves[0]);
        int s0 = -negamax(board, -beta, -alpha, depth - 1);
        board.undo_move(moves[0]);
        if (s0 > best_score) { best_score = s0; best_col = moves[0]; }
        if (s0 > alpha) alpha = s0;

        // Search remaining moves in parallel (Young Brothers Wait)
        std::atomic<int>  shared_alpha{alpha};
        std::atomic<int>  shared_best_score{best_score};
        std::atomic<int>  shared_best_col{best_col};
        std::atomic<bool> cutoff{false};

        #pragma omp parallel for schedule(dynamic) num_threads(omp_get_max_threads())
        for (int i = 1; i < n_moves; ++i) {
            if (cutoff.load(std::memory_order_relaxed)) continue;

            int local_alpha = shared_alpha.load(std::memory_order_relaxed);
            if (local_alpha >= beta) { cutoff.store(true); continue; }

            // Skip remaining moves if we already have a forced win.
            // shared_best_score is initialised with s0 (the serial move's score),
            // so this fires immediately when the first move is a winner.
            if (shared_best_score.load(std::memory_order_relaxed) > 0)
                { cutoff.store(true, std::memory_order_relaxed); continue; }

            Board local = board;  // thread-local copy (two uint64_t + small state)
            local.make_move(moves[i]);
            int s = -negamax(local, -beta, -local_alpha, depth - 1);

            // Update shared best atomically
            int cur_best = shared_best_score.load(std::memory_order_relaxed);
            while (s > cur_best &&
                   !shared_best_score.compare_exchange_weak(
                       cur_best, s, std::memory_order_relaxed));
            if (s > cur_best) {
                shared_best_col.store(moves[i], std::memory_order_relaxed);
            }

            int cur_alpha = shared_alpha.load(std::memory_order_relaxed);
            while (s > cur_alpha &&
                   !shared_alpha.compare_exchange_weak(
                       cur_alpha, s, std::memory_order_relaxed));

            if (shared_alpha.load() >= beta)
                cutoff.store(true, std::memory_order_relaxed);
        }

        // Discard this depth's results if we were aborted mid-search
        if (!abort_.load(std::memory_order_relaxed)) {
            best_score = shared_best_score.load();
            best_col   = shared_best_col.load();
            result.depth_reached = depth;
        } else {
            break;
        }
    }

    // Invalidate any pending timer so it doesn't affect future solve() calls
    solve_gen_.fetch_add(1, std::memory_order_relaxed);
    abort_.store(false, std::memory_order_relaxed);

    result.best_col       = best_col;
    result.score          = best_score;
    result.nodes_evaluated = nodes_.load(std::memory_order_relaxed);
    result.is_exact       = (diff_ == Difficulty::PERFECT) ||
                             (result.depth_reached >= CELLS - board.moves_played());

    return result;
}
