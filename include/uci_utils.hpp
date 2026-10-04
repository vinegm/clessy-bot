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

bool is_command(const std::string &token);

bool is_go_keyword(const std::string &token);

bool parse_setoption(const std::vector<std::string> &tokens, std::string &name, std::string &value);

bool parse_position_fen(const std::vector<std::string> &tokens, std::string &fen, size_t &end);

// NOTE: searchmoves stay strings, only the engine can turn them into legal moves
void parse_go_limits(
    const std::vector<std::string> &tokens,
    SearchLimits &limits,
    bool &ponder,
    std::vector<std::string> &searchmoves
);

std::string score_to_uci(int score);
std::string format_info_line(const SearchResult &result, int multipv, int hashfull);

std::string format_board(const Piece squares[64]);
