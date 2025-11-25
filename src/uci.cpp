#include "uci.hpp"

#include "engine.hpp"
#include "uci_utils.hpp"

#include <iostream>
#include <stdexcept>
#include <sys/types.h>

void ClessUCI::run() {
  std::string line;

  while (std::getline(std::cin, line)) {
    std::vector<std::string> tokens = split_string(line);

    if (tokens.empty()) continue;

    call_command(tokens);
  }
}

void ClessUCI::call_command(const std::vector<std::string> &tokens) {
  std::string command = tokens[0];

  if (command == "uci") return handle_uci();
  if (command == "isready") return handle_isready();
  if (command == "setoption") return handle_setoption(tokens);
  if (command == "ucinewgame") return handle_ucinewgame();
  if (command == "position") return handle_position(tokens);
  if (command == "go") return handle_go(tokens);
  if (command == "d") { return handle_d(tokens); }
  if (command == "quit" || command == "exit") { exit(0); }

  send_info("Unknown command: '" + command + "'");
}

void ClessUCI::handle_uci() {
  send_response("id name Clessy");
  send_response("id author vinegm");
  send_response("uciok");
}

void ClessUCI::handle_isready() { send_response("readyok"); }

void ClessUCI::handle_setoption(const std::vector<std::string> &tokens) {
  throw std::runtime_error("Not implemented");
}

void ClessUCI::handle_ucinewgame() { engine.set_fen(INITIAL_POSITION_FEN); }

void ClessUCI::handle_position(const std::vector<std::string> &tokens) {
  if (tokens.size() < 2) {
    send_response("Position command requires at least one argument");
    return;
  }

  std::string position_type = tokens[1];

  if (position_type == "startpos") {
    engine.set_fen(INITIAL_POSITION_FEN);

    if (tokens.size() > 2 && tokens[2] == "moves") { process_moves(tokens, 3); }

    return;
  }

  if (position_type == "fen") {
    if (tokens.size() < 8) {
      send_response("FEN position invalid: insufficient tokens");
      return;
    }

    std::string fen = tokens[2] + " " + tokens[3] + " " + tokens[4] + " " + tokens[5] + " "
                      + tokens[6] + " " + tokens[7];

    engine.set_fen(fen);

    size_t moves_index = 8;
    if (moves_index < tokens.size() && tokens[moves_index] == "moves") {
      process_moves(tokens, moves_index + 1);
    }
    return;
  }

  send_response("Unknown position type: " + position_type);
}

void ClessUCI::handle_go(const std::vector<std::string> &tokens) {
  if (tokens.size() < 2) {
    throw std::invalid_argument("Go command requires at least one argument");
  }

  if (tokens[1] == "perft") {
    if (tokens.size() < 3) { return send_response("Perft command requires depth argument"); }
    int depth = std::stoi(tokens[2]);

    auto results = engine.perft_divide(depth);

    unsigned long total_nodes = 0;
    for (const auto &[move, nodes] : results) {
      std::string move_str = move_to_uci(move);

      send_response(move_str + ": " + std::to_string(nodes));
      total_nodes += nodes;
    }

    send_response("\nNodes searched: " + std::to_string(total_nodes));
    return;
  }

  MoveList legal_moves = engine.get_legal_moves();

  if (legal_moves.empty()) return send_response("bestmove (none)");

  Move best_move = legal_moves[0];

  send_response("bestmove " + move_to_uci(best_move));
}

void ClessUCI::handle_d(const std::vector<std::string> &tokens) {
  std::string fen = engine.get_fen();
  send_response("Fen: " + fen);

  MoveList legal_moves = engine.get_legal_moves();
  send_response("Legal moves (" + std::to_string(legal_moves.count) + "):");

  std::string moves_str;
  for (int i = 0; i < legal_moves.count; i++) {
    moves_str += move_to_uci(legal_moves.moves[i]) + " ";
  }

  send_response(moves_str);
}

void ClessUCI::send_response(const std::string &response) {
  std::cout << response << std::endl;
  std::cout.flush();
}

void ClessUCI::send_info(const std::string &info) {
  std::cout << "info " << info << std::endl;
  std::cout.flush();
}

void ClessUCI::process_moves(const std::vector<std::string> &tokens, size_t start_index) {
  for (size_t i = start_index; i < tokens.size(); ++i) {
    Move move = uci_to_move(tokens[i]);
    MoveList legal_moves = engine.get_legal_moves();

    bool is_legal = false;
    for (int j = 0; j < legal_moves.count; j++) {
      if (legal_moves.moves[j].to != move.to) continue;
      if (legal_moves.moves[j].from != move.from) continue;
      if (legal_moves.moves[j].promotion_piece != move.promotion_piece) continue;
      move = legal_moves.moves[j];

      is_legal = true;
      break;
    }

    if (!is_legal) break;

    engine.make_move(move);
  }
}
