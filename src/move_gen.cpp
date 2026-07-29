#include "move_gen.hpp"

#include "attacks.hpp"
#include "chess_types.hpp"
#include "position.hpp"

#include <cstdint>

MoveList MoveGenerator::generate_pseudo_legal_moves(const Position &position) const {
  MoveList move_list;
  Move *moves = move_list.moves;
  Move *start = moves;

  Masks masks = generate_masks(position);

  if (position.to_move == WHITE) {
    moves += generate_pawn_moves<WHITE>(position, moves, masks);
  } else {
    moves += generate_pawn_moves<BLACK>(position, moves, masks);
  }

  moves += generate_piece_moves<KNIGHT>(position, moves, masks);
  moves += generate_piece_moves<BISHOP>(position, moves, masks);
  moves += generate_piece_moves<ROOK>(position, moves, masks);
  moves += generate_piece_moves<QUEEN>(position, moves, masks);
  moves += generate_piece_moves<KING>(position, moves, masks);
  moves += generate_castling_moves(position, moves, masks);

  move_list.count = moves - start;
  return move_list;
}

MoveList MoveGenerator::generate_legal_moves(const Position &position) const {
  // The masks already keep every other move legal, so the only ones still
  // worth playing out are the en passant captures.
  return filter_en_passant_legality(position, generate_pseudo_legal_moves(position));
}

MoveList MoveGenerator::filter_en_passant_legality(
    const Position &position,
    const MoveList &pseudo
) const {
  if (!position.en_passant_square.has_value()) return pseudo;

  // En passant takes two pieces off the same rank at once, a discovered check
  // no pin ray can describe.
  Position temp_position = position;

  MoveList legal_moves;
  for (const Move &move : pseudo) {
    if (move.is_en_passant() && !is_legal_move(temp_position, move)) continue;

    legal_moves.add_move(move);
  }

  return legal_moves;
}

template<Color Us>
int MoveGenerator::generate_pawn_moves(
    const Position &position,
    Move *moves,
    const Masks &masks
) const {
  const int num_checks = count_bits(masks.checkers);
  if (num_checks >= 2) return 0; // double check, only the king may move

  Move *start = moves;
  constexpr Color Them = (Us == WHITE) ? BLACK : WHITE;
  constexpr int Forward = (Us == WHITE) ? NORTH : SOUTH;
  constexpr uint64_t StartingRank = (Us == WHITE) ? RANK_2 : RANK_7;
  constexpr uint64_t PromotionRank = (Us == WHITE) ? RANK_8 : RANK_1;

  const uint64_t our_pawns = position.get_bb(Us, PAWN);
  const uint64_t enemy_pieces = position.occupancy[Them];
  const uint64_t empty_squares = ~position.occupancy[ANY_COLOR];

  // Single pushes
  uint64_t single_pushes;
  if constexpr (Us == WHITE) {
    single_pushes = (our_pawns << NORTH) & empty_squares;
  } else {
    single_pushes = (our_pawns >> (-SOUTH)) & empty_squares;
  }

  while (single_pushes) {
    const Square to = pop_lsb_square(single_pushes);
    const Square from = static_cast<Square>(to - Forward);
    const uint64_t to_bit = square_to_bit(to);

    if (masks.pinned_pieces & square_to_bit(from) && !(to_bit & masks.pin_rays[from])) continue;
    if (num_checks == 1 && !(to_bit & masks.check_mask)) continue;

    if (to_bit & PromotionRank) {
      *moves++ = {from, to, PROMOTION, QUEEN};
      *moves++ = {from, to, PROMOTION, ROOK};
      *moves++ = {from, to, PROMOTION, BISHOP};
      *moves++ = {from, to, PROMOTION, KNIGHT};
      continue;
    }

    *moves++ = {from, to};
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
    const Square to = pop_lsb_square(double_pushes);
    const Square from = static_cast<Square>(to - 2 * Forward);
    const uint64_t to_bit = square_to_bit(to);

    if (masks.pinned_pieces & square_to_bit(from) && !(to_bit & masks.pin_rays[from])) continue;
    if (num_checks == 1 && !(to_bit & masks.check_mask)) continue;

    *moves++ = {from, to};
  }

  // Captures
  uint64_t pawns_copy = our_pawns;
  while (pawns_copy) {
    const Square from = pop_lsb_square(pawns_copy);
    uint64_t attacks = PAWN_ATTACKS[Us][from] & enemy_pieces;

    if (masks.pinned_pieces & square_to_bit(from)) attacks &= masks.pin_rays[from];
    if (num_checks == 1) attacks &= masks.check_mask | masks.checkers;

    while (attacks) {
      const Square to = pop_lsb_square(attacks);

      if ((square_to_bit(to) & PromotionRank) == 0) {
        *moves++ = {from, to, CAPTURE};
        continue;
      }

      *moves++ = {from, to, static_cast<MoveType>(CAPTURE | PROMOTION), QUEEN};
      *moves++ = {from, to, static_cast<MoveType>(CAPTURE | PROMOTION), ROOK};
      *moves++ = {from, to, static_cast<MoveType>(CAPTURE | PROMOTION), BISHOP};
      *moves++ = {from, to, static_cast<MoveType>(CAPTURE | PROMOTION), KNIGHT};
    }
  }

  // En passant captures, legality left to filter_en_passant_legality
  if (position.en_passant_square.has_value()) {
    const Square en_passant_square = position.en_passant_square.value();
    pawns_copy = our_pawns;

    while (pawns_copy) {
      const Square from = pop_lsb_square(pawns_copy);
      if (PAWN_ATTACKS[Us][from] & square_to_bit(en_passant_square)) {
        *moves++ = {from, en_passant_square, EN_PASSANT};
      }
    }
  }

  return moves - start;
}

