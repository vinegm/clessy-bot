#include "uci_parser.hpp"

#include "logger.hpp"

#include <sstream>

void UciParser::append_token(std::string &target, const std::string &token) {
  if (!target.empty()) target += ' ';
  target += token;
}

std::vector<std::string> UciParser::split_string(const std::string &str) {
  std::vector<std::string> tokens;
  std::istringstream iss(str);
  std::string token;

  while (iss >> token) {
    tokens.push_back(token);
  }

  return tokens;
}

bool UciParser::is_command(const std::string &token) {
  return token == "uci" || token == "debug" || token == "isready" || token == "setoption"
         || token == "register" || token == "ucinewgame" || token == "position" || token == "go"
         || token == "stop" || token == "ponderhit" || token == "quit" || token == "exit"
         || token == "d" || token == "nnue" || token == "eval";
}

bool UciParser::is_go_keyword(const std::string &token) {
  return token == "searchmoves" || token == "ponder" || token == "wtime" || token == "btime"
         || token == "winc" || token == "binc" || token == "movestogo" || token == "depth"
         || token == "nodes" || token == "mate" || token == "movetime" || token == "infinite"
         || token == "perft";
}

bool UciParser::parse_setoption(
    const std::vector<std::string> &tokens,
    std::string &name,
    std::string &value
) {
  if (tokens.size() < 2 || tokens[1] != "name") {
    Logger::warn("setoption: expected 'name'");
    return false;
  }

  size_t index = 2;
  for (; index < tokens.size() && tokens[index] != "value"; index++) {
    append_token(name, tokens[index]);
  }

  for (index++; index < tokens.size(); index++) {
    append_token(value, tokens[index]);
  }

  return true;
}

bool UciParser::parse_position_fen(
    const std::vector<std::string> &tokens,
    std::string &fen,
    size_t &end
) {
  int fields = 0;
  for (end = 2; end < tokens.size() && tokens[end] != "moves"; end++) {
    append_token(fen, tokens[end]);
    fields++;
  }

  // NOTE: the clocks are optional in practice
  if (fields >= 4) return true;

  Logger::warn("position: FEN needs at least 4 fields, got ", fields);
  return false;
}

void UciParser::parse_go_limits(
    const std::vector<std::string> &tokens,
    SearchLimits &limits,
    bool &ponder,
    std::vector<std::string> &searchmoves
) {
  for (size_t i = 1; i < tokens.size(); i++) {
    const std::string &token = tokens[i];

    if (token == "infinite") {
      limits.infinite = true;
      continue;
    }

    if (token == "ponder") {
      ponder = true;
      continue;
    }

    if (token == "searchmoves") {
      for (i++; i < tokens.size() && !is_go_keyword(tokens[i]); i++) {
        searchmoves.push_back(tokens[i]);
      }

      // NOTE: step back so the loop lands on the keyword that ended the list
      i--;
      continue;
    }

    if (i + 1 >= tokens.size()) continue;

    if (token == "depth") limits.depth = std::stoi(tokens[i + 1]);
    if (token == "nodes") limits.nodes = std::stoll(tokens[i + 1]);
    if (token == "mate") limits.mate = std::stoi(tokens[i + 1]);
    if (token == "movetime") limits.movetime = std::stoll(tokens[i + 1]);
    if (token == "movestogo") limits.movestogo = std::stoi(tokens[i + 1]);
    if (token == "wtime") limits.wtime = std::stoll(tokens[i + 1]);
    if (token == "btime") limits.btime = std::stoll(tokens[i + 1]);
    if (token == "winc") limits.winc = std::stoll(tokens[i + 1]);
    if (token == "binc") limits.binc = std::stoll(tokens[i + 1]);
  }
}
