#include "position.hpp"

#include "attacks.hpp"
#include "chess_types.hpp"
#include "zobrist.hpp"

#include <algorithm>
#include <sstream>

void Position::set_fen(const std::string &fen) {
  for (int i = 0; i < 12; i++) {
    bitboards[i] = 0;
  }
  for (int i = 0; i < 64; i++) {
    lookup_table[i] = NO_PIECE;
  }
  occupancy[WHITE] = 0;
  occupancy[BLACK] = 0;
  occupancy[ANY_COLOR] = 0;
  castling_rights = NO_CASTLING;
  undo_stack.clear();
  hash = 0;

  // The board is cleared below without going through remove_piece, so anything
  // the accumulator still holds describes the previous position. Drop it
  // before the first add_piece rather than adding the new pieces on top.
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

    Color color = isupper(piece_char) ? WHITE : BLACK;
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
  to_move = (active_color == "w") ? WHITE : BLACK;

  if (castling == "-") {
    castling_rights = NO_CASTLING;
  } else {
    for (char c : castling) {
      switch (c) {
        case 'K': castling_rights |= W_CASTLE_KING; break;
        case 'Q': castling_rights |= W_CASTLE_QUEEN; break;
        case 'k': castling_rights |= B_CASTLE_KING; break;
        case 'q': castling_rights |= B_CASTLE_QUEEN; break;
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

  // The piece keys were folded in by add_piece; the rest of the state is only
  // known now that the whole FEN has been read.
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
  fen += ' ';
  fen += (to_move == WHITE) ? 'w' : 'b';

  // Castling rights
  fen += ' ';
  if (castling_rights == NO_CASTLING) {
    fen += "-";
  } else {
    if (castling_rights & W_CASTLE_KING) fen += "K";
    if (castling_rights & W_CASTLE_QUEEN) fen += "Q";
    if (castling_rights & B_CASTLE_KING) fen += "k";
    if (castling_rights & B_CASTLE_QUEEN) fen += "q";
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

  // XOR the old castling and en passant state out; the new one goes back in
  // once the move has been applied.
  hash ^= ZOBRIST_KEYS.castling[castling_rights];
  hash ^= en_passant_key(en_passant_square);

  en_passant_square.reset();
  if (piece_type == PAWN || move.is_capture()) {
    halfmove_clock = 0;
  } else {
    halfmove_clock++;
  }

  if (piece_type == PAWN) {
    if (abs(square_rank(move.to()) - square_rank(move.from())) == 2) {
      int to_file = square_file(move.to());
      int from_rank = square_rank(move.from());
      int to_rank = square_rank(move.to());
      int en_passant_rank = (from_rank + to_rank) / 2; // Square between from and to
      en_passant_square = static_cast<Square>(indexes_to_square(en_passant_rank, to_file));
    }
  }

  // Handle castling
  update_castling_rights(move, captured_square);
  if (move.is_castling()) {
    Square rook_from, rook_to;
    if (move.to() > move.from()) {
      rook_from = static_cast<Square>(move.from() + 3 * EAST);
      rook_to = static_cast<Square>(move.from() + EAST);
    } else {
      rook_from = static_cast<Square>(move.from() + 4 * WEST);
      rook_to = static_cast<Square>(move.from() + WEST);
    }

    uint8_t rook_encoded = lookup_table[rook_from];
    Color rook_color = decode_color(rook_encoded);
    PieceType rook_type = decode_type(rook_encoded);

    remove_piece(rook_color, rook_type, rook_from);
    add_piece(rook_color, rook_type, rook_to);
  }

  remove_piece(piece_color, piece_type, move.from());
  if (move.is_promotion()) {
    add_piece(piece_color, move.promotion_piece(), move.to());
  } else {
    add_piece(piece_color, piece_type, move.to());
  }

  pass_turn();

  hash ^= ZOBRIST_KEYS.castling[castling_rights];
  hash ^= en_passant_key(en_passant_square);
}

void Position::undo_move() {
  if (undo_stack.empty()) return;

  UndoInfo undo_info = undo_stack.back();
  undo_stack.pop_back();

  Move move = undo_info.move;
  Square from = move.from();
  Square to = move.to();
  Square captured_square = get_captured_square(move);

  uint8_t moved_piece_encoded = lookup_table[to];
  Color moved_color = decode_color(moved_piece_encoded);
  PieceType moved_type = decode_type(moved_piece_encoded);
  Color captured_color = decode_color(undo_info.captured_piece_encoded);
  PieceType captured_type = decode_type(undo_info.captured_piece_encoded);

  remove_piece(moved_color, moved_type, to);

  if (move.is_promotion()) {
    add_piece(moved_color, PAWN, from);
  } else {
    add_piece(moved_color, moved_type, from);
  }

  if (captured_type != NO_TYPE) add_piece(captured_color, captured_type, captured_square);

  if (move.is_castling()) {
    int is_kingside = (to > from);
    int rook_from_offset = is_kingside * 3 + (1 - is_kingside) * -4;
    int rook_to_offset = is_kingside * 1 + (1 - is_kingside) * -1;

    Square rook_from = static_cast<Square>(from + rook_from_offset);
    Square rook_to = static_cast<Square>(from + rook_to_offset);

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

// These two are the only places the board changes, which is what makes them
// the whole of the NNUE incremental update: make_move, undo_move and set_fen
// all express castling, promotion, en passant and captures as add/remove
// pairs, so keeping the accumulator in step here keeps it in step everywhere.
//
// An uncomputed accumulator is left alone rather than started, so a perft that
// never evaluates pays one predictable branch per piece touched.
void Position::add_piece(Color color, PieceType piece, Square square) {
  uint64_t square_bit = square_to_bit(square);
  get_bb_ref(color, piece) |= square_bit;
  occupancy[color] |= square_bit;
  occupancy[ANY_COLOR] |= square_bit;

  const Piece encoded = encode_piece(color, piece);
  lookup_table[square] = encoded;
  hash ^= zobrist_piece_key(encoded, square);

  NNUE::Accumulator &acc = accumulator();
  if (acc.is_computed()) NNUE::add_feature(acc, color, piece, square);
}

void Position::remove_piece(Color color, PieceType piece, Square square) {
  uint64_t square_bit = square_to_bit(square);
  get_bb_ref(color, piece) &= ~square_bit;
  occupancy[color] &= ~square_bit;
  occupancy[ANY_COLOR] &= ~square_bit;

  hash ^= zobrist_piece_key(encode_piece(color, piece), square);
  lookup_table[square] = NO_PIECE;

  NNUE::Accumulator &acc = accumulator();
  if (acc.is_computed()) NNUE::remove_feature(acc, color, piece, square);
}

void Position::pass_turn() {
  bool black_played = to_move == BLACK;
  if (black_played) fullmove_counter++;

  to_move = opposite_color(to_move);
  hash ^= ZOBRIST_KEYS.side_to_move;
}

uint64_t Position::en_passant_key(const std::optional<Square> &square) {
  return square.has_value() ? ZOBRIST_KEYS.en_passant_file[square_file(*square)] : 0;
}