template<PieceType PieceT>
int MoveGenerator::generate_piece_moves(
    const Position &position,
    Move *moves,
    const Masks &masks
) const {
  const int num_checks = count_bits(masks.checkers);
  if (num_checks >= 2 && PieceT != KING) return 0;

  Move *start = moves;
  const Color us = position.to_move;
  const Color them = opposite_color(us);

  uint64_t our_pieces_bb = position.get_bb(us, PieceT);
  const uint64_t our_occupancy = position.occupancy[us];
  const uint64_t enemy_occupancy = position.occupancy[them];

  while (our_pieces_bb) {
    const Square from = pop_lsb_square(our_pieces_bb);

    uint64_t attacks;
    if constexpr (PieceT == KNIGHT) {
      attacks = KNIGHT_ATTACKS[from];
    } else if constexpr (PieceT == BISHOP) {
      attacks = get_bishop_attacks(from, position.occupancy[ANY_COLOR]);
    } else if constexpr (PieceT == ROOK) {
      attacks = get_rook_attacks(from, position.occupancy[ANY_COLOR]);
    } else if constexpr (PieceT == QUEEN) {
      attacks = get_bishop_attacks(from, position.occupancy[ANY_COLOR])
                | get_rook_attacks(from, position.occupancy[ANY_COLOR]);
    } else {
      attacks = KING_ATTACKS[from] & ~masks.enemy_attacks;
    }

    attacks &= ~our_occupancy;

    if constexpr (PieceT != KING) {
      if (masks.pinned_pieces & square_to_bit(from)) attacks &= masks.pin_rays[from];
      if (num_checks == 1) attacks &= masks.check_mask | masks.checkers;
    }

    while (attacks) {
      const Square to = pop_lsb_square(attacks);
      const MoveType move_type = (square_to_bit(to) & enemy_occupancy) ? CAPTURE : NORMAL_MOVE;

      *moves++ = {from, to, move_type};
    }
  }

  return moves - start;
}

