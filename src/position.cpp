#include "position.hpp"

#include "chess_types.hpp"

#include <sstream>

Position::Position(const std::string &fen) : bitboards{}, lookup_table{} {
  std::istringstream fen_stream(fen);
  std::string piece_placement, active_color, castling, en_passant, halfmove_str, fullmove_str;

  fen_stream >> piece_placement >> active_color >> castling >> en_passant >> halfmove_str
      >> fullmove_str;

  // Piece placement
  int rank = 7, file = 0;
  for (char piece_char : piece_placement) {
    if (piece_char == '/') {
      rank--;
      file = 0;
      continue;
    }

    if (isdigit(piece_char)) {
      file += piece_char - '0';
      continue;
    }

    PieceColor color = isupper(piece_char) ? WHITE : BLACK;
    PieceType type;

    switch (toupper(piece_char)) {
      case 'P': type = PIECE_PAWN; break;
      case 'R': type = PIECE_ROOK; break;
      case 'N': type = PIECE_KNIGHT; break;
      case 'B': type = PIECE_BISHOP; break;
      case 'Q': type = PIECE_QUEEN; break;
      case 'K': type = PIECE_KING; break;
      default: type = PIECE_NONE; break;
    }

    if (type != PIECE_NONE) add_piece(color, type, indexes_to_square(rank, file));

    file++;
  }

  // Active color
  to_move = (active_color == "w") ? WHITE : BLACK;

  if (castling == "-") {
    castling_rights = 0;
  } else {
    for (char c : castling) {
      switch (c) {
        case 'K': castling_rights |= WHITE_CASTLE_KING; break;
        case 'Q': castling_rights |= WHITE_CASTLE_QUEEN; break;
        case 'k': castling_rights |= BLACK_CASTLE_KING; break;
        case 'q': castling_rights |= BLACK_CASTLE_QUEEN; break;
      }
    }
  }

  // En passant square
  if (en_passant == "-") {
    en_passant_square = std::nullopt;
  } else {
    int file_idx = en_passant[0] - 'a';
    int rank_idx = en_passant[1] - '1'; // -'1' for 0-based index
    en_passant_square = indexes_to_square(rank_idx, file_idx);
  }

  // Halfmove clock and Fullmove counter
  halfmove_clock = halfmove_str.empty() ? 0 : std::stoi(halfmove_str);
  fullmove_counter = fullmove_str.empty() ? 1 : std::stoi(fullmove_str);
}

std::string Position::get_fen() const {
  std::string fen;

  // Piece placement
  for (int rank = 7; rank >= 0; rank--) {
    int empty_count = 0;

    for (int file = 0; file < 8; file++) {
      Square square = indexes_to_square(rank, file);
      uint8_t encoded_piece = lookup_table[square];

      if (encoded_piece == 0) {
        empty_count++;
        continue;
      }

      if (empty_count > 0) {
        fen += std::to_string(empty_count);
        empty_count = 0;
      }

      PieceColor color = decode_color(encoded_piece);
      PieceType type = decode_type(encoded_piece);

      char piece_char;
      switch (type) {
        case PIECE_PAWN: piece_char = 'P'; break;
        case PIECE_ROOK: piece_char = 'R'; break;
        case PIECE_KNIGHT: piece_char = 'N'; break;
        case PIECE_BISHOP: piece_char = 'B'; break;
        case PIECE_QUEEN: piece_char = 'Q'; break;
        case PIECE_KING: piece_char = 'K'; break;
        default: continue;
      }

      if (color == BLACK) { piece_char = tolower(piece_char); }

      fen += piece_char;
    }
    if (empty_count > 0) { fen += std::to_string(empty_count); }

    if (rank > 0) { fen += '/'; }
  }

  // Active color
  fen += ' ';
  fen += (to_move == WHITE) ? 'w' : 'b';

  // Castling rights
  fen += ' ';
  if (castling_rights == 0) {
    fen += "-";
  } else {
    if (castling_rights & WHITE_CASTLE_KING) fen += "K";
    if (castling_rights & WHITE_CASTLE_QUEEN) fen += "Q";
    if (castling_rights & BLACK_CASTLE_KING) fen += "k";
    if (castling_rights & BLACK_CASTLE_QUEEN) fen += "q";
  }

  // En passant square
  fen += ' ';
  if (en_passant_square.has_value()) {
    Square ep_square = en_passant_square.value();
    int file = ep_square % 8;
    int rank = ep_square / 8;
    fen += static_cast<char>('a' + file);
    fen += static_cast<char>('1' + rank);
  } else {
    fen += '-';
  }

  // Halfmove clock
  fen += ' ';
  fen += std::to_string(halfmove_clock);

  // Fullmove counter
  fen += ' ';
  fen += std::to_string(fullmove_counter);

  return fen;
}

