#pragma once

#include "chess_types.hpp"
#include "search.hpp"

#include <string>

class UciFormatter {
public:
  UciFormatter() = delete;

  static std::string score_to_uci(int score);
  static std::string format_info_line(const SearchResult &result, int multipv, int hashfull);
  static std::string format_board(const Piece squares[64]);
};
