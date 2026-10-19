#include "opening_book.hpp"
#include <algorithm>
#include <cstring>
#include <fstream>
#include <iostream>

namespace {

struct Header {
    char     magic[4];
    uint32_t version;
    uint32_t n_entries;
    uint8_t  max_plies;
    int8_t   score_min;
    int8_t   score_max;
    uint8_t  reserved;
};
static_assert(sizeof(Header) == 16, "book header must be 16 bytes");

}  // namespace

bool OpeningBook::load(const std::string& path) {
    entries_.clear();
    max_plies_ = 0;

    std::ifstream in(path, std::ios::binary);
    if (!in) return false;

    Header h{};
    in.read(reinterpret_cast<char*>(&h), sizeof(h));
    if (!in || std::memcmp(h.magic, MAGIC, 4) != 0 || h.version != VERSION) {
        return false;
    }

    entries_.resize(h.n_entries);
    // Entry is {uint64, int8} = 9 bytes packed on disk (no padding).
    // We read with a fixed-size temporary to be portable against struct alignment.
    for (uint32_t i = 0; i < h.n_entries; ++i) {
        unsigned char buf[9];
        in.read(reinterpret_cast<char*>(buf), 9);
        if (!in) { entries_.clear(); return false; }
        uint64_t k;
        std::memcpy(&k, buf, 8);
        entries_[i].key   = k;
        entries_[i].score = static_cast<int8_t>(buf[8]);
    }

    max_plies_ = h.max_plies;
    return true;
}

int OpeningBook::probe(uint64_t canonical_key) const {
    auto it = std::lower_bound(entries_.begin(), entries_.end(), canonical_key,
                               [](const Entry& e, uint64_t k) { return e.key < k; });
    if (it == entries_.end() || it->key != canonical_key) return MISS;
    return it->score;
}

bool OpeningBook::save(const std::string& path,
                       int max_plies,
                       std::vector<Entry> entries) {
    std::sort(entries.begin(), entries.end(),
              [](const Entry& a, const Entry& b) { return a.key < b.key; });
    // Dedupe (identical keys with identical score keep one; conflicting scores -> error).
    size_t w = 0;
    for (size_t r = 0; r < entries.size(); ++r) {
        if (w > 0 && entries[w - 1].key == entries[r].key) {
            if (entries[w - 1].score != entries[r].score) {
                std::cerr << "OpeningBook::save: conflicting scores for key "
                          << entries[r].key << std::endl;
                return false;
            }
            continue;
        }
        entries[w++] = entries[r];
    }
    entries.resize(w);

    std::ofstream out(path, std::ios::binary | std::ios::trunc);
    if (!out) return false;

    Header h{};
    std::memcpy(h.magic, MAGIC, 4);
    h.version   = VERSION;
    h.n_entries = static_cast<uint32_t>(entries.size());
    h.max_plies = static_cast<uint8_t>(max_plies);
    h.score_min = -22;
    h.score_max = +22;
    h.reserved  = 0;
    out.write(reinterpret_cast<const char*>(&h), sizeof(h));

    for (const Entry& e : entries) {
        unsigned char buf[9];
        std::memcpy(buf, &e.key, 8);
        buf[8] = static_cast<unsigned char>(e.score);
        out.write(reinterpret_cast<const char*>(buf), 9);
    }
    return static_cast<bool>(out);
}
