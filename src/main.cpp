#include "board.hpp"
#include "solver.hpp"
#include <iostream>
#include <sstream>
#include <string>
#include <vector>
#include <array>
#include <climits>
#include <chrono>
#include <cstdlib>
#include <cstdio>

static std::string difficulty_name(Difficulty d, int budget_ms) {
    switch (d) {
        case Difficulty::EASY:    return "EASY";
        case Difficulty::MEDIUM:  return "MEDIUM";
        case Difficulty::STRONG:  return "TIMED (" + std::to_string(budget_ms / 1000) + "s)";
        case Difficulty::PERFECT: return "PERFECT";
    }
    return "?";
}

static void print_header(Difficulty diff, int budget_ms, bool two_player) {
    std::cout << "\n";
    if (two_player)
        std::cout << "  Connect 4 — Player X vs Player O\n";
    else
        std::cout << "  Connect 4 — You (X) vs AI (O) — Mode: "
                  << difficulty_name(diff, budget_ms) << "\n";
    std::cout << "\n";
}

static void print_result(const SearchResult& r, int moves_played, bool /*human_turn*/) {
    if (r.best_col < 0) return;

    Outcome out = r.outcome(0);
    int pct     = r.confidence_pct(moves_played);
    int ply_left = CELLS - moves_played;

    if (r.is_exact) {
        // Definitive game-tree result
        const char* label = out == Outcome::WIN  ? "WIN"
                         : out == Outcome::DRAW ? "DRAW"
                                                : "LOSS";
        std::cout << "  Your position : " << label;
        if (out != Outcome::DRAW)
            std::cout << "  (" << (out == Outcome::WIN ? "you win" : "AI wins")
                      << " in " << (r.moves_to_end() - moves_played)
                      << " more moves with perfect play)";
        std::cout << "\n  Confidence    : 100%  (solved)\n";
    } else {
        // Heuristic-only — don't claim a definite outcome
        const char* hint = out == Outcome::WIN  ? "looks good for you"
                        : out == Outcome::DRAW ? "looks balanced"
                                               : "looks worrying";
        std::cout << "  Estimate      : " << hint
                  << "  (searched " << r.depth_reached
                  << " of " << ply_left << " remaining plies, " << pct << "% depth)\n";
    }
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

// Returns {difficulty, time_budget_ms (only meaningful for STRONG)}
static std::pair<Difficulty, int> choose_difficulty() {
    std::cout << "\n  Difficulty:\n"
              << "    1) Easy    (depth 3)\n"
              << "    2) Medium  (depth 7)\n"
              << "    3) Timed   (iterative deepening with a time limit)\n"
              << "    4) Perfect (full search — slow early game)\n"
              << "  Choice [4]: ";
    std::string line;
    std::getline(std::cin, line);
    if (line == "1") return {Difficulty::EASY,   0};
    if (line == "2") return {Difficulty::MEDIUM,  0};
    if (line == "3") {
        std::cout << "  Seconds per move [5]: ";
        std::string s;
        std::getline(std::cin, s);
        int secs = 5;
        try { secs = std::stoi(s); } catch (...) {}
        if (secs < 1) secs = 1;
        return {Difficulty::STRONG, secs * 1000};
    }
    return {Difficulty::PERFECT, 0};
}

static bool choose_info_mode() {
    std::cout << "\n  Show move scores? (y/n) [n]: ";
    std::string line;
    std::getline(std::cin, line);
    return (line == "y" || line == "Y");
}

// ---------------------------------------------------------------------------
// Side-by-side board + score table rendering
// ---------------------------------------------------------------------------

static std::string fmt_score(int s) {
    char buf[16];
    std::snprintf(buf, sizeof(buf), "%+4d", s);
    return buf;
}

static const char* score_label(int s, bool exact) {
    if (exact) {
        if (s > 0) return "WIN ";
        if (s < 0) return "LOSS";
        return "draw";
    }
    if (s >  2) return "win?";
    if (s < -2) return "loss";
    return "~eq ";
}

// Count visual columns (terminal width) of a UTF-8 string.
static int visual_len(const std::string& s) {
    int n = 0;
    for (size_t i = 0; i < s.size(); ) {
        unsigned char c = s[i];
        if      (c < 0x80) { ++i;      ++n; }
        else if (c < 0xE0) { i += 2;   ++n; }
        else if (c < 0xF0) { i += 3;   ++n; }
        else               { i += 4;   ++n; }
    }
    return n;
}

// Print the board with a per-column score table to the right.
// col_scores[c] is from current player's view (INT_MIN = full/invalid).
// player label is shown in the header, e.g. "X" or "O".
static void print_board_scores(
    const Board& board,
    const std::array<int, COLS>& col_scores,
    int best_col,
    bool is_exact,
    const char* player_label = "")
{
    std::string bs = board.to_string();
    std::vector<std::string> lines;
    std::istringstream ss(bs);
    std::string ln;
    while (std::getline(ss, ln)) lines.push_back(ln);
    // Remove trailing empty line (to_string ends with \n)
    if (!lines.empty() && lines.back().empty()) lines.pop_back();
    // lines: 6 board rows (top→bottom) + "───" + " 1 2…" = 8 entries

    for (int i = 0; i < (int)lines.size(); ++i) {
        std::cout << "  " << lines[i];
        // Pad so the table starts at a fixed column (board lines are 15 visual chars wide)
        int pad = 15 - visual_len(lines[i]) + 4;
        for (int p = 0; p < pad; ++p) std::cout << ' ';

        if (i == 0) {
            std::cout << "Col  Score";
            if (player_label[0]) std::cout << " (" << player_label << ")";
        } else if (i >= 1 && i < 1 + COLS) {
            int col = i - 1;
            int s   = col_scores[col];
            if (s == INT_MIN) {
                std::cout << " " << (col + 1) << "   full";
            } else {
                std::cout << " " << (col + 1) << " " << fmt_score(s)
                          << "  " << score_label(s, is_exact);
                if (col == best_col) std::cout << " \xE2\x86\x90";  // UTF-8 ←
            }
        }
        std::cout << "\n";
    }
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
    Difficulty diff       = Difficulty::PERFECT;
    int        budget_ms  = 0;
    if (!two_player) {
        auto [d, b] = choose_difficulty();
        diff = d; budget_ms = b;
    }

    bool info_mode = !two_player && choose_info_mode();

    Solver solver(diff);
    if (diff == Difficulty::STRONG) solver.set_time_budget_ms(budget_ms);

    if (solver.load_book("data/connect4.book")) {
        std::cout << "  Opening book loaded (" << solver.book().max_plies()
                  << " plies, " << solver.book().size() << " positions)\n";
    } else {
        std::cout << "  No opening book (run `make data/connect4.book`)\n";
    }

    while (true) {
        Board board;
        print_header(diff, budget_ms, two_player);

        SearchResult last_result;
        bool game_over = false;

        while (!game_over) {
            int player    = board.current_player();  // 0 = X, 1 = O
            bool is_human = two_player || (player == 0);

            int col = -1;
            if (is_human) {
                // Print board — with score table on human's turn when info mode is on
                if (info_mode && last_result.depth_reached > 0) {
                    auto scores = solver.score_moves(board, last_result.depth_reached);
                    int hi = -1, hi_s = INT_MIN;
                    for (int c = 0; c < COLS; ++c)
                        if (scores[c] != INT_MIN && scores[c] > hi_s) { hi_s = scores[c]; hi = c; }
                    print_board_scores(board, scores, hi, last_result.is_exact, "X");
                } else {
                    std::cout << board.to_string();
                }
                std::cout << "\n";

                if (!two_player && last_result.best_col >= 0)
                    print_result(last_result, board.moves_played(), true);
                std::cout << "  Player " << (player == 0 ? "X" : "O") << "'s turn\n";
                col = get_human_move(board);
                if (col < 0) { std::cout << "  Goodbye.\n"; return 0; }
            } else {
                // AI move — print board first (plain), then think, then show scores
                std::cout << board.to_string() << "\n";
                std::cout << "  AI is thinking...\n";
                auto t0 = std::chrono::steady_clock::now();

                Solver::ProgressCb cb;
                if (diff == Difficulty::STRONG) {
                    cb = [t0, budget_ms](int d, int /*md*/, long nodes) {
                        auto now = std::chrono::steady_clock::now();
                        double elapsed = std::chrono::duration<double, std::milli>(now - t0).count();
                        constexpr int BAR = 32;
                        int filled = static_cast<int>(BAR * elapsed / budget_ms);
                        if (filled > BAR) filled = BAR;
                        std::cout << "\r  [";
                        for (int i = 0; i < BAR; ++i)
                            std::cout << (i < filled ? '#' : '.');
                        std::cout << "] " << static_cast<int>(elapsed / 1000.0) << "s"
                                  << "  depth " << d
                                  << "  " << nodes / 1000 << "k nodes  ";
                        std::cout.flush();
                    };
                } else if (diff == Difficulty::PERFECT) {
                    cb = [](int d, int md, long nodes) {
                        std::cout << "\r  depth " << d << "/" << md
                                  << "  " << nodes / 1000 << "k nodes          ";
                        std::cout.flush();
                    };
                }

                last_result = solver.solve(board, cb);
                auto t1 = std::chrono::steady_clock::now();
                double ms = std::chrono::duration<double, std::milli>(t1 - t0).count();

                if (diff == Difficulty::STRONG || diff == Difficulty::PERFECT)
                    std::cout << "\n";  // finish the progress line

                col = last_result.best_col;
                std::cout << "  AI played column " << col + 1
                          << "  [depth " << last_result.depth_reached
                          << " | " << last_result.nodes_evaluated / 1000 << "k nodes"
                          << " | " << ms / 1000.0 << "s]\n\n";
                print_result(last_result, board.moves_played(), false);
                std::cout << "\n";

                // Show AI's per-column scores after solve (TT is warm — fast)
                if (info_mode) {
                    auto scores = solver.score_moves(board, last_result.depth_reached);
                    print_board_scores(board, scores, col, last_result.is_exact, "O");
                    std::cout << "\n";
                }
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
