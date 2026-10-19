# CLAUDE.md

This file provides guidance to Claude Code (claude.ai/code) when working with code in this repository.

## Build & Run

```bash
make            # build ./connect4
make test       # build and run both test suites
make clean      # remove binaries
```

Requires g++ with C++20 and OpenMP support (`libgomp`). cmake is not required — use the Makefile.

## Architecture

**Bitboard** — column-major layout, 7 bits per column (6 data + 1 sentinel), two `uint64_t` per player. Win detection is 4 shift+AND operations (`board.cpp: has_won`). `is_valid` uses `height_[col] < col_base(col) + ROWS` (NOT the sentinel bit in `mask_`, which is never set).

**Transposition table** (`transposition.hpp`) — lock-free, open-addressing, two `std::atomic<uint64_t>` per slot packed as key + data. Uses `std::unique_ptr<Slot[]>` (not `std::vector`) because `std::atomic` is not copyable. Caller passes canonical key = `min(key, key_mirrored)`; a `flipped` flag tracks whether the returned `best_col` needs `mirror_col()` applied.

**Solver** (`solver.cpp`) — negamax + alpha-beta + iterative deepening + OpenMP root parallelism (Young Brothers Wait). First move searched serially to establish alpha; remaining moves searched in parallel with `#pragma omp parallel for`. OMP section skips immediately when `shared_best_score > 0` (winning move already found).

**Time limit** (STRONG mode) — a detached `std::thread` fires after 5 s and sets `abort_`. A generation counter (`solve_gen_`) prevents a timer from a previous call interfering with the next one. `negamax` checks `abort_` every 1024 nodes. In-negamax time checks (`steady_clock::now()`) were tried but proved unreliable under `-O3`; the timer-thread approach is robust.

**Difficulty modes**:
- EASY: depth 3, heuristic eval at leaves
- MEDIUM: depth 7
- STRONG: iterative deepening with 5 s wall-clock limit
- PERFECT: iterative deepening to full depth (slow early-game; provably optimal when it finishes)

**Evaluator** (`evaluator.hpp`) — counts open 4-windows weighted by pieces already in them. Designed as a virtual interface so a CUDA neural evaluator can replace it later without touching the solver.

## Opening book

`src/opening_book.{hpp,cpp}` loads `data/connect4.book` — a 10-ply exact-score table imported from Pascal Pons' published `7x6.book` (github.com/PascalPons/connect4). `Solver::solve()` and `Solver::score_moves()` probe it before searching; a hit returns `is_exact=true` with zero nodes evaluated.

Format: 16-byte header + sorted `(uint64 canonical_key, int8 score)` entries, 9 bytes each. ~1.2M positions, ~10 MB. Keys are Zobrist (`min(key, key_mirrored)` for symmetry).

The book file is a build artifact (gitignored). Regenerate with `make book` after cloning Pons' solver into `/tmp/pons-c4/` and downloading his `7x6.book` release asset (see Makefile `PONS_DIR`/`PONS_BOOK`). The importer in `tools/import_book.cpp` enumerates canonical positions up to `MAX_PLIES` and pipes them through Pons' `c4solver` for exact scoring. Raising `MAX_PLIES` is a one-line change but import time scales super-linearly (14 plies ≈ 30+ hours because most of Pons' 2²³-entry book misses require a full re-solve).

## Known limitations

Without the opening book, PERFECT mode is only practical for mid/late game (30+ moves played). With the book, the first 10 plies are instant; from ply 11 onward the solver runs normally.
