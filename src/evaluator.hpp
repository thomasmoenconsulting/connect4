#pragma once
#include "board.hpp"

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

// Count open winning lines weighted by how many of our pieces are already in them.
// Lines with opponent pieces score negatively.
class HeuristicEval : public Evaluator {
public:
    int score(const Board& board) const override {
        int me  = board.current_player();
        int opp = 1 - me;
        uint64_t mine = board.pieces(me);
        uint64_t theirs = board.pieces(opp);
        uint64_t empty = BOARD_MASK & ~(mine | theirs);

        int s = 0;

        // All possible horizontal, vertical, diagonal windows of 4
        // For each window: score based on pieces already in it
        auto eval_window = [&](uint64_t window) {
            int my_count  = __builtin_popcountll(mine   & window);
            int opp_count = __builtin_popcountll(theirs & window);
            if (my_count > 0 && opp_count > 0) return;  // blocked window
            if (my_count > 0)  s += (1 << (my_count  - 1));  // 1, 2, 4 for 1,2,3 in row
            if (opp_count > 0) s -= (1 << (opp_count - 1));
            (void)empty;
        };

        // Horizontal
        for (int c = 0; c <= COLS - 4; ++c)
            for (int r = 0; r < ROWS; ++r) {
                uint64_t w = 0;
                for (int i = 0; i < 4; ++i)
                    w |= 1ULL << (col_base(c + i) + r);
                eval_window(w);
            }

        // Vertical
        for (int c = 0; c < COLS; ++c)
            for (int r = 0; r <= ROWS - 4; ++r) {
                uint64_t w = 0;
                for (int i = 0; i < 4; ++i)
                    w |= 1ULL << (col_base(c) + r + i);
                eval_window(w);
            }

        // Diagonal /
        for (int c = 0; c <= COLS - 4; ++c)
            for (int r = 0; r <= ROWS - 4; ++r) {
                uint64_t w = 0;
                for (int i = 0; i < 4; ++i)
                    w |= 1ULL << (col_base(c + i) + r + i);
                eval_window(w);
            }

        // Diagonal backslash
        for (int c = 0; c <= COLS - 4; ++c)
            for (int r = 3; r < ROWS; ++r) {
                uint64_t w = 0;
                for (int i = 0; i < 4; ++i)
                    w |= 1ULL << (col_base(c + i) + r - i);
                eval_window(w);
            }

        return s;
    }
};
