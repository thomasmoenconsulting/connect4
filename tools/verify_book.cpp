// Spot-check the book against known Pons results.
// Usage: ./verify_book data/connect4.book
#include "../src/board.hpp"
#include "../src/opening_book.hpp"
#include <algorithm>
#include <cstdio>
#include <iostream>
#include <string>
#include <vector>

struct Case {
    const char* seq;      // move sequence (1-indexed cols)
    int         expected; // Pons score
};

// Expected scores in our solver's convention (converted from Pons' |P| via
// |S| = |P|/2 + 2 for decisive, 0 for draws).
static Case CASES[] = {
    {"",       2},   // Pons +1  -> +2
    {"1",      3},   // Pons +2  -> +3
    {"4",     -2},   // Pons -1  -> -2
    {"44",     2},   // Pons +1  -> +2
    {"4433",  11},   // Pons +18 -> +11
    {"443322", 11},
};

int main(int argc, char** argv) {
    if (argc != 2) { std::cerr << "Usage: " << argv[0] << " <book>\n"; return 1; }
    OpeningBook book;
    if (!book.load(argv[1])) { std::cerr << "load failed\n"; return 1; }
    std::cout << "Book: " << book.size() << " entries, max_plies=" << book.max_plies() << "\n";

    int fails = 0;
    for (const Case& c : CASES) {
        Board b;
        for (const char* p = c.seq; *p; ++p) {
            int col = *p - '1';  // 1-indexed → 0-indexed
            b.make_move(col);
        }
        uint64_t k = std::min(b.key(), b.key_mirrored());
        int got = book.probe(k);
        bool ok = (got == c.expected);
        std::printf("  '%s' ply=%d: book=%4d  expected=%4d  %s\n",
                    c.seq, b.moves_played(), got, c.expected, ok ? "OK" : "FAIL");
        if (!ok) ++fails;
    }
    return fails;
}
