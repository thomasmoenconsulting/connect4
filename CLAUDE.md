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

## Known limitations

PERFECT mode is only practical for mid/late game (30+ moves played). Early-game positions require tens of minutes even with alpha-beta and TT. The solver lacks PVS/aspiration windows and an opening book.