Piece Position::get_piece_at(Square square) {
  uint8_t encoded_piece = lookup_table[square];
  if (encoded_piece == 0) return Piece{WHITE, PIECE_NONE};

  PieceColor color = decode_color(encoded_piece);
  PieceType type = decode_type(encoded_piece);
  return Piece{color, type};
}

void Position::make_move(const Move &move) {
  uint8_t encoded_piece = lookup_table[move.from];
  Piece piece = decode_piece(encoded_piece);
  Square captured_square = get_captured_square(move);

  uint8_t captured_piece_encoded = 0;
  if (move.is_capture()) {
    captured_piece_encoded = lookup_table[captured_square];
    Piece captured_piece = decode_piece(captured_piece_encoded);
    remove_piece(captured_piece.color, captured_piece.type, captured_square);
  }

  push_undo_info(move, captured_piece_encoded);

  if (castling_rights != 0) update_castling_rights(move, piece, captured_square);

  en_passant_square.reset();
  if (piece.type == PIECE_PAWN || move.is_capture()) {
    halfmove_clock = 0;
  } else {
    halfmove_clock++;
  }

  if (piece.type == PIECE_PAWN) {
    if (abs(square_rank(move.to) - square_rank(move.from)) == 2) {
      int to_file = square_file(move.to);
      int from_rank = square_rank(move.from);
      int to_rank = square_rank(move.to);
      int en_passant_rank = (from_rank + to_rank) / 2; // Square between from and to
      en_passant_square = static_cast<Square>(indexes_to_square(en_passant_rank, to_file));
    }
  }

  // Handle castling
  if (move.is_castling()) {
    Square rook_from, rook_to;
    if (move.to > move.from) {
      rook_from = static_cast<Square>(move.from + 3 * EAST);
      rook_to = static_cast<Square>(move.from + EAST);
    } else {
      rook_from = static_cast<Square>(move.from + 4 * WEST);
      rook_to = static_cast<Square>(move.from + WEST);
    }

    uint8_t rook_encoded = lookup_table[rook_from];
    Piece rook = decode_piece(rook_encoded);

    remove_piece(rook.color, rook.type, rook_from);
    add_piece(rook.color, rook.type, rook_to);
  }

  remove_piece(piece.color, piece.type, move.from);
  if (move.is_promotion()) {
    add_piece(piece.color, move.promotion_piece, move.to);
  } else {
    add_piece(piece.color, piece.type, move.to);
  }

  pass_turn();
}

