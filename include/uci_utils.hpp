#pragma once

#include "chess_types.hpp"
#include "move_gen.hpp"
#include "search.hpp"

#include <string>
#include <vector>

std::vector<std::string> split_string(const std::string &str);

std::string square_to_uci(Square square);
Square uci_to_square(const std::string &uci);

std::string move_to_uci(const Move &move);
Move uci_to_move(const std::string &uci);
std::string moves_to_uci(const MoveList &moves);

char piece_type_to_char(PieceType type);
PieceType char_to_piece_type(char c);
char piece_to_char(Piece piece);

// Whether the token starts a command, so junk before one can be skipped.
bool is_command(const std::string &token);

// Whether the token is a "go" argument, which is what ends searchmoves.
bool is_go_keyword(const std::string &token);

// Name and value of a "setoption" line, either of which may be several words.
// Reports why on a false.
bool parse_setoption(const std::vector<std::string> &tokens, std::string &name, std::string &value);

// FEN of a "position fen" line, plus the index of the token that ended it.
// Reports why on a false.
bool parse_position_fen(const std::vector<std::string> &tokens, std::string &fen, size_t &end);

// Fills limits from a "go" line. searchmoves come back as strings, since only
// the engine can turn one into a move it can play.
void parse_go_limits(
    const std::vector<std::string> &tokens,
    SearchLimits &limits,
    bool &ponder,
    std::vector<std::string> &searchmoves
);

std::string score_to_uci(int score);
std::string format_info_line(const SearchResult &result, int multipv, int hashfull);

// The board as the `d` command prints it, ranks from 8 down to 1.
std::string format_board(const Piece squares[64]);
