#pragma once
#include <cstdint>
#include <array>
#include <string>

// Board layout — column-major, bottom to top, 7 bits per column (6 data + 1 sentinel):
//
//  Col:  0  1  2  3  4  5  6
//  Bit:  0  7 14 21 28 35 42   <- row 0 (bottom)
//        1  8 15 22 29 36 43
//        2  9 16 23 30 37 44
//        3 10 17 24 31 38 45
//        4 11 18 25 32 39 46
//        5 12 19 26 33 40 47   <- row 5 (top)
//        6 13 20 27 34 41 48   <- sentinel row (always 0)

static constexpr int COLS  = 7;
static constexpr int ROWS  = 6;
static constexpr int CELLS = COLS * ROWS;  // 42

// Bit index of the bottom cell of column c
static constexpr int col_base(int c) { return c * (ROWS + 1); }

// One bit per column at the bottom row
static constexpr uint64_t BOTTOM_MASK = 0x0000'0408'1020'4081ULL;

// All 42 valid board bits (excludes sentinel row)
static constexpr uint64_t BOARD_MASK = BOTTOM_MASK * ((1ULL << ROWS) - 1);

class Board {
public:
    Board();

    bool make_move(int col);   // returns false if column is full
    void undo_move(int col);

    bool is_valid(int col) const;
    bool is_full()         const { return moves_ == CELLS; }

    // True when the player who just moved has four in a row
    bool last_player_won() const;

    int moves_played()   const { return moves_; }
    int current_player() const { return moves_ & 1; }  // 0 or 1

    uint64_t key()         const { return key_; }
    uint64_t key_mirrored() const;

    static int mirror_col(int col) { return COLS - 1 - col; }

    std::string to_string() const;

    // Centre-out move ordering
    static constexpr std::array<int, COLS> MOVE_ORDER = {3, 2, 4, 1, 5, 0, 6};

    uint64_t pieces(int player) const { return pieces_[player]; }

private:
    uint64_t pieces_[2];       // bitboards for player 0 and player 1
    uint64_t mask_;            // all pieces combined
    int      height_[COLS];    // bit index of next empty cell per column
    uint64_t key_;
    int      moves_;

    static bool has_won(uint64_t pos);
};
