// Importer: enumerate canonical positions up to ply N, query Pons' c4solver
// for each position's exact score, write our own book file keyed by Zobrist.
//
// Usage: import_book <path to c4solver> <path to 7x6.book> <out book path>
//
// Strategy:
//   1. DFS enumerate all unique positions (canonical by min(key, mirror_key))
//      up to MAX_PLIES, excluding positions reached by a winning move (those
//      are terminal and Pons' solver treats them as errors).
//   2. For each position, record (move_string, zobrist_canonical_key).
//   3. Pipe all move strings to Pons' c4solver via a tempfile, collect scores.
//   4. Write our book file.

#include "../src/board.hpp"
#include "../src/opening_book.hpp"

#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <iostream>
#include <sstream>
#include <string>
#include <unordered_set>
#include <vector>

static constexpr int MAX_PLIES = 10;

struct Entry {
    std::string moves;    // "4433..." move sequence (1-indexed cols, Pons convention)
    uint64_t    key;      // canonical Zobrist
};

static std::unordered_set<uint64_t> visited;
static std::vector<Entry>           out;

static void dfs(Board& b, std::string& moves) {
    uint64_t canon = std::min(b.key(), b.key_mirrored());
    if (!visited.insert(canon).second) return;  // already enumerated

    out.push_back({moves, canon});

    if ((int)moves.size() >= MAX_PLIES) return;

    for (int c = 0; c < COLS; ++c) {
        if (!b.is_valid(c)) continue;
        b.make_move(c);
        if (b.last_player_won()) {
            // Terminal — don't include or recurse. Pons rejects won positions.
            b.undo_move(c);
            continue;
        }
        moves.push_back('1' + c);
        dfs(b, moves);
        moves.pop_back();
        b.undo_move(c);
    }
}

int main(int argc, char** argv) {
    if (argc != 4) {
        std::cerr << "Usage: " << argv[0]
                  << " <c4solver> <7x6.book> <out.book>\n";
        return 1;
    }
    const std::string pons_bin  = argv[1];
    const std::string pons_book = argv[2];
    const std::string out_path  = argv[3];

    std::cerr << "Enumerating positions up to ply " << MAX_PLIES << "...\n";
    Board b;
    std::string moves;
    moves.reserve(MAX_PLIES);
    dfs(b, moves);
    std::cerr << "  " << out.size() << " unique positions\n";

    // Write move sequences to a tempfile; Pons' solver reads line-by-line.
    const std::string moves_file  = out_path + ".moves";
    const std::string scores_file = out_path + ".scores";

    {
        std::ofstream mf(moves_file);
        if (!mf) { std::cerr << "cannot write " << moves_file << "\n"; return 1; }
        for (const auto& e : out) mf << e.moves << "\n";
    }
    std::cerr << "Running Pons' solver (this takes a few minutes)...\n";

    // Shell-escape paths minimally (we control them from argv; no spaces expected).
    std::ostringstream cmd;
    cmd << "'" << pons_bin << "' -b '" << pons_book << "' < '" << moves_file
        << "' > '" << scores_file << "'";
    int rc = std::system(cmd.str().c_str());
    if (rc != 0) {
        std::cerr << "c4solver exited " << rc << "\n";
        return 1;
    }

    // Parse scores. Each line is "<sequence> <score>" (sequence may be empty).
    std::cerr << "Parsing scores...\n";
    std::ifstream sf(scores_file);
    if (!sf) { std::cerr << "cannot read " << scores_file << "\n"; return 1; }

    std::vector<OpeningBook::Entry> book_entries;
    book_entries.reserve(out.size());

    std::string line;
    size_t idx = 0;
    while (std::getline(sf, line)) {
        if (idx >= out.size()) {
            std::cerr << "more score lines than queries\n"; return 1;
        }
        // Split on last space (handles empty-sequence case: " 1").
        auto sp = line.rfind(' ');
        if (sp == std::string::npos) {
            std::cerr << "malformed line: '" << line << "'\n"; return 1;
        }
        const std::string seq_part = line.substr(0, sp);
        const std::string score_part = line.substr(sp + 1);
        if (seq_part != out[idx].moves) {
            std::cerr << "sequence mismatch at " << idx << ": expected '"
                      << out[idx].moves << "' got '" << seq_part << "'\n";
            return 1;
        }
        int score = std::atoi(score_part.c_str());
        if (score < -22 || score > 22) {
            std::cerr << "score out of range at " << idx << ": " << score << "\n";
            return 1;
        }
        book_entries.push_back({out[idx].key, (int8_t)score});
        ++idx;
    }
    if (idx != out.size()) {
        std::cerr << "fewer score lines than queries (" << idx << " vs "
                  << out.size() << ")\n";
        return 1;
    }

    std::cerr << "Writing " << out_path << "...\n";
    if (!OpeningBook::save(out_path, MAX_PLIES, std::move(book_entries))) {
        std::cerr << "save failed\n"; return 1;
    }

    // Clean up intermediates.
    std::remove(moves_file.c_str());
    std::remove(scores_file.c_str());

    std::cerr << "Done.\n";
    return 0;
}
