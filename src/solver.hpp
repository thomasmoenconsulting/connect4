#pragma once
#include "board.hpp"
#include "transposition.hpp"
#include "evaluator.hpp"
#include <cstdint>
#include <atomic>
#include <chrono>
#include <thread>

enum class Difficulty { EASY, MEDIUM, STRONG, PERFECT };

enum class Outcome { WIN, DRAW, LOSS, UNKNOWN };

struct SearchResult {
    int     best_col       = -1;
    int     score          = 0;     // negamax score (current player's view)
    int     depth_reached  = 0;
    long    nodes_evaluated = 0;
    bool    is_exact       = false; // provably optimal

    // From the human player's perspective (human = player 0)
    Outcome outcome(int human_player) const;
    int     moves_to_end()    const;  // valid only when is_exact
    int     confidence_pct(int moves_played) const;
};

class Solver {
public:
    explicit Solver(Difficulty diff = Difficulty::PERFECT,
                    size_t tt_mb   = 64);

    SearchResult solve(Board& board);

    void set_difficulty(Difficulty d) { diff_ = d; }
    void clear_tt()                   { tt_.clear(); }

    // Score range: wins scored as MAX_SCORE - half_moves_to_win
    static constexpr int MAX_SCORE = 22;  // (CELLS + 1) / 2

private:
    Difficulty         diff_;
    TranspositionTable tt_;
    HeuristicEval      eval_;
    std::atomic<long>  nodes_{0};
    std::atomic<bool>     abort_{false};  // set to interrupt a running negamax
    std::atomic<uint64_t> solve_gen_{0}; // incremented each solve() call

    int negamax(Board& board, int alpha, int beta, int depth);

    int max_depth_for(const Board& board) const;
};