int MoveGenerator::generate_castling_moves(
    const Position &position,
    Move *moves,
    const Masks &masks
) const {
  Move *start = moves;
  const Color us = position.to_move;

  if (position.castling_rights == NO_CASTLING) return 0;
  if (masks.checkers) return 0;

  const uint64_t king_bb = position.get_bb(us, KING);
  const Square king_square = lsb_square(king_bb);

  // King-side castling
  if ((us == WHITE && (position.castling_rights & W_CASTLE_KING))
      || (us == BLACK && (position.castling_rights & B_CASTLE_KING))) {
    const uint64_t destination_bb = move_bit<EAST>(king_bb, 2);
    const uint64_t middle_bb = move_bit<EAST>(king_bb, 1);

    const uint64_t between_squares = middle_bb | destination_bb;
    const uint64_t king_path = king_bb | between_squares;

    if (!(position.occupancy[ANY_COLOR] & between_squares) && !(masks.enemy_attacks & king_path)) {
      *moves++ = {king_square, lsb_square(destination_bb), CASTLING};
    }
  }

  // Queen-side castling
  if ((us == WHITE && (position.castling_rights & W_CASTLE_QUEEN))
      || (us == BLACK && (position.castling_rights & B_CASTLE_QUEEN))) {
    const uint64_t destination_bb = move_bit<WEST>(king_bb, 2);
    const uint64_t middle_bb = move_bit<WEST>(king_bb, 1);
    const uint64_t outer_bb = move_bit<WEST>(king_bb, 3);

    const uint64_t between_squares = middle_bb | destination_bb | outer_bb;
    const uint64_t king_path = king_bb | middle_bb | destination_bb;

    if (!(position.occupancy[ANY_COLOR] & between_squares) && !(masks.enemy_attacks & king_path)) {
      *moves++ = {king_square, lsb_square(destination_bb), CASTLING};
    }
  }

  return moves - start;
}

Masks MoveGenerator::generate_masks(const Position &position) const {
  const Color us = position.to_move;
  const Color them = opposite_color(us);
  const Square king_square = find_king(position, us);

  Masks masks;

  // A king that steps away along a checking ray is still on it, so it has to
  // be transparent while the enemy attack map is built.
  const uint64_t occupancy_without_king = position.occupancy[ANY_COLOR] ^ position.get_bb(us, KING);
  masks.enemy_attacks = attack_map(position, them, occupancy_without_king);

  find_checkers(position, king_square, masks);

  // Under double check nothing but a king move is legal, so the rays would
  // never be read.
  if (count_bits(masks.checkers) >= 2) return masks;

  const uint64_t queens = position.get_bb(them, QUEEN);
  find_pins(position, king_square, position.get_bb(them, ROOK) | queens, get_rook_attacks, masks);
  find_pins(
      position,
      king_square,
      position.get_bb(them, BISHOP) | queens,
      get_bishop_attacks,
      masks
  );

  return masks;
}

uint64_t
    MoveGenerator::attack_map(const Position &position, Color color, uint64_t occupancy) const {
  uint64_t attacks = 0;

  uint64_t pawns = position.get_bb(color, PAWN);
  while (pawns) {
    attacks |= PAWN_ATTACKS[color][pop_lsb_square(pawns)];
  }

  uint64_t knights = position.get_bb(color, KNIGHT);
  while (knights) {
    attacks |= KNIGHT_ATTACKS[pop_lsb_square(knights)];
  }

  // The queen is folded into both slider loops instead of getting one of its
  // own, since it attacks exactly what the two of them do.
  uint64_t queens = position.get_bb(color, QUEEN);

  uint64_t diagonal_sliders = position.get_bb(color, BISHOP) | queens;
  while (diagonal_sliders) {
    attacks |= get_bishop_attacks(pop_lsb_square(diagonal_sliders), occupancy);
  }

  uint64_t straight_sliders = position.get_bb(color, ROOK) | queens;
  while (straight_sliders) {
    attacks |= get_rook_attacks(pop_lsb_square(straight_sliders), occupancy);
  }

  const uint64_t king = position.get_bb(color, KING);
  if (king) attacks |= KING_ATTACKS[lsb_square(king)];

  return attacks;
}

