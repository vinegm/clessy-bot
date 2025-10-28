#pragma once

#include "engine.hpp"

#include <memory>
#include <vector>

class ClessUCI {
public:
  void run();

private:
  std::unique_ptr<ClessEngine> engine = std::make_unique<ClessEngine>();
  bool debug_mode = false;

  // Commands
  void handle_uci();
  void handle_debug(const std::vector<std::string> &tokens);
  void handle_isready();
  void handle_setoption(const std::vector<std::string> &tokens);
  void handle_register(const std::vector<std::string> &tokens);
  void handle_ucinewgame();
  void handle_position(const std::vector<std::string> &tokens);
  void handle_go(const std::vector<std::string> &tokens);
  void handle_stop();
  void handle_ponderhit();

  // Utility
  void call_command(const std::vector<std::string> &tokens);
  std::vector<std::string> split_string(const std::string &str);
  void send_response(const std::string &response);
  void send_info(const std::string &info);

  static std::string square_to_uci(Square square);
  static Square uci_to_square(const std::string &uci);
  static std::string move_to_uci(const Move &move);
  Move uci_to_move(const std::string &uci);

  static char piece_type_to_char(PieceType type);
  static PieceType char_to_piece_type(char c);
};
