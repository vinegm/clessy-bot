#include "move_gen.hpp"

#include "chess_types.hpp"

#include <cstdint>

MoveList MoveGenerator::generate_pseudo_legal_moves(const Position &position) const {
  MoveList move_list;
  Move *moves = move_list.moves;
  Move *start = moves;

  if (position.to_move == WHITE) {
    moves += generate_pawn_moves<WHITE>(position, moves);
  } else {
    moves += generate_pawn_moves<BLACK>(position, moves);
  }

  moves += generate_piece_moves<PIECE_KNIGHT>(position, moves);
  moves += generate_piece_moves<PIECE_BISHOP>(position, moves);
  moves += generate_piece_moves<PIECE_ROOK>(position, moves);
  moves += generate_piece_moves<PIECE_QUEEN>(position, moves);
  moves += generate_piece_moves<PIECE_KING>(position, moves);
  moves += generate_castling_moves(position, moves);

  move_list.count = moves - start;
  return move_list;
}

MoveList MoveGenerator::generate_legal_moves(const Position &position) const {
  MoveList pseudo_legal = generate_pseudo_legal_moves(position);

  MoveList legal_moves;
  for (int i = 0; i < pseudo_legal.count; i++) {
    if (is_legal_move(position, pseudo_legal[i])) { legal_moves.add_move(pseudo_legal[i]); }
  }

  return legal_moves;
}

template<PieceColor Us>
int MoveGenerator::generate_pawn_moves(const Position &position, Move *moves) const {
  Move *start = moves;
  constexpr PieceColor Them = (Us == WHITE) ? BLACK : WHITE;
  constexpr int Forward = (Us == WHITE) ? NORTH : SOUTH;
  constexpr uint64_t StartingRank = (Us == WHITE) ? RANK_2 : RANK_7;
  constexpr uint64_t PromotionRank = (Us == WHITE) ? RANK_8 : RANK_1;

  const uint64_t our_pawns = position.bitboards[bitboard_index(Us, PIECE_PAWN)];
  const uint64_t enemy_pieces = position.occupancy[Them];
  const uint64_t empty_squares = ~position.occupancy[ANY];

  // Single pushes
  uint64_t single_pushes;
  if constexpr (Us == WHITE) {
    single_pushes = (our_pawns << NORTH) & empty_squares;
  } else {
    single_pushes = (our_pawns >> (-SOUTH)) & empty_squares;
  }

  while (single_pushes) {
    const Square to = static_cast<Square>(pop_lsb(single_pushes));
    const Square from = static_cast<Square>(to - Forward);

    if (square_to_bit(to) & PromotionRank) {
      *moves++ = {from, to, PROMOTION, PIECE_QUEEN};
      *moves++ = {from, to, PROMOTION, PIECE_ROOK};
      *moves++ = {from, to, PROMOTION, PIECE_BISHOP};
      *moves++ = {from, to, PROMOTION, PIECE_KNIGHT};
    } else {
      *moves++ = {from, to};
    }
  }

  // Double pushes
  uint64_t double_pushes;
  if constexpr (Us == WHITE) {
    const uint64_t single_push_from_start = ((our_pawns & StartingRank) << NORTH) & empty_squares;
    double_pushes = (single_push_from_start << NORTH) & empty_squares;
  } else {
    const uint64_t single_push_from_start =
        ((our_pawns & StartingRank) >> (-SOUTH)) & empty_squares;
    double_pushes = (single_push_from_start >> (-SOUTH)) & empty_squares;
  }

  while (double_pushes) {
    const Square to = static_cast<Square>(pop_lsb(double_pushes));
    const Square from = static_cast<Square>(to - 2 * Forward);
    *moves++ = {from, to};
  }

  // Regular captures
  uint64_t pawns_copy = our_pawns;
  while (pawns_copy) {
    const Square from = static_cast<Square>(pop_lsb(pawns_copy));
    uint64_t attacks = PAWN_ATTACKS[Us][from] & enemy_pieces;

    while (attacks) {
      const Square to = static_cast<Square>(pop_lsb(attacks));

      if (square_to_bit(to) & PromotionRank) {
        *moves++ = {from, to, static_cast<MoveType>(CAPTURE | PROMOTION), PIECE_QUEEN};
        *moves++ = {from, to, static_cast<MoveType>(CAPTURE | PROMOTION), PIECE_ROOK};
        *moves++ = {from, to, static_cast<MoveType>(CAPTURE | PROMOTION), PIECE_BISHOP};
        *moves++ = {from, to, static_cast<MoveType>(CAPTURE | PROMOTION), PIECE_KNIGHT};
      } else {
        *moves++ = {from, to, CAPTURE};
      }
    }
  }

  // En passant captures
  if (position.en_passant_square.has_value()) {
    const Square en_passant_square = position.en_passant_square.value();
    pawns_copy = our_pawns;

    while (pawns_copy) {
      const Square from = static_cast<Square>(pop_lsb(pawns_copy));
      if (PAWN_ATTACKS[Us][from] & square_to_bit(en_passant_square)) {
        *moves++ = {from, en_passant_square, EN_PASSANT};
      }
    }
  }

  return moves - start;
}