void Position::undo_move() {
  if (undo_stack.empty()) return;

  UndoInfo undo_info = undo_stack.back();
  undo_stack.pop_back();

  Move move = undo_info.move;
  Square from = move.from;
  Square to = move.to;
  Square captured_square = get_captured_square(move);

  uint8_t moved_piece_encoded = lookup_table[to];
  Piece moved_piece = decode_piece(moved_piece_encoded);
  Piece captured_piece = decode_piece(undo_info.captured_piece_encoded);

  remove_piece(moved_piece.color, moved_piece.type, to);

  if (move.is_promotion()) {
    add_piece(moved_piece.color, PIECE_PAWN, from);
  } else {
    add_piece(moved_piece.color, moved_piece.type, from);
  }

  if (captured_piece.type != PIECE_NONE)
    add_piece(captured_piece.color, captured_piece.type, captured_square);

  if (move.is_castling()) {
    Square rook_from, rook_to;
    if (to > from) {
      rook_from = static_cast<Square>(from + 3 * EAST);
      rook_to = static_cast<Square>(from + EAST);
    } else {
      rook_from = static_cast<Square>(from + 4 * WEST);
      rook_to = static_cast<Square>(from + WEST);
    }

    uint8_t rook_encoded = lookup_table[rook_to];
    Piece rook = decode_piece(rook_encoded);

    remove_piece(rook.color, rook.type, rook_to);
    add_piece(rook.color, rook.type, rook_from);
  }

  castling_rights = undo_info.castling_rights;
  en_passant_square = undo_info.en_passant_square;
  halfmove_clock = undo_info.halfmove_clock;
  fullmove_counter = undo_info.fullmove_counter;

  to_move = opposite_color(to_move);
}

Square Position::get_captured_square(const Move &move) const {
  if (move.is_en_passant()) {
    int to_file = square_file(move.to);
    int from_rank = square_rank(move.from);
    return indexes_to_square(from_rank, to_file);
  }

  return move.to;
}

void Position::push_undo_info(const Move &move, uint8_t captured_piece_encoded) {
  UndoInfo undo_info;
  undo_info.move = move;
  undo_info.captured_piece_encoded = captured_piece_encoded;

  undo_info.castling_rights = castling_rights;
  undo_info.en_passant_square = en_passant_square;
  undo_info.halfmove_clock = halfmove_clock;
  undo_info.fullmove_counter = fullmove_counter;

  undo_stack.push_back(undo_info);
}

void Position::update_castling_rights(
    const Move &move,
    const Piece &piece,
    Square captured_square
) {
  if (piece.type == PIECE_KING) {
    if (piece.color == WHITE) {
      castling_rights &= ~(WHITE_CASTLE_KING | WHITE_CASTLE_QUEEN);
    } else {
      castling_rights &= ~(BLACK_CASTLE_KING | BLACK_CASTLE_QUEEN);
    }
    return;
  }

  if (piece.type == PIECE_ROOK) {
    if (piece.color == WHITE) {
      if (move.from == H1) {
        castling_rights &= ~WHITE_CASTLE_KING;
      } else if (move.from == A1) {
        castling_rights &= ~WHITE_CASTLE_QUEEN;
      }
    } else {
      if (move.from == H8) {
        castling_rights &= ~BLACK_CASTLE_KING;
      } else if (move.from == A8) {
        castling_rights &= ~BLACK_CASTLE_QUEEN;
      }
    }
  }

  if (move.is_capture()) {
    if (captured_square == H1) {
      castling_rights &= ~WHITE_CASTLE_KING;
    } else if (captured_square == A1) {
      castling_rights &= ~WHITE_CASTLE_QUEEN;
    } else if (captured_square == H8) {
      castling_rights &= ~BLACK_CASTLE_KING;
    } else if (captured_square == A8) {
      castling_rights &= ~BLACK_CASTLE_QUEEN;
    }
  }
}

void Position::add_piece(PieceColor color, PieceType piece, Square square) {
  uint64_t square_bit = square_to_bit(square);
  bitboards[bitboard_index(color, piece)] |= square_bit;
  occupancy[color] |= square_bit;
  occupancy[ANY] |= square_bit;

  lookup_table[square] = encode_piece(color, piece);
}

void Position::remove_piece(PieceColor color, PieceType piece, Square square) {
  uint64_t square_bit = square_to_bit(square);
  bitboards[bitboard_index(color, piece)] &= ~square_bit;
  occupancy[color] &= ~square_bit;
  occupancy[ANY] &= ~square_bit;

  lookup_table[square] = 0;
}

void Position::pass_turn() {
  bool black_played = to_move == BLACK;
  if (black_played) fullmove_counter++;

  to_move = opposite_color(to_move);
}
