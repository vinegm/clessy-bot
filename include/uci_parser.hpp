#pragma once

#include "search.hpp"

#include <string>
#include <vector>

class UciParser {
public:
  UciParser() = delete;

  static std::vector<std::string> split_string(const std::string &str);

  static bool is_command(const std::string &token);
  static bool is_go_keyword(const std::string &token);

  static bool parse_setoption(
      const std::vector<std::string> &tokens,
      std::string &name,
      std::string &value
  );
  static bool
      parse_position_fen(const std::vector<std::string> &tokens, std::string &fen, size_t &end);

  // NOTE: searchmoves stay strings, only the engine can turn them into legal moves
  static void parse_go_limits(
      const std::vector<std::string> &tokens,
      SearchLimits &limits,
      bool &ponder,
      std::vector<std::string> &searchmoves
  );

private:
  static void append_token(std::string &target, const std::string &token);
};
