#include "uci_utils.hpp"

#include <cctype>
#include <sstream>
#include <stdexcept>

std::vector<std::string> split_string(const std::string &str) {
  std::vector<std::string> tokens;
  std::istringstream iss(str);
  std::string token;

  while (iss >> token) {
    tokens.push_back(token);
  }

  return tokens;
}

std::string square_to_uci(Square square) {
  int file = square % 8;
  int rank = square / 8;

  std::string result;
  result += ('a' + file);
  result += ('1' + rank);

  return result;
}

Square uci_to_square(const std::string &uci) {
  if (uci.length() < 2) { throw std::invalid_argument("Invalid UCI square notation"); }

  char file_char = std::tolower(uci[0]);
  char rank_char = uci[1];

  if (file_char < 'a' || file_char > 'h' || rank_char < '1' || rank_char > '8') {
    throw std::invalid_argument("Invalid UCI square notation");
  }

  int file = file_char - 'a';
  int rank = rank_char - '1';

  return static_cast<Square>(rank * 8 + file);
}

std::string move_to_uci(const Move &move) {
  std::string result = square_to_uci(move.from) + square_to_uci(move.to);

  if (move.is_promotion()) { result += piece_type_to_char(move.promotion_piece); }

  return result;
}

char piece_type_to_char(PieceType type) {
  switch (type) {
    case QUEEN: return 'q';
    case ROOK: return 'r';
    case BISHOP: return 'b';
    case KNIGHT: return 'n';
    default: return 'q';
  }
}

PieceType char_to_piece_type(char c) {
  switch (std::tolower(c)) {
    case 'q': return QUEEN;
    case 'r': return ROOK;
    case 'b': return BISHOP;
    case 'n': return KNIGHT;
    default: return QUEEN;
  }
}

Move uci_to_move(const std::string &uci) {
  if (uci.length() < 4) { throw std::invalid_argument("Invalid UCI move notation: too short"); }

  Square from = uci_to_square(uci.substr(0, 2));
  Square to = uci_to_square(uci.substr(2, 2));

  Move move;
  move.from = from;
  move.to = to;
  move.type = NORMAL_MOVE;
  move.promotion_piece = NO_TYPE;

  // Check for promotion
  if (uci.length() == 5) {
    move.type = static_cast<MoveType>(move.type | PROMOTION);
    move.promotion_piece = char_to_piece_type(uci[4]);
  }

  return move;
}
