#include "../src/board.hpp"
#include "../src/opening_book.hpp"
#include "../src/solver.hpp"
#include <cassert>
#include <cstdio>
#include <iostream>
#include <string>
#include <vector>

static const std::string TMP_BOOK = "/tmp/connect4_test.book";

// Build a tiny synthetic book covering the empty board and the single
// position reached after X plays column 3 (centre). Verify Solver probes
// it and returns an exact result with zero nodes evaluated.
static void test_book_short_circuit() {
    Board empty;
    Board after_centre;
    after_centre.make_move(3);

    uint64_t k_empty  = std::min(empty.key(),  empty.key_mirrored());
    uint64_t k_centre = std::min(after_centre.key(), after_centre.key_mirrored());

    std::vector<OpeningBook::Entry> entries = {
        {k_empty,  18},   // claim empty board is a +18 win (synthetic)
        {k_centre, -17},  // after centre: opponent now sees -17
    };
    assert(OpeningBook::save(TMP_BOOK, 14, entries));

    Solver solver(Difficulty::PERFECT);
    assert(solver.load_book(TMP_BOOK));
    assert(solver.book().loaded());
    assert(solver.book().max_plies() == 14);
    assert(solver.book().size() == 2);

    Board b;
    auto r = solver.solve(b);
    assert(r.is_exact);
    assert(r.nodes_evaluated == 0);
    assert(r.score == 18);
    // Best col must be centre: only child in the book, score -(-17) = +17.
    assert(r.best_col == 3);

    std::remove(TMP_BOOK.c_str());
}

// Book miss should fall through to normal search without crashing.
static void test_book_miss_falls_through() {
    // Empty book file (0 entries) — probe always misses.
    std::vector<OpeningBook::Entry> none;
    assert(OpeningBook::save(TMP_BOOK, 14, none));

    Solver solver(Difficulty::MEDIUM);
    assert(solver.load_book(TMP_BOOK));

    Board b;
    b.make_move(0); b.make_move(6);
    b.make_move(1); b.make_move(6);
    b.make_move(2);  // p1 must block col 3
    auto r = solver.solve(b);
    assert(r.best_col == 3);

    std::remove(TMP_BOOK.c_str());
}

// Missing book file: load_book returns false; solver still works.
static void test_missing_book() {
    Solver solver(Difficulty::EASY);
    assert(!solver.load_book("/tmp/does_not_exist_connect4.book"));
    assert(!solver.book().loaded());
    Board b;
    auto r = solver.solve(b);
    assert(r.best_col >= 0 && r.best_col < COLS);
}

// Mirror symmetry: a book entry for one orientation must answer the
// mirrored position too.
static void test_mirror_lookup() {
    Board left;
    left.make_move(1);  // X on column 2 (0-indexed col 1)
    Board right;
    right.make_move(5); // mirrored: column 6

    uint64_t k_left  = std::min(left.key(),  left.key_mirrored());
    uint64_t k_right = std::min(right.key(), right.key_mirrored());
    assert(k_left == k_right);  // canonical keys match by construction

    std::vector<OpeningBook::Entry> entries = {{k_left, 5}};
    assert(OpeningBook::save(TMP_BOOK, 14, entries));

    OpeningBook book;
    assert(book.load(TMP_BOOK));
    assert(book.probe(k_left)  == 5);
    assert(book.probe(k_right) == 5);

    std::remove(TMP_BOOK.c_str());
}

int main() {
    test_book_short_circuit();
    test_book_miss_falls_through();
    test_missing_book();
    test_mirror_lookup();
    std::cout << "All book tests passed.\n";
    return 0;
}
