#include "position.hpp"

#include "attacks.hpp"
#include "chess_types.hpp"
#include "zobrist.hpp"

#include <algorithm>
#include <sstream>

void Position::set_fen(const std::string &fen) {
  for (uint64_t &bitboard : bitboards) {
    bitboard = 0;
  }
  for (Piece &piece : lookup_table) {
    piece = NO_PIECE;
  }
  occupancy[WHITE] = 0;
  occupancy[BLACK] = 0;
  occupancy[ANY_COLOR] = 0;
  castling_rights = NO_CASTLING;
  undo_stack.clear();
  hash = 0;

  // NOTE: the board is cleared without remove_piece, so the accumulator is stale
  accumulator().generation = 0;

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

    Color color = BLACK;
    if (isupper(piece_char)) color = WHITE;

    PieceType type;
    switch (toupper(piece_char)) {
      case 'P': type = PAWN; break;
      case 'R': type = ROOK; break;
      case 'N': type = KNIGHT; break;
      case 'B': type = BISHOP; break;
      case 'Q': type = QUEEN; break;
      case 'K': type = KING; break;
      default: type = NO_TYPE; break;
    }

    if (type != NO_TYPE) add_piece(color, type, indexes_to_square(rank, file));

    file++;
  }

  // Active color
  to_move = BLACK;
  if (active_color == "w") to_move = WHITE;

  // NOTE: castling_rights was reset above, and "-" matches none of the letters
  for (char c : castling) {
    switch (c) {
      case 'K': castling_rights |= W_CASTLE_KING; break;
      case 'Q': castling_rights |= W_CASTLE_QUEEN; break;
      case 'k': castling_rights |= B_CASTLE_KING; break;
      case 'q': castling_rights |= B_CASTLE_QUEEN; break;
      default: break;
    }
  }

  // En passant square
  en_passant_square = std::nullopt;
  if (en_passant != "-")
    en_passant_square = indexes_to_square(en_passant[1] - '1', en_passant[0] - 'a');

  // Halfmove clock and Fullmove counter
  halfmove_clock = 0;
  if (!halfmove_str.empty()) halfmove_clock = std::stoi(halfmove_str);

  fullmove_counter = 1;
  if (!fullmove_str.empty()) fullmove_counter = std::stoi(fullmove_str);

  if (to_move == BLACK) hash ^= ZOBRIST_KEYS.side_to_move;
  hash ^= ZOBRIST_KEYS.castling[castling_rights];
  hash ^= en_passant_key(en_passant_square);
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

      Color color = decode_color(encoded_piece);
      PieceType type = decode_type(encoded_piece);

      char piece_char;
      switch (type) {
        case PAWN: piece_char = 'P'; break;
        case ROOK: piece_char = 'R'; break;
        case KNIGHT: piece_char = 'N'; break;
        case BISHOP: piece_char = 'B'; break;
        case QUEEN: piece_char = 'Q'; break;
        case KING: piece_char = 'K'; break;
        default: continue;
      }

      if (color == BLACK) { piece_char = tolower(piece_char); }

      fen += piece_char;
    }
    if (empty_count > 0) { fen += std::to_string(empty_count); }

    if (rank > 0) { fen += '/'; }
  }

  // Active color
  char side = 'w';
  if (to_move == BLACK) side = 'b';

  fen += ' ';
  fen += side;

  // Castling rights
  fen += ' ';
  if (castling_rights == NO_CASTLING) fen += '-';
  if (castling_rights & W_CASTLE_KING) fen += 'K';
  if (castling_rights & W_CASTLE_QUEEN) fen += 'Q';
  if (castling_rights & B_CASTLE_KING) fen += 'k';
  if (castling_rights & B_CASTLE_QUEEN) fen += 'q';

  // En passant square
  fen += ' ';
  fen += en_passant_fen();

  // Halfmove clock
  fen += ' ';
  fen += std::to_string(halfmove_clock);

  // Fullmove counter
  fen += ' ';
  fen += std::to_string(fullmove_counter);

  return fen;
}

Piece Position::get_piece_at(Square square) { return lookup_table[square]; }

