#pragma once
#include "board.hpp"
#include <array>
#include <cstdint>

// Interface for leaf-node evaluation in depth-limited modes.
// PERFECT mode never calls this — it always searches to terminal nodes.
// Replace HeuristicEval with a NeuralEval (CUDA) later without touching solver.
class Evaluator {
public:
    virtual ~Evaluator() = default;
    // Score from the perspective of board.current_player().
    // Positive = good for current player.
    virtual int score(const Board& board) const = 0;
};

// Heuristic eval:
//   - Each open 4-window contributes by piece count: 1, 2, 4 for 1, 2, 3 pieces.
//   - "Odd/even threat" bonus: a 3-piece window's empty cell on the row parity
//     that the owning player would occupy in a strict-alternation column adds
//     extra weight. Player 0 (first to move overall) wants empty rows 0/2/4;
//     player 1 wants rows 1/3/5. The rule is a simplification of Connect-4
//     zugzwang theory — strictly correct only deep in the endgame, but a
//     useful steering signal at intermediate depths.
//   - All windows are precomputed once at construction so the per-eval inner
//     loop is just popcount + a few ops, no nested geometry rebuild.
class HeuristicEval : public Evaluator {
public:
    HeuristicEval() {
        n_windows_ = 0;
        // Horizontal
        for (int c = 0; c <= COLS - 4; ++c)
            for (int r = 0; r < ROWS; ++r)
                add_window(c, r, 1, 0);
        // Vertical
        for (int c = 0; c < COLS; ++c)
            for (int r = 0; r <= ROWS - 4; ++r)
                add_window(c, r, 0, 1);
        // Diagonal /
        for (int c = 0; c <= COLS - 4; ++c)
            for (int r = 0; r <= ROWS - 4; ++r)
                add_window(c, r, 1, 1);
        // Diagonal \  (anchor at top-left of the diagonal)
        for (int c = 0; c <= COLS - 4; ++c)
            for (int r = 3; r < ROWS; ++r)
                add_window(c, r, 1, -1);
    }

    int score(const Board& board) const override {
        int me  = board.current_player();
        int opp = 1 - me;
        uint64_t mine   = board.pieces(me);
        uint64_t theirs = board.pieces(opp);

        int s = 0;
        for (int i = 0; i < n_windows_; ++i) {
            uint64_t w  = windows_[i];
            int my_c    = __builtin_popcountll(mine   & w);
            int opp_c   = __builtin_popcountll(theirs & w);
            if (my_c && opp_c) continue;            // blocked
            if (my_c)  s += 1 << (my_c  - 1);
            if (opp_c) s -= 1 << (opp_c - 1);

            // Parity bonus for 3-piece threats. The unique empty cell sits
            // at (w \ pieces); its row parity decides which player it
            // structurally favours in deep-endgame zugzwang.
            if (my_c == 3) {
                uint64_t empty_bit = w & ~mine;     // theirs is 0 here
                int row = __builtin_ctzll(empty_bit) % (ROWS + 1);
                if ((row & 1) == me) s += 4;
            } else if (opp_c == 3) {
                uint64_t empty_bit = w & ~theirs;
                int row = __builtin_ctzll(empty_bit) % (ROWS + 1);
                if ((row & 1) == opp) s -= 4;
            }
        }
        return s;
    }

private:
    // 4 horizontal/row + 7 vertical/col + 4 each diagonal direction:
    // 4*6 + 7*3 + 4*3 + 4*3 = 24 + 21 + 12 + 12 = 69 windows.
    static constexpr int MAX_WINDOWS = 80;
    std::array<uint64_t, MAX_WINDOWS> windows_{};
    int n_windows_ = 0;

    void add_window(int c0, int r0, int dc, int dr) {
        uint64_t w = 0;
        for (int i = 0; i < 4; ++i)
            w |= 1ULL << (col_base(c0 + i * dc) + (r0 + i * dr));
        windows_[n_windows_++] = w;
    }
};
