#pragma once

#include "engine.hpp"

#include <vector>

class ClessUCI {
public:
  void run();

private:
  ClessEngine engine;

  void call_command(const std::vector<std::string> &tokens);

  // Commands
  void handle_uci();
  void handle_isready();
  void handle_setoption(const std::vector<std::string> &tokens);
  void handle_ucinewgame();
  void handle_position(const std::vector<std::string> &tokens);
  void handle_go(const std::vector<std::string> &tokens);
  void handle_d(const std::vector<std::string> &tokens);

  void send_response(const std::string &response);
  void send_info(const std::string &info);
  void process_moves(const std::vector<std::string> &tokens, size_t start_index);
};
