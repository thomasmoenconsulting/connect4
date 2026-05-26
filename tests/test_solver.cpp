#include "../src/board.hpp"
#include "../src/solver.hpp"
#include <cassert>
#include <iostream>

static void test_immediate_win() {
    // p0 has 3 in a row; solver must play the 4th
    Board b;
    b.make_move(0); b.make_move(6);
    b.make_move(1); b.make_move(6);
    b.make_move(2); b.make_move(6);
    // p0 to move — col 3 wins immediately
    Solver solver(Difficulty::PERFECT);
    auto r = solver.solve(b);
    assert(r.best_col == 3);
    assert(r.score > 0);
}

static void test_block_opponent_win() {
    // p0 has 3 in cols 0-2; it's p1's turn — must block at col 3
    // Use MEDIUM (depth 7): fast, still sees the 1-ply threat
    Board b;
    b.make_move(0); b.make_move(6);
    b.make_move(1); b.make_move(6);
    b.make_move(2);  // p0; now p1 to move
    Solver solver(Difficulty::MEDIUM);
    auto r = solver.solve(b);
    assert(r.best_col == 3);
}

static void test_easy_returns_valid_move() {
    Board b;
    Solver solver(Difficulty::EASY);
    auto r = solver.solve(b);
    assert(r.best_col >= 0 && r.best_col < COLS);
    assert(b.is_valid(r.best_col));
}

static void test_medium_returns_valid_move() {
    Board b;
    b.make_move(3); b.make_move(3);
    b.make_move(2); b.make_move(4);
    Solver solver(Difficulty::MEDIUM);
    auto r = solver.solve(b);
    assert(r.best_col >= 0 && r.best_col < COLS);
    assert(b.is_valid(r.best_col));
}

static void test_easy_mode_depth_limited() {
    Board b;
    Solver solver(Difficulty::EASY);
    auto r = solver.solve(b);
    assert(!r.is_exact);
    assert(r.depth_reached <= 3);
}

static void test_confidence_pct_range() {
    Board b;
    Solver solver_easy(Difficulty::EASY);
    auto r = solver_easy.solve(b);
    int pct = r.confidence_pct(b.moves_played());
    assert(pct >= 0 && pct <= 100);
}

static void test_outcome_helpers_on_winning_position() {
    // p0 has an immediate win — verify outcome/moves_to_end
    Board b;
    b.make_move(0); b.make_move(6);
    b.make_move(1); b.make_move(6);
    b.make_move(2); b.make_move(6);
    Solver solver(Difficulty::PERFECT);
    auto r = solver.solve(b);
    assert(r.outcome(0) == Outcome::WIN);
    assert(r.is_exact);
    assert(r.moves_to_end() > 0);
}

int main() {
    test_immediate_win();
    std::cout << "  immediate_win: ok\n";

    test_block_opponent_win();
    std::cout << "  block_opponent_win: ok\n";

    test_easy_returns_valid_move();
    std::cout << "  easy_returns_valid_move: ok\n";

    test_medium_returns_valid_move();
    std::cout << "  medium_returns_valid_move: ok\n";

    test_easy_mode_depth_limited();
    std::cout << "  easy_mode_depth_limited: ok\n";

    test_confidence_pct_range();
    std::cout << "  confidence_pct_range: ok\n";

    test_outcome_helpers_on_winning_position();
    std::cout << "  outcome_helpers: ok\n";

    std::cout << "All solver tests passed.\n";
    return 0;
}
