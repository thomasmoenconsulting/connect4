// Game monitor: replays a fixed move sequence, computing AI response only at the end.
//
// Usage: ./monitor <diff 1-4> <budget_s> [move1 move2 ...]
//   Moves alternate human/AI starting with human.
//   Moves at even positions (0-based) are human moves.
//   Moves at odd positions are STORED AI moves — replayed as-is, never recomputed.
//   If the sequence ends on a human move, compute+print the AI's response.
//   The printed "AI MOVE: N" line lets the caller record and pass it back next time.
//
// Example session (caller records AI moves and passes them back):
//   ./monitor 3 5 4        -> human:4, AI computes -> prints "AI MOVE: 4"
//   ./monitor 3 5 4 4 4    -> human:4, AI:4(stored), human:4, AI computes -> "AI MOVE: 3"
//   ./monitor 3 5 4 4 4 3 7 -> human:4, AI:4, human:4, AI:3(stored), human:7, AI computes

#include "board.hpp"
#include "solver.hpp"
#include <iostream>
#include <chrono>
#include <cstdlib>
#include <string>

static void print_board(const Board& b) {
    std::cout << b.to_string() << "\n";
}

static void print_analysis(const SearchResult& r, int moves_played) {
    Outcome out = r.outcome(0);
    if (r.is_exact) {
        const char* label = out == Outcome::WIN  ? "WIN"
                         : out == Outcome::DRAW ? "DRAW"
                                                : "LOSS";
        std::cout << "  Position   : " << label;
        if (out != Outcome::DRAW) {
            int from_now = r.moves_to_end() - moves_played;
            if (from_now <= 0) from_now = 1;
            std::cout << "  (" << (out == Outcome::WIN ? "you" : "AI")
                      << " wins in ~" << from_now << " moves)";
        }
    } else {
        const char* hint = out == Outcome::WIN  ? "looks good for you"
                        : out == Outcome::DRAW ? "looks balanced"
                                               : "looks worrying";
        std::cout << "  Estimate   : " << hint
                  << "  (depth " << r.depth_reached << "/" << (CELLS - moves_played)
                  << ", " << r.confidence_pct(moves_played) << "% depth)";
    }
    std::cout << "\n";
}

int main(int argc, char* argv[]) {
    if (argc < 3) {
        std::cerr << "Usage: monitor <diff 1-4> <budget_s> [move ...]\n"
                  << "  Moves alternate human/AI. AI moves are replayed as-is.\n"
                  << "  If sequence ends on human move, AI response is computed.\n";
        return 1;
    }

    int diff_n   = std::atoi(argv[1]);
    int budget_s = std::atoi(argv[2]);

    Difficulty diff;
    switch (diff_n) {
        case 1:  diff = Difficulty::EASY;    break;
        case 2:  diff = Difficulty::MEDIUM;  break;
        case 3:  diff = Difficulty::STRONG;  break;
        default: diff = Difficulty::PERFECT; break;
    }

    Solver solver(diff);
    if (diff == Difficulty::STRONG) solver.set_time_budget_ms(budget_s * 1000);

    // Parse the move sequence: alternating human / stored-AI
    int n_moves = argc - 3;
    Board board;
    int arg_idx = 3;

    std::cout << "============================\n";
    std::cout << "   CONNECT FOUR — MONITOR\n";
    std::cout << "============================\n\n";

    bool game_over = false;

    // Replay all stored moves
    for (int i = 0; i < n_moves && !game_over; ++i) {
        int col = std::atoi(argv[arg_idx++]) - 1;
        int player = board.current_player();
        bool is_human = (player == 0);

        print_board(board);

        if (!board.is_valid(col)) {
            std::cout << "  ERROR: " << (is_human ? "Human" : "AI")
                      << " move col " << col + 1 << " is invalid\n";
            return 1;
        }

        if (is_human) {
            std::cout << "  You played column " << col + 1 << "\n\n";
        } else {
            std::cout << "  AI (O) played column " << col + 1 << " [stored]\n\n";
        }

        board.make_move(col);

        if (board.last_player_won()) {
            print_board(board);
            if (is_human) std::cout << "  You win!\n";
            else          std::cout << "  AI wins.\n";
            game_over = true;
        } else if (board.is_full()) {
            print_board(board);
            std::cout << "  Draw!\n";
            game_over = true;
        }
    }

    if (game_over) return 0;

    // If the sequence ended on a human move, compute the AI response
    if (board.current_player() == 1) {
        print_board(board);
        std::cout << "  AI (O) thinking...\n";
        auto t0 = std::chrono::steady_clock::now();
        SearchResult result = solver.solve(board);
        auto t1 = std::chrono::steady_clock::now();
        double ms = std::chrono::duration<double, std::milli>(t1 - t0).count();

        int col = result.best_col;
        std::cout << "  AI played column " << col + 1
                  << "  [depth " << result.depth_reached
                  << " | " << result.nodes_evaluated / 1000 << "k nodes"
                  << " | " << ms / 1000.0 << "s]\n";
        print_analysis(result, board.moves_played());
        std::cout << "\n";

        // Print a parseable marker so the caller can record this move
        std::cout << "AI MOVE: " << col + 1 << "\n\n";

        board.make_move(col);
        if (board.last_player_won()) {
            print_board(board);
            std::cout << "  AI wins.\n";
            return 0;
        }
        if (board.is_full()) {
            print_board(board);
            std::cout << "  Draw!\n";
            return 0;
        }
    }

    // Show current board and whose turn it is
    print_board(board);
    if (board.current_player() == 0)
        std::cout << "  [YOUR TURN — X to move]\n";

    return 0;
}