void Position::make_move(const Move &move) {
  const uint64_t hash_before = hash;

  Piece encoded_piece = lookup_table[move.from()];
  Color piece_color = decode_color(encoded_piece);
  PieceType piece_type = decode_type(encoded_piece);
  Square captured_square = get_captured_square(move);

  Piece captured_piece_encoded = NO_PIECE;
  if (move.is_capture()) {
    captured_piece_encoded = lookup_table[captured_square];
    Color captured_color = decode_color(captured_piece_encoded);
    PieceType captured_type = decode_type(captured_piece_encoded);
    remove_piece(captured_color, captured_type, captured_square);
  }

  push_undo_info(move, captured_piece_encoded, hash_before);

  hash ^= ZOBRIST_KEYS.castling[castling_rights];
  hash ^= en_passant_key(en_passant_square);

  en_passant_square.reset();

  halfmove_clock++;
  if (piece_type == PAWN || move.is_capture()) halfmove_clock = 0;

  if (piece_type == PAWN && abs(square_rank(move.to()) - square_rank(move.from())) == 2) {
    int between_rank = (square_rank(move.from()) + square_rank(move.to())) / 2;
    en_passant_square = indexes_to_square(between_rank, square_file(move.to()));
  }

  // Handle castling
  update_castling_rights(move, captured_square);
  if (move.is_castling()) {
    auto [rook_from, rook_to] = castling_rook_squares(move.from(), move.to());

    uint8_t rook_encoded = lookup_table[rook_from];
    Color rook_color = decode_color(rook_encoded);
    PieceType rook_type = decode_type(rook_encoded);

    remove_piece(rook_color, rook_type, rook_from);
    add_piece(rook_color, rook_type, rook_to);
  }

  PieceType placed_type = piece_type;
  if (move.is_promotion()) placed_type = move.promotion_piece();

  remove_piece(piece_color, piece_type, move.from());
  add_piece(piece_color, placed_type, move.to());

  pass_turn();

  hash ^= ZOBRIST_KEYS.castling[castling_rights];
  hash ^= en_passant_key(en_passant_square);
}

void Position::make_null_move() {
  push_undo_info(Move{}, NO_PIECE, hash);

  // NOTE: en passant doesn't survive the opponent passing
  hash ^= en_passant_key(en_passant_square);
  en_passant_square.reset();

  halfmove_clock++;
  pass_turn();
}

void Position::undo_move() {
  if (undo_stack.empty()) return;

  UndoInfo undo_info = undo_stack.back();
  undo_stack.pop_back();

  // NOTE: a null move touched no piece
  if (undo_info.move.is_null()) {
    castling_rights = undo_info.castling_rights;
    en_passant_square = undo_info.en_passant_square;
    halfmove_clock = undo_info.halfmove_clock;
    fullmove_counter = undo_info.fullmove_counter;
    hash = undo_info.hash;

    to_move = opposite_color(to_move);
    return;
  }

  Move move = undo_info.move;
  Square from = move.from();
  Square to = move.to();
  Square captured_square = get_captured_square(move);

  uint8_t moved_piece_encoded = lookup_table[to];
  Color moved_color = decode_color(moved_piece_encoded);
  PieceType moved_type = decode_type(moved_piece_encoded);
  Color captured_color = decode_color(undo_info.captured_piece_encoded);
  PieceType captured_type = decode_type(undo_info.captured_piece_encoded);

  PieceType original_type = moved_type;
  if (move.is_promotion()) original_type = PAWN;

  remove_piece(moved_color, moved_type, to);
  add_piece(moved_color, original_type, from);

  if (captured_type != NO_TYPE) add_piece(captured_color, captured_type, captured_square);

  if (move.is_castling()) {
    auto [rook_from, rook_to] = castling_rook_squares(from, to);

    uint8_t rook_encoded = lookup_table[rook_to];
    Color rook_color = decode_color(rook_encoded);
    PieceType rook_type = decode_type(rook_encoded);

    remove_piece(rook_color, rook_type, rook_to);
    add_piece(rook_color, rook_type, rook_from);
  }

  castling_rights = undo_info.castling_rights;
  en_passant_square = undo_info.en_passant_square;
  halfmove_clock = undo_info.halfmove_clock;
  fullmove_counter = undo_info.fullmove_counter;
  hash = undo_info.hash;

  to_move = opposite_color(to_move);
}

