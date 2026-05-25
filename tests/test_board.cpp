#include "../src/board.hpp"
#include <cassert>
#include <iostream>

static void test_empty_board() {
    Board b;
    assert(b.moves_played() == 0);
    assert(b.current_player() == 0);
    assert(!b.is_full());
    assert(!b.last_player_won());
    for (int c = 0; c < COLS; ++c)
        assert(b.is_valid(c));
}

static void test_vertical_win() {
    Board b;
    b.make_move(0); b.make_move(1);
    b.make_move(0); b.make_move(1);
    b.make_move(0); b.make_move(1);
    assert(!b.last_player_won());
    b.make_move(0);  // p0 completes 4 in col 0
    assert(b.last_player_won());
}

static void test_horizontal_win() {
    Board b;
    b.make_move(0); b.make_move(0);
    b.make_move(1); b.make_move(1);
    b.make_move(2); b.make_move(2);
    assert(!b.last_player_won());
    b.make_move(3);  // p0 wins horizontally at row 0
    assert(b.last_player_won());
}

static void test_diagonal_forward_win() {
    // / diagonal: p0 wins at (col0,r0),(col1,r1),(col2,r2),(col3,r3)
    // Carefully constructed so neither player wins early.
    // p0 fillers: col4, col5, col6 (each once, no vertical 4-in-a-row)
    // p1 fillers: col1r0, col2r0,r1, col3r0,r1,r2 (no 4-in-a-row)
    Board b;
    b.make_move(0); // p0 col0r0 (diagonal 1st)
    b.make_move(1); // p1 col1r0
    b.make_move(4); // p0 filler
    b.make_move(2); // p1 col2r0
    b.make_move(1); // p0 col1r1 (diagonal 2nd)
    b.make_move(2); // p1 col2r1
    b.make_move(5); // p0 filler
    b.make_move(3); // p1 col3r0
    b.make_move(2); // p0 col2r2 (diagonal 3rd)
    b.make_move(3); // p1 col3r1
    b.make_move(6); // p0 filler
    b.make_move(3); // p1 col3r2
    assert(!b.last_player_won());
    b.make_move(3); // p0 col3r3 — wins diagonally!
    assert(b.last_player_won());
}

static void test_diagonal_backward_win() {
    // \ diagonal: p0 wins at (col3,r0),(col2,r1),(col1,r2),(col0,r3)
    Board b;
    b.make_move(3); // p0 col3r0 (diagonal 1st)
    b.make_move(2); // p1 col2r0
    b.make_move(4); // p0 filler
    b.make_move(1); // p1 col1r0
    b.make_move(2); // p0 col2r1 (diagonal 2nd)
    b.make_move(1); // p1 col1r1
    b.make_move(5); // p0 filler
    b.make_move(0); // p1 col0r0
    b.make_move(1); // p0 col1r2 (diagonal 3rd)
    b.make_move(0); // p1 col0r1
    b.make_move(6); // p0 filler
    b.make_move(0); // p1 col0r2
    assert(!b.last_player_won());
    b.make_move(0); // p0 col0r3 — wins diagonally!
    assert(b.last_player_won());
}

static void test_full_column() {
    Board b;
    for (int r = 0; r < ROWS; ++r) {
        assert(b.is_valid(0));
        b.make_move(0);
        if (r < ROWS - 1) b.make_move(1);
    }
    assert(!b.is_valid(0));
}

static void test_undo_move() {
    Board b;
    b.make_move(3);
    b.make_move(2);
    uint64_t key_before = b.key();
    b.make_move(3);
    b.undo_move(3);
    assert(b.key() == key_before);
    assert(b.moves_played() == 2);
    assert(b.current_player() == 0);
}

static void test_mirror_col() {
    assert(Board::mirror_col(0) == 6);
    assert(Board::mirror_col(3) == 3);
    assert(Board::mirror_col(6) == 0);
}

static void test_key_mirrored() {
    Board b;
    b.make_move(0);  // play in col 0

    Board bm;
    bm.make_move(6); // mirror: col 6

    // b's mirrored key should equal bm's normal key
    assert(b.key_mirrored() == bm.key());
}

static void test_no_win_on_first_moves() {
    Board b;
    b.make_move(3);
    assert(!b.last_player_won());
    b.make_move(3);
    assert(!b.last_player_won());
}

int main() {
    test_empty_board();
    std::cout << "  empty_board: ok\n";
    test_vertical_win();
    std::cout << "  vertical_win: ok\n";
    test_horizontal_win();
    std::cout << "  horizontal_win: ok\n";
    test_diagonal_forward_win();
    std::cout << "  diagonal_forward_win: ok\n";
    test_diagonal_backward_win();
    std::cout << "  diagonal_backward_win: ok\n";
    test_full_column();
    std::cout << "  full_column: ok\n";
    test_undo_move();
    std::cout << "  undo_move: ok\n";
    test_mirror_col();
    std::cout << "  mirror_col: ok\n";
    test_key_mirrored();
    std::cout << "  key_mirrored: ok\n";
    test_no_win_on_first_moves();
    std::cout << "  no_win_early: ok\n";
    std::cout << "All board tests passed.\n";
    return 0;
}