template<PieceType PieceT>
int MoveGenerator::generate_piece_moves(const Position &position, Move *moves) const {
  Move *start = moves;
  const PieceColor us = position.to_move;
  const PieceColor them = opposite_color(us);

  uint64_t our_pieces_bb = position.bitboards[bitboard_index(us, PieceT)];
  const uint64_t our_occupancy = position.occupancy[us];
  const uint64_t enemy_occupancy = position.occupancy[them];

  while (our_pieces_bb) {
    const Square from = static_cast<Square>(pop_lsb(our_pieces_bb));
    uint64_t attacks;

    if constexpr (PieceT == PIECE_KNIGHT) {
      attacks = KNIGHT_ATTACKS[from];
    } else if constexpr (PieceT == PIECE_BISHOP) {
      attacks = get_bishop_attacks(from, position.occupancy[ANY]);
    } else if constexpr (PieceT == PIECE_ROOK) {
      attacks = get_rook_attacks(from, position.occupancy[ANY]);
    } else if constexpr (PieceT == PIECE_QUEEN) {
      attacks = get_rook_attacks(from, position.occupancy[ANY])
                | get_bishop_attacks(from, position.occupancy[ANY]);
    } else if constexpr (PieceT == PIECE_KING) {
      attacks = KING_ATTACKS[from];
    }

    attacks &= ~our_occupancy;

    while (attacks) {
      const Square to = static_cast<Square>(pop_lsb(attacks));
      const MoveType move_type = (square_to_bit(to) & enemy_occupancy) ? CAPTURE : NORMAL_MOVE;

      *moves++ = {from, to, move_type};
    }
  }

  return moves - start;
}

int MoveGenerator::generate_castling_moves(const Position &position, Move *moves) const {
  Move *start = moves;
  const PieceColor us = position.to_move;
  const PieceColor them = opposite_color(us);

  if (position.castling_rights == 0) return 0;

  const Square king_square = find_king(position, us);

  // King-side castling
  if ((us == WHITE && (position.castling_rights & WHITE_CASTLE_KING))
      || (us == BLACK && (position.castling_rights & BLACK_CASTLE_KING))) {
    const Square king_dest = static_cast<Square>(king_square + 2 * EAST);
    const Square middle_square = static_cast<Square>(king_square + EAST);

    if (!(position.occupancy[ANY] & (square_to_bit(middle_square) | square_to_bit(king_dest)))
        && !is_square_attacked(position, king_square, them)
        && !is_square_attacked(position, middle_square, them)
        && !is_square_attacked(position, king_dest, them)) {
      *moves++ = {king_square, king_dest, CASTLING};
    }
  }

  // Queen-side castling
  if ((us == WHITE && (position.castling_rights & WHITE_CASTLE_QUEEN))
      || (us == BLACK && (position.castling_rights & BLACK_CASTLE_QUEEN))) {
    const Square king_dest = static_cast<Square>(king_square + 2 * WEST);
    const Square middle_square = static_cast<Square>(king_square + WEST);
    const Square outer_square = static_cast<Square>(king_square + 3 * WEST);

    if (!(position.occupancy[ANY]
          & (square_to_bit(middle_square) | square_to_bit(king_dest) | square_to_bit(outer_square)))
        && !is_square_attacked(position, king_square, them)
        && !is_square_attacked(position, middle_square, them)
        && !is_square_attacked(position, king_dest, them)) {
      *moves++ = {king_square, king_dest, CASTLING};
    }
  }

  return moves - start;
}

bool MoveGenerator::is_square_attacked(
    const Position &position,
    Square square,
    PieceColor enemy_color
) const {
  const uint64_t all_pieces = position.occupancy[ANY];

  if (PAWN_ATTACKS[opposite_color(enemy_color)][square]
      & position.bitboards[bitboard_index(enemy_color, PIECE_PAWN)]) {
    return true;
  }

  if (KNIGHT_ATTACKS[square] & position.bitboards[bitboard_index(enemy_color, PIECE_KNIGHT)]) {
    return true;
  }

  const uint64_t diagonal_attackers =
      position.bitboards[bitboard_index(enemy_color, PIECE_BISHOP)]
      | position.bitboards[bitboard_index(enemy_color, PIECE_QUEEN)];
  if (diagonal_attackers && (get_bishop_attacks(square, all_pieces) & diagonal_attackers)) {
    return true;
  }

  const uint64_t straight_attackers =
      position.bitboards[bitboard_index(enemy_color, PIECE_ROOK)]
      | position.bitboards[bitboard_index(enemy_color, PIECE_QUEEN)];
  if (straight_attackers && (get_rook_attacks(square, all_pieces) & straight_attackers)) {
    return true;
  }

  if (KING_ATTACKS[square] & position.bitboards[bitboard_index(enemy_color, PIECE_KING)]) {
    return true;
  }

  return false;
}

bool MoveGenerator::is_in_check(const Position &position, PieceColor color) const {
  Square king_square = find_king(position, color);
  return is_square_attacked(position, king_square, opposite_color(color));
}

bool MoveGenerator::is_legal_move(const Position &position, const Move &move) const {
  Position temp_position = position;
  temp_position.make_move(move);

  if (is_in_check(temp_position, position.to_move)) return false;

  return true;
}

Square MoveGenerator::find_king(const Position &position, PieceColor color) const {
  const uint64_t king = position.bitboards[bitboard_index(color, PIECE_KING)];
  return static_cast<Square>(lsb_index(king));
}

template int MoveGenerator::generate_pawn_moves<WHITE>(const Position &position, Move *moves) const;
template int MoveGenerator::generate_pawn_moves<BLACK>(const Position &position, Move *moves) const;

template int
    MoveGenerator::generate_piece_moves<PIECE_KNIGHT>(const Position &position, Move *moves) const;
template int
    MoveGenerator::generate_piece_moves<PIECE_BISHOP>(const Position &position, Move *moves) const;
template int
    MoveGenerator::generate_piece_moves<PIECE_ROOK>(const Position &position, Move *moves) const;
template int
    MoveGenerator::generate_piece_moves<PIECE_QUEEN>(const Position &position, Move *moves) const;
template int
    MoveGenerator::generate_piece_moves<PIECE_KING>(const Position &position, Move *moves) const;
