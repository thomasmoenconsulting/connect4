#pragma once
#include <cstdint>
#include <string>
#include <vector>
#include <climits>

// Opening book: precomputed exact negamax scores for positions near the
// game start, keyed by the Board's canonical Zobrist key.
//
// File format ("C4BK" v1), little-endian:
//   char   magic[4]   = "C4BK"
//   uint32 version    = 1
//   uint32 n_entries
//   uint8  max_plies           // positions with moves_played() <= max_plies may be in the book
//   int8   score_min = -22
//   int8   score_max = +22
//   uint8  reserved  = 0
//   { uint64 canonical_key; int8 score; }[n_entries]  sorted ascending by canonical_key
//
// Score convention matches Solver::MAX_SCORE: +N = win in (MAX_SCORE - N)
// half-moves from this position, from the current player's view.

class OpeningBook {
public:
    static constexpr char MAGIC[4] = {'C','4','B','K'};
    static constexpr uint32_t VERSION = 1;
    static constexpr int MISS = INT_MIN;

    struct Entry {
        uint64_t key;
        int8_t   score;
    };

    bool load(const std::string& path);           // false on any error (silent book-less fallback)
    bool loaded()    const { return !entries_.empty(); }
    int  max_plies() const { return max_plies_; }
    size_t size()    const { return entries_.size(); }

    // canonical_key = min(board.key(), board.key_mirrored())
    int probe(uint64_t canonical_key) const;      // returns MISS when absent

    // Writer side (used by the importer only).
    static bool save(const std::string& path,
                     int max_plies,
                     std::vector<Entry> entries); // sorts in place, dedupes, writes file

private:
    std::vector<Entry> entries_;                  // sorted by key
    int max_plies_ = 0;
};
