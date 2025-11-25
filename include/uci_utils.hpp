#pragma once

#include "chess_types.hpp"

#include <string>
#include <vector>

std::vector<std::string> split_string(const std::string &str);

std::string square_to_uci(Square square);
Square uci_to_square(const std::string &uci);

std::string move_to_uci(const Move &move);
Move uci_to_move(const std::string &uci);

char piece_type_to_char(PieceType type);
PieceType char_to_piece_type(char c);
