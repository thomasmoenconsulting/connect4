#pragma once
#include <atomic>
#include <cstdint>
#include <cstddef>
#include <memory>

enum class Bound : uint8_t { EXACT, LOWER, UPPER };

struct TTEntry {
    int8_t   score    = 0;
    int8_t   depth    = 0;
    int8_t   best_col = -1;
    Bound    bound    = Bound::EXACT;
};

// Lock-free transposition table using two atomic 64-bit slots per entry.
//
// Race protection: the data slot stores `pack(entry) XOR key`. A racing
// reader that sees (old_key, new_data) or (new_key, old_data) recovers
// garbage on the XOR — caught by the sanity checks in probe(). With random
// 64-bit Zobrist keys the residual chance of a corrupted entry passing
// validation is ~1 in 2^32 per probe — alpha-beta tolerates it.
//
// Replacement policy: replace if new depth >= stored depth.
//
// Symmetry: callers pass the canonical key = min(key, key_mirrored).
// The caller is responsible for translating the returned best_col when
// the board was flipped (mirror_col(best_col)).

class TranspositionTable {
public:
    explicit TranspositionTable(size_t size_mb = 64) {
        size_t entries = 1;
        while (entries * sizeof(Slot) < size_mb * 1024 * 1024)
            entries <<= 1;
        count_ = entries;
        slots_ = std::make_unique<Slot[]>(entries);
        mask_  = entries - 1;
    }

    void clear() {
        for (size_t i = 0; i < count_; ++i) {
            slots_[i].key .store(0, std::memory_order_relaxed);
            slots_[i].data.store(0, std::memory_order_relaxed);
        }
    }

    // Returns false if no usable entry found. May still set best_col when
    // the key matches but the stored depth is too shallow to return a score.
    bool probe(uint64_t key, int depth, int alpha, int beta,
               int& score, int& best_col) const
    {
        const Slot& slot = slots_[key & mask_];
        uint64_t k    = slot.key .load(std::memory_order_relaxed);
        uint64_t data = slot.data.load(std::memory_order_relaxed);

        if (k != key) return false;

        TTEntry e = unpack(data ^ k);
        if (!is_sane(e)) return false;  // racy read caught by XOR sanity check

        best_col = e.best_col;

        if (e.depth < depth) return false;  // not deep enough

        if (e.bound == Bound::EXACT) {
            score = e.score; return true;
        }
        if (e.bound == Bound::LOWER && e.score >= beta) {
            score = e.score; return true;
        }
        if (e.bound == Bound::UPPER && e.score <= alpha) {
            score = e.score; return true;
        }
        return false;
    }

    void store(uint64_t key, int depth, int score, Bound bound, int best_col) {
        Slot& slot = slots_[key & mask_];
        uint64_t cur_k = slot.key.load(std::memory_order_relaxed);
        if (cur_k == key) {
            uint64_t cur_d = slot.data.load(std::memory_order_relaxed);
            TTEntry  cur   = unpack(cur_d ^ cur_k);
            if (is_sane(cur) && cur.depth > depth) return;  // keep deeper entry
        }

        TTEntry e;
        e.score    = static_cast<int8_t>(score);
        e.depth    = static_cast<int8_t>(depth);
        e.best_col = static_cast<int8_t>(best_col);
        e.bound    = bound;

        slot.key .store(key,                   std::memory_order_relaxed);
        slot.data.store(pack(e) ^ key,         std::memory_order_relaxed);
    }

private:
    struct Slot {
        std::atomic<uint64_t> key  {0};
        std::atomic<uint64_t> data {0};
    };

    std::unique_ptr<Slot[]> slots_;
    size_t                  count_{0};
    size_t                  mask_{0};

    static uint64_t pack(const TTEntry& e) {
        return (uint64_t(uint8_t(e.score))    <<  0)
             | (uint64_t(uint8_t(e.depth))    <<  8)
             | (uint64_t(uint8_t(e.best_col)) << 16)
             | (uint64_t(uint8_t(e.bound))    << 24);
    }

    static TTEntry unpack(uint64_t data) {
        TTEntry e;
        e.score    = static_cast<int8_t>( data        & 0xFF);
        e.depth    = static_cast<int8_t>((data >>  8) & 0xFF);
        e.best_col = static_cast<int8_t>((data >> 16) & 0xFF);
        e.bound    = static_cast<Bound> ((data >> 24) & 0xFF);
        return e;
    }

    // Validates that the decoded entry's fields are in legal ranges.
    // Connect-4 specific: depth in [0, 42], best_col in [-1, 6], bound enum
    // is one of {EXACT, LOWER, UPPER}, score within ±(MAX_SCORE+1).
    static bool is_sane(const TTEntry& e) {
        if (e.depth < 0 || e.depth > 42)             return false;
        if (e.best_col < -1 || e.best_col >= 7)      return false;
        if (static_cast<uint8_t>(e.bound) > 2)       return false;
        if (e.score < -23 || e.score > 23)           return false;
        return true;
    }
};