void MoveGenerator::find_checkers(
    const Position &position,
    Square king_square,
    Masks &masks
) const {
  const Color us = position.to_move;
  const Color them = opposite_color(us);
  const uint64_t all_pieces = position.occupancy[ANY_COLOR];
  const uint64_t king_bb = square_to_bit(king_square);
  const uint64_t queens = position.get_bb(them, QUEEN);

  uint64_t diagonal_checkers =
      get_bishop_attacks(king_square, all_pieces) & (position.get_bb(them, BISHOP) | queens);
  while (diagonal_checkers) {
    const Square checker = pop_lsb_square(diagonal_checkers);
    const uint64_t checker_bb = square_to_bit(checker);

    masks.checkers |= checker_bb;

    // Where the two rays meet is what lies between them, which is what an
    // interposing move has to land on.
    masks.check_mask |=
        get_bishop_attacks(king_square, checker_bb) & get_bishop_attacks(checker, king_bb);
  }

  uint64_t straight_checkers =
      get_rook_attacks(king_square, all_pieces) & (position.get_bb(them, ROOK) | queens);
  while (straight_checkers) {
    const Square checker = pop_lsb_square(straight_checkers);
    const uint64_t checker_bb = square_to_bit(checker);

    masks.checkers |= checker_bb;
    masks.check_mask |=
        get_rook_attacks(king_square, checker_bb) & get_rook_attacks(checker, king_bb);
  }

  // A pawn or a knight can only be captured, never blocked, so neither leaves
  // anything behind in the check mask.
  masks.checkers |= PAWN_ATTACKS[us][king_square] & position.get_bb(them, PAWN);
  masks.checkers |= KNIGHT_ATTACKS[king_square] & position.get_bb(them, KNIGHT);
}

void MoveGenerator::find_pins(
    const Position &position,
    Square king_square,
    uint64_t snipers,
    SliderAttacks attacks,
    Masks &masks
) const {
  const uint64_t our_occupancy = position.occupancy[position.to_move];
  const uint64_t king_bb = square_to_bit(king_square);

  // X-ray out of the king with our own pieces transparent: a sniper it reaches
  // is pinning whatever stands in the way.
  const uint64_t xray = attacks(king_square, position.occupancy[ANY_COLOR] ^ our_occupancy);

  uint64_t pinners = xray & snipers;
  while (pinners) {
    const Square pinner = pop_lsb_square(pinners);
    const uint64_t pinner_bb = square_to_bit(pinner);

    const uint64_t between = attacks(king_square, pinner_bb) & attacks(pinner, king_bb);
    const uint64_t pinned = between & our_occupancy;

    // Two of our pieces in the way and neither is pinned, either can move.
    if (count_bits(pinned) != 1) continue;

    const Square pinned_square = lsb_square(pinned);
    masks.pinned_pieces |= pinned;
    masks.pin_rays[pinned_square] = between | pinner_bb;
  }
}

bool MoveGenerator::is_square_attacked(
    const Position &position,
    Square square,
    Color enemy_color
) const {
  const uint64_t all_pieces = position.occupancy[ANY_COLOR];

  if (PAWN_ATTACKS[opposite_color(enemy_color)][square] & position.get_bb(enemy_color, PAWN)) {
    return true;
  }

  if (KNIGHT_ATTACKS[square] & position.get_bb(enemy_color, KNIGHT)) return true;

  const uint64_t diagonal_attackers =
      position.get_bb(enemy_color, BISHOP) | position.get_bb(enemy_color, QUEEN);
  if (diagonal_attackers && (get_bishop_attacks(square, all_pieces) & diagonal_attackers)) {
    return true;
  }

  const uint64_t straight_attackers =
      position.get_bb(enemy_color, ROOK) | position.get_bb(enemy_color, QUEEN);
  if (straight_attackers && (get_rook_attacks(square, all_pieces) & straight_attackers)) {
    return true;
  }

  if (KING_ATTACKS[square] & position.get_bb(enemy_color, KING)) return true;

  return false;
}

bool MoveGenerator::is_in_check(const Position &position, Color color) const {
  return is_square_attacked(position, find_king(position, color), opposite_color(color));
}

bool MoveGenerator::is_legal_move(Position &position, const Move &move) const {
  const Color side = position.to_move;

  position.make_move(move);
  const bool in_check = is_in_check(position, side);
  position.undo_move();

  return !in_check;
}

Square MoveGenerator::find_king(const Position &position, Color color) const {
  return lsb_square(position.get_bb(color, KING));
}