bool Position::is_repetition() const {
  // Nothing before the last irreversible move can come back.
  const int lookback = std::min<int>(halfmove_clock, undo_stack.size());

  for (int i = 1; i <= lookback; i++) {
    if (undo_stack[undo_stack.size() - i].hash == hash) return true;
  }

  return false;
}

Square Position::get_captured_square(const Move &move) const {
  if (move.is_en_passant()) {
    int to_file = square_file(move.to());
    int from_rank = square_rank(move.from());
    return indexes_to_square(from_rank, to_file);
  }

  return move.to();
}

void Position::push_undo_info(
    const Move &move,
    Piece captured_piece_encoded,
    uint64_t hash_before
) {
  UndoInfo undo_info;
  undo_info.move = move;
  undo_info.captured_piece_encoded = captured_piece_encoded;

  undo_info.castling_rights = castling_rights;
  undo_info.en_passant_square = en_passant_square;
  undo_info.halfmove_clock = halfmove_clock;
  undo_info.fullmove_counter = fullmove_counter;
  undo_info.hash = hash_before;

  undo_stack.push_back(undo_info);
}

void Position::update_castling_rights(const Move &move, Square captured_square) {
  CastlingRights mask_from = CASTLING_MASKS[move.from()];
  CastlingRights mask_to = CASTLING_MASKS[move.to()];
  CastlingRights mask = mask_from & mask_to;

  castling_rights &= mask;

  if (move.is_capture() && captured_square != move.to()) {
    castling_rights &= CASTLING_MASKS[captured_square];
  }
}

// NOTE: the only places the board changes, so the accumulator follows every move
void Position::add_piece(Color color, PieceType piece, Square square) {
  uint64_t square_bit = square_to_bit(square);
  get_bb_ref(color, piece) |= square_bit;
  occupancy[color] |= square_bit;
  occupancy[ANY_COLOR] |= square_bit;

  const Piece encoded = encode_piece(color, piece);
  lookup_table[square] = encoded;
  hash ^= zobrist_piece_key(encoded, square);

  NNUE::Accumulator &current = accumulator();
  if (current.is_computed()) NNUE::add_feature(current, color, piece, square);
}

void Position::remove_piece(Color color, PieceType piece, Square square) {
  uint64_t square_bit = square_to_bit(square);
  get_bb_ref(color, piece) &= ~square_bit;
  occupancy[color] &= ~square_bit;
  occupancy[ANY_COLOR] &= ~square_bit;

  hash ^= zobrist_piece_key(encode_piece(color, piece), square);
  lookup_table[square] = NO_PIECE;

  NNUE::Accumulator &current = accumulator();
  if (current.is_computed()) NNUE::remove_feature(current, color, piece, square);
}

void Position::pass_turn() {
  bool black_played = to_move == BLACK;
  if (black_played) fullmove_counter++;

  to_move = opposite_color(to_move);
  hash ^= ZOBRIST_KEYS.side_to_move;
}

uint64_t Position::en_passant_key(const std::optional<Square> &square) {
  if (!square.has_value()) return 0;

  return ZOBRIST_KEYS.en_passant_file[square_file(*square)];
}

std::string Position::en_passant_fen() const {
  if (!en_passant_square.has_value()) return "-";

  std::string text;
  text += static_cast<char>('a' + square_file(*en_passant_square));
  text += static_cast<char>('1' + square_rank(*en_passant_square));

  return text;
}

std::pair<Square, Square> Position::castling_rook_squares(Square king_from, Square king_to) {
  if (king_to > king_from) {
    return {static_cast<Square>(king_from + 3 * EAST), static_cast<Square>(king_from + EAST)};
  }

  return {static_cast<Square>(king_from + 4 * WEST), static_cast<Square>(king_from + WEST)};
}
