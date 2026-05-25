#include "board.hpp"
#include <sstream>
#include <stdexcept>

// ---------------------------------------------------------------------------
// Zobrist table — deterministic, initialised before main()
// ---------------------------------------------------------------------------

static uint64_t ZOBRIST[2][COLS][ROWS];

static uint64_t splitmix64(uint64_t& state) {
    state += 0x9e3779b97f4a7c15ULL;
    uint64_t z = state;
    z = (z ^ (z >> 30)) * 0xbf58476d1ce4e5b9ULL;
    z = (z ^ (z >> 27)) * 0x94d049bb133111ebULL;
    return z ^ (z >> 31);
}

struct ZobristInit {
    ZobristInit() {
        uint64_t state = 0xdeadbeefcafe1234ULL;
        for (int p = 0; p < 2; ++p)
            for (int c = 0; c < COLS; ++c)
                for (int r = 0; r < ROWS; ++r)
                    ZOBRIST[p][c][r] = splitmix64(state);
    }
} _zinit;

// ---------------------------------------------------------------------------
// Board
// ---------------------------------------------------------------------------

Board::Board()
    : pieces_{0, 0}, mask_(0), key_(0), moves_(0)
{
    for (int c = 0; c < COLS; ++c)
        height_[c] = col_base(c);  // bit index of bottom cell in column c
}

bool Board::is_valid(int col) const {
    if (col < 0 || col >= COLS) return false;
    return height_[col] < col_base(col) + ROWS;
}

bool Board::make_move(int col) {
    if (!is_valid(col)) return false;
    int      p   = current_player();
    uint64_t bit = 1ULL << height_[col];
    pieces_[p] |= bit;
    mask_       |= bit;
    int row = height_[col] - col_base(col);
    key_ ^= ZOBRIST[p][col][row];
    ++height_[col];
    ++moves_;
    return true;
}

void Board::undo_move(int col) {
    --moves_;
    --height_[col];
    int      p   = current_player();  // after decrement == the player who just moved
    uint64_t bit = 1ULL << height_[col];
    pieces_[p] &= ~bit;
    mask_       &= ~bit;
    int row = height_[col] - col_base(col);
    key_ ^= ZOBRIST[p][col][row];
}

bool Board::has_won(uint64_t pos) {
    uint64_t m;
    m = pos & (pos >> 7); if (m & (m >> 14)) return true;  // horizontal
    m = pos & (pos >> 1); if (m & (m >>  2)) return true;  // vertical
    m = pos & (pos >> 8); if (m & (m >> 16)) return true;  // diagonal /
    m = pos & (pos >> 6); if (m & (m >> 12)) return true;  // diagonal backslash
    return false;
}

bool Board::last_player_won() const {
    return has_won(pieces_[1 - current_player()]);
}

uint64_t Board::key_mirrored() const {
    uint64_t k = 0;
    for (int p = 0; p < 2; ++p) {
        uint64_t bb = pieces_[p];
        while (bb) {
            int idx = __builtin_ctzll(bb);
            bb &= bb - 1;
            int col = idx / (ROWS + 1);
            int row = idx % (ROWS + 1);
            if (row < ROWS)  // skip sentinel bits (shouldn't exist, but guard anyway)
                k ^= ZOBRIST[p][COLS - 1 - col][row];
        }
    }
    return k;
}

std::string Board::to_string() const {
    std::ostringstream oss;
    for (int r = ROWS - 1; r >= 0; --r) {
        for (int c = 0; c < COLS; ++c) {
            uint64_t bit = 1ULL << (col_base(c) + r);
            if      (pieces_[0] & bit) oss << "|X";
            else if (pieces_[1] & bit) oss << "|O";
            else                       oss << "| ";
        }
        oss << "|\n";
    }
    oss << "───────────────\n";
    oss << " 1 2 3 4 5 6 7\n";
    return oss.str();
}
