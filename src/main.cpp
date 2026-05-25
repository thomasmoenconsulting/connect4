#include "board.hpp"
#include "solver.hpp"
#include <iostream>
#include <string>
#include <chrono>
#include <cstdlib>

static const char* difficulty_name(Difficulty d) {
    switch (d) {
        case Difficulty::EASY:    return "EASY";
        case Difficulty::MEDIUM:  return "MEDIUM";
        case Difficulty::STRONG:  return "STRONG";
        case Difficulty::PERFECT: return "PERFECT";
    }
    return "?";
}

static void print_header(Difficulty diff, bool two_player) {
    std::cout << "\n";
    if (two_player)
        std::cout << "  Connect 4 — Player X vs Player O\n";
    else
        std::cout << "  Connect 4 — You (X) vs AI (O) — Mode: "
                  << difficulty_name(diff) << "\n";
    std::cout << "\n";
}

static void print_result(const SearchResult& r, int moves_played, bool /*human_turn*/) {
    if (r.best_col < 0) return;

    Outcome out = r.outcome(0);
    int pct     = r.confidence_pct(moves_played);
    int ply_left = CELLS - moves_played;

    // Outcome label (always from human's perspective)
    const char* label = "UNKNOWN";
    if (out == Outcome::WIN)       label = "WIN";
    else if (out == Outcome::DRAW) label = "DRAW";
    else if (out == Outcome::LOSS) label = "LOSS";

    std::cout << "  Your position : " << label;
    if (r.is_exact && r.moves_to_end() > 0)
        std::cout << "  (" << (out == Outcome::WIN ? "you win" : "AI wins")
                  << " in " << r.moves_to_end() << " moves with perfect play)";
    else if (!r.is_exact)
        std::cout << "  (estimated, searched " << r.depth_reached
                  << " of " << ply_left << " remaining plies)";
    std::cout << "\n";

    std::cout << "  Confidence    : " << pct << "%";
    if (pct == 100) std::cout << "  (solved)";
    std::cout << "\n";
}

static int get_human_move(const Board& board) {
    int col;
    while (true) {
        std::cout << "  Your move (1-7): ";
        std::string line;
        if (!std::getline(std::cin, line)) return -1;
        try {
            col = std::stoi(line) - 1;
        } catch (...) {
            std::cout << "  Please enter a number between 1 and 7.\n";
            continue;
        }
        if (board.is_valid(col)) return col;
        std::cout << "  Column " << col + 1 << " is full or invalid. Try again.\n";
    }
}

static Difficulty choose_difficulty() {
    std::cout << "\n  Difficulty:\n"
              << "    1) Easy\n"
              << "    2) Medium\n"
              << "    3) Strong\n"
              << "    4) Perfect\n"
              << "  Choice [4]: ";
    std::string line;
    std::getline(std::cin, line);
    if (line == "1") return Difficulty::EASY;
    if (line == "2") return Difficulty::MEDIUM;
    if (line == "3") return Difficulty::STRONG;
    return Difficulty::PERFECT;
}

static bool choose_two_player() {
    std::cout << "\n  Mode:\n"
              << "    1) Human vs AI\n"
              << "    2) Human vs Human\n"
              << "  Choice [1]: ";
    std::string line;
    std::getline(std::cin, line);
    return line == "2";
}

int main() {
    std::cout << "============================\n";
    std::cout << "       CONNECT  FOUR\n";
    std::cout << "============================\n";

    bool two_player = choose_two_player();
    Difficulty diff = Difficulty::PERFECT;
    if (!two_player) diff = choose_difficulty();

    Solver solver(diff);

    while (true) {
        Board board;
        print_header(diff, two_player);

        SearchResult last_result;
        bool game_over = false;

        while (!game_over) {
            std::cout << board.to_string() << "\n";

            int player = board.current_player();  // 0 = X, 1 = O
            bool is_human = two_player || (player == 0);

            int col = -1;
            if (is_human) {
                // Show the AI's analysis before the human moves (in 1P mode)
                if (!two_player && last_result.best_col >= 0)
                    print_result(last_result, board.moves_played(), true);
                std::cout << "  Player " << (player == 0 ? "X" : "O") << "'s turn\n";
                col = get_human_move(board);
                if (col < 0) { std::cout << "  Goodbye.\n"; return 0; }
            } else {
                // AI move
                std::cout << "  AI is thinking...\n";
                auto t0 = std::chrono::steady_clock::now();
                last_result = solver.solve(board);
                auto t1 = std::chrono::steady_clock::now();
                double ms = std::chrono::duration<double, std::milli>(t1 - t0).count();

                col = last_result.best_col;
                std::cout << "  AI played column " << col + 1
                          << "  [depth " << last_result.depth_reached
                          << " | " << last_result.nodes_evaluated / 1000 << "k nodes"
                          << " | " << ms / 1000.0 << "s]\n\n";
                print_result(last_result, board.moves_played(), false);
                std::cout << "\n";
            }

            board.make_move(col);

            if (board.last_player_won()) {
                std::cout << "\n" << board.to_string() << "\n";
                if (two_player)
                    std::cout << "  Player " << (player == 0 ? "X" : "O") << " wins!\n";
                else
                    std::cout << (player == 0 ? "  You win! Congratulations!\n"
                                              : "  AI wins.\n");
                game_over = true;
            } else if (board.is_full()) {
                std::cout << "\n" << board.to_string() << "\n";
                std::cout << "  It's a draw!\n";
                game_over = true;
            }
        }

        std::cout << "\n  Play again? (y/n) [y]: ";
        std::string ans;
        std::getline(std::cin, ans);
        if (ans == "n" || ans == "N") break;
        solver.clear_tt();
    }

    std::cout << "\n  Thanks for playing!\n";
    return 0;
}
