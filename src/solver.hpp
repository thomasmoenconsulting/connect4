#pragma once
#include "board.hpp"
#include "transposition.hpp"
#include "evaluator.hpp"
#include "opening_book.hpp"
#include <cstdint>
#include <atomic>
#include <chrono>
#include <thread>
#include <functional>

enum class Difficulty { EASY, MEDIUM, STRONG, PERFECT };

enum class Outcome { WIN, DRAW, LOSS, UNKNOWN };

struct SearchResult {
    int     best_col        = -1;
    int     score           = 0;     // negamax score from player_to_move's view
    int     depth_reached   = 0;
    long    nodes_evaluated = 0;
    bool    is_exact        = false; // provably optimal (full search completed)
    int     player_to_move  = 0;     // 0 or 1 — used to flip score in outcome()

    // From the specified player's perspective
    Outcome outcome(int human_player) const;
    int     moves_to_end()    const;  // valid only when is_exact
    int     confidence_pct(int moves_played) const;
};

class Solver {
public:
    explicit Solver(Difficulty diff = Difficulty::PERFECT,
                    size_t tt_mb   = 64);

    // Returns true if the book file loaded successfully. Silent no-op on miss.
    bool load_book(const std::string& path);
    const OpeningBook& book() const { return book_; }

    // on_depth(depth_completed, max_depth, nodes_so_far) — called after each depth
    using ProgressCb = std::function<void(int, int, long)>;
    SearchResult solve(Board& board, ProgressCb on_depth = nullptr);

    void set_difficulty(Difficulty d)   { diff_ = d; }
    void set_time_budget_ms(int ms)     { time_budget_ms_ = ms; }
    int  time_budget_ms()         const { return time_budget_ms_; }
    void set_perfect_max_ms(int ms)     { perfect_max_ms_ = ms; }
    int  perfect_max_ms()         const { return perfect_max_ms_; }
    void clear_tt()                     { tt_.clear(); }

    // Score every valid column at the given depth using the warm TT.
    // Returns INT_MIN for full/invalid columns. Scores are from current player's view.
    // Cheap when called right after solve() because the TT is already populated.
    std::array<int, COLS> score_moves(Board& board, int depth);

    // Score range: wins scored as MAX_SCORE - half_moves_to_win
    static constexpr int MAX_SCORE = 22;  // (CELLS + 1) / 2

private:
    Difficulty         diff_;
    int                time_budget_ms_{5000};
    int                perfect_max_ms_{300000};  // 5-min safety cap for PERFECT
    TranspositionTable tt_;
    HeuristicEval      eval_;
    OpeningBook        book_;
    std::atomic<long>     nodes_{0};
    std::atomic<bool>     abort_{false};      // set to interrupt a running negamax
    std::atomic<uint64_t> solve_gen_{0};      // incremented each solve() call

    int negamax(Board& board, int alpha, int beta, int depth);
    int max_depth_for(const Board& board) const;
};
