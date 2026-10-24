#include "move_gen.hpp"

#include "attacks.hpp"
#include "chess_types.hpp"
#include "position.hpp"

#include <cstdint>

static constexpr int pawn_forward(Color color) {
  if (color == WHITE) return NORTH;
  return SOUTH;
}

static constexpr uint64_t pawn_starting_rank(Color color) {
  if (color == WHITE) return RANK_2;
  return RANK_7;
}

static constexpr uint64_t pawn_promotion_rank(Color color) {
  if (color == WHITE) return RANK_8;
  return RANK_1;
}

template<Color Us>
static constexpr uint64_t push_forward(uint64_t pawns) {
  if constexpr (Us == WHITE) return pawns << NORTH;

  return pawns >> -SOUTH;
}

// NOTE: where a piece may land, given the pin it is under and the check to answer
uint64_t MoveGenerator::legal_targets(Square from, const Masks &masks) {
  if (masks.pinned_pieces & square_to_bit(from)) return masks.pin_rays[from] & masks.check_mask;

  return masks.check_mask;
}

template<PieceType PieceT>
uint64_t MoveGenerator::piece_attacks(Square from, const Position &position, const Masks &masks) {
  const uint64_t occupancy = position.occupancy[ANY_COLOR];

  if constexpr (PieceT == KNIGHT) return Attacks::knight_attacks(from);
  if constexpr (PieceT == BISHOP) return Attacks::bishop_attacks(from, occupancy);
  if constexpr (PieceT == ROOK) return Attacks::rook_attacks(from, occupancy);
  if constexpr (PieceT == QUEEN) {
    return Attacks::bishop_attacks(from, occupancy) | Attacks::rook_attacks(from, occupancy);
  }

  return Attacks::king_attacks(from);
}

MoveList MoveGenerator::generate_legal_moves(const Position &position) {
  // NOTE: the masks already make every move legal except en passant
  return filter_en_passant_legality(position, generate_pseudo_legal_moves(position));
}

MoveList MoveGenerator::generate_pseudo_legal_moves(const Position &position) {
  MoveList move_list;
  const Masks masks = generate_masks(position);

  move_list.count = generate_moves(position, move_list.moves, masks, ~0ULL);
  move_list.count += generate_castling_moves(position, move_list.end(), masks);

  return move_list;
}

MoveList MoveGenerator::generate_legal_captures(const Position &position) {
  return generate_noisy(position, generate_masks(position));
}

// NOTE: captures, en passant and every promotion, which survives a captures only mask
MoveList MoveGenerator::generate_noisy(const Position &position, const Masks &masks) {
  MoveList move_list;

  const uint64_t targets = position.occupancy[opposite_color(position.to_move)];

  move_list.count = generate_moves(position, move_list.moves, masks, targets);

  return filter_en_passant_legality(position, move_list);
}

MoveList MoveGenerator::generate_quiets(const Position &position, const Masks &masks) {
  Move pseudo[MAX_POSSIBLE_LEGAL_MOVES];

  const uint64_t targets = ~position.occupancy[ANY_COLOR];

  int count = generate_moves(position, pseudo, masks, targets);
  count += generate_castling_moves(position, pseudo + count, masks);

  // NOTE: promotions and en passant slip through an empty squares mask, they are noisy
  MoveList move_list;
  for (int i = 0; i < count; i++) {
    if (!pseudo[i].is_capture() && !pseudo[i].is_promotion()) move_list.add_move(pseudo[i]);
  }

  return move_list;
}

// NOTE: a move from the transposition table may belong to another position. Generates
// only the moving piece's type aimed at the target, and looks for the move among them
bool MoveGenerator::is_legal(const Position &position, const Masks &masks, const Move &move) {
  const Piece piece = position.lookup_table[move.from()];
  if (piece == NO_PIECE || decode_color(piece) != position.to_move) return false;

  // NOTE: generate_moves stops after the king in double check, so must this
  const PieceType type = decode_type(piece);
  if (type != KING && count_bits(masks.checkers) > 1) return false;

  Move candidates[MAX_POSSIBLE_LEGAL_MOVES];
  const uint64_t target = square_to_bit(move.to());

  int count = 0;
  switch (type) {
    case PAWN:
      if (position.to_move == WHITE) {
        count = generate_pawn_moves<WHITE>(position, candidates, masks, target);
      } else {
        count = generate_pawn_moves<BLACK>(position, candidates, masks, target);
      }
      break;
    case KNIGHT: count = generate_piece_moves<KNIGHT>(position, candidates, masks, target); break;
    case BISHOP: count = generate_piece_moves<BISHOP>(position, candidates, masks, target); break;
    case ROOK: count = generate_piece_moves<ROOK>(position, candidates, masks, target); break;
    case QUEEN: count = generate_piece_moves<QUEEN>(position, candidates, masks, target); break;
    case KING:
      count = generate_piece_moves<KING>(position, candidates, masks, target);
      count += generate_castling_moves(position, candidates + count, masks);
      break;
    default: return false;
  }

  for (int i = 0; i < count; i++) {
    if (candidates[i] != move) continue;

    // NOTE: the masks make every move legal except en passant
    if (!move.is_en_passant()) return true;

    Position copy = position;
    return is_legal_move(copy, move);
  }

  return false;
}

int MoveGenerator::generate_moves(
    const Position &position,
    Move *moves,
    const Masks &masks,
    uint64_t targets
) {
  Move *start = moves;

  moves += generate_piece_moves<KING>(position, moves, masks, targets);
  if (count_bits(masks.checkers) > 1) return moves - start;

  if (position.to_move == WHITE)
    moves += generate_pawn_moves<WHITE>(position, moves, masks, targets);
  if (position.to_move == BLACK)
    moves += generate_pawn_moves<BLACK>(position, moves, masks, targets);

  moves += generate_piece_moves<KNIGHT>(position, moves, masks, targets);
  moves += generate_piece_moves<BISHOP>(position, moves, masks, targets);
  moves += generate_piece_moves<ROOK>(position, moves, masks, targets);
  moves += generate_piece_moves<QUEEN>(position, moves, masks, targets);

  return moves - start;
}

MoveList
    MoveGenerator::filter_en_passant_legality(const Position &position, const MoveList &pseudo) {
  if (!position.en_passant_square.has_value()) return pseudo;

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
    const Masks &masks,
    uint64_t targets
) {
  Move *start = moves;
  constexpr Color Them = opposite_color(Us);
  constexpr int Forward = pawn_forward(Us);
  constexpr uint64_t StartingRank = pawn_starting_rank(Us);
  constexpr uint64_t PromotionRank = pawn_promotion_rank(Us);

  const uint64_t our_pawns = position.pieces(Us, PAWN);
  const uint64_t enemy_pieces = position.occupancy[Them];
  const uint64_t empty_squares = ~position.occupancy[ANY_COLOR];

  // Single pushes
  // NOTE: promotions survive a captures only targets mask
  uint64_t single_pushes = push_forward<Us>(our_pawns) & empty_squares;
  single_pushes &= targets | PromotionRank;

  while (single_pushes) {
    const Square to = pop_lsb_square(single_pushes);
    const auto from = static_cast<Square>(to - Forward);
    const uint64_t to_bit = square_to_bit(to);

    if (!(to_bit & legal_targets(from, masks))) continue;

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
  const uint64_t single_push_from_start =
      push_forward<Us>(our_pawns & StartingRank) & empty_squares;
  uint64_t double_pushes = push_forward<Us>(single_push_from_start) & empty_squares;
  double_pushes &= targets;

  while (double_pushes) {
    const Square to = pop_lsb_square(double_pushes);
    const auto from = static_cast<Square>(to - 2 * Forward);
    if (!(square_to_bit(to) & legal_targets(from, masks))) continue;

    *moves++ = {from, to};
  }

  // Captures
  uint64_t pawns_copy = our_pawns;
  while (pawns_copy) {
    const Square from = pop_lsb_square(pawns_copy);
    uint64_t attacks = Attacks::pawn_attacks(Us, from) & enemy_pieces & targets;
    attacks &= legal_targets(from, masks);

    while (attacks) {
      const Square to = pop_lsb_square(attacks);

      if ((square_to_bit(to) & PromotionRank) == 0) {
        *moves++ = {from, to, CAPTURE};
        continue;
      }

      *moves++ = {from, to, CAPTURE_PROMOTION, QUEEN};
      *moves++ = {from, to, CAPTURE_PROMOTION, ROOK};
      *moves++ = {from, to, CAPTURE_PROMOTION, BISHOP};
      *moves++ = {from, to, CAPTURE_PROMOTION, KNIGHT};
    }
  }

  // En passant captures (ignores legalities)
  if (position.en_passant_square.has_value()) {
    const Square en_passant_square = position.en_passant_square.value();
    pawns_copy = our_pawns;

    while (pawns_copy) {
      const Square from = pop_lsb_square(pawns_copy);
      if (Attacks::pawn_attacks(Us, from) & square_to_bit(en_passant_square)) {
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
    const Masks &masks,
    uint64_t targets
) {
  Move *start = moves;
  const Color us = position.to_move;
  const Color them = opposite_color(us);

  uint64_t our_pieces_bb = position.pieces(us, PieceT);
  const uint64_t our_occupancy = position.occupancy[us];
  const uint64_t enemy_occupancy = position.occupancy[them];

  while (our_pieces_bb) {
    const Square from = pop_lsb_square(our_pieces_bb);

    uint64_t attacks = piece_attacks<PieceT>(from, position, masks);
    attacks &= ~our_occupancy & targets;

    if constexpr (PieceT != KING) attacks &= legal_targets(from, masks);
    if constexpr (PieceT == KING) attacks = safe_king_squares(position, attacks);

    while (attacks) {
      const Square to = pop_lsb_square(attacks);
      MoveType move_type = NORMAL_MOVE;
      if (square_to_bit(to) & enemy_occupancy) move_type = CAPTURE;

      *moves++ = {from, to, move_type};
    }
  }

  return moves - start;
}

int MoveGenerator::generate_castling_moves(
    const Position &position,
    Move *moves,
    const Masks &masks
) {
  Move *start = moves;
  const Color us = position.to_move;

  if (position.castling_rights == NO_CASTLING) return 0;
  if (masks.checkers) return 0;

  const uint64_t king_bb = position.pieces(us, KING);
  const Square king_square = lsb_square(king_bb);

  // King-side castling
  if ((us == WHITE && (position.castling_rights & W_CASTLE_KING))
      || (us == BLACK && (position.castling_rights & B_CASTLE_KING))) {
    const uint64_t destination_bb = move_bit<EAST>(king_bb, 2);
    const uint64_t middle_bb = move_bit<EAST>(king_bb, 1);

    const uint64_t between_squares = middle_bb | destination_bb;
    const uint64_t king_path = king_bb | between_squares;

    if (!(position.occupancy[ANY_COLOR] & between_squares)
        && safe_king_squares(position, king_path) == king_path) {
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

    if (!(position.occupancy[ANY_COLOR] & between_squares)
        && safe_king_squares(position, king_path) == king_path) {
      *moves++ = {king_square, lsb_square(destination_bb), CASTLING};
    }
  }

  return moves - start;
}

MoveGenerator::Masks MoveGenerator::generate_masks(const Position &position) {
  const Color us = position.to_move;
  const Color them = opposite_color(us);
  const Square king_square = find_king(position, us);

  Masks masks;

  find_checkers(position, king_square, masks);
  if (count_bits(masks.checkers) >= 2) return masks;

  masks.check_mask |= masks.checkers;
  if (!masks.checkers) masks.check_mask = ~0ULL;

  const uint64_t queens = position.pieces(them, QUEEN);

  find_pins(
      position,
      king_square,
      position.pieces(them, ROOK) | queens,
      Attacks::rook_attacks,
      masks
  );
  find_pins(
      position,
      king_square,
      position.pieces(them, BISHOP) | queens,
      Attacks::bishop_attacks,
      masks
  );

  return masks;
}

void MoveGenerator::find_checkers(const Position &position, Square king_square, Masks &masks) {
  const Color us = position.to_move;
  const Color them = opposite_color(us);
  const uint64_t all_pieces = position.occupancy[ANY_COLOR];
  const uint64_t king_bb = square_to_bit(king_square);
  const uint64_t queens = position.pieces(them, QUEEN);

  uint64_t diagonal_attackers = position.pieces(them, BISHOP) | queens;
  uint64_t diagonal_checkers =
      Attacks::bishop_attacks(king_square, all_pieces) & diagonal_attackers;

  while (diagonal_checkers) {
    const Square checker = pop_lsb_square(diagonal_checkers);
    const uint64_t checker_bb = square_to_bit(checker);

    const uint64_t between = Attacks::bishop_attacks(king_square, checker_bb)
                             & Attacks::bishop_attacks(checker, king_bb);

    masks.checkers |= checker_bb;
    masks.check_mask |= between;
  }

  uint64_t straight_attackers = position.pieces(them, ROOK) | queens;
  uint64_t straight_checkers = Attacks::rook_attacks(king_square, all_pieces) & straight_attackers;

  while (straight_checkers) {
    const Square checker = pop_lsb_square(straight_checkers);
    const uint64_t checker_bb = square_to_bit(checker);

    const uint64_t between =
        Attacks::rook_attacks(king_square, checker_bb) & Attacks::rook_attacks(checker, king_bb);

    masks.checkers |= checker_bb;
    masks.check_mask |= between;
  }

  // NOTE: pawn and knight checks can't be blocked
  masks.checkers |= Attacks::pawn_attacks(us, king_square) & position.pieces(them, PAWN);
  masks.checkers |= Attacks::knight_attacks(king_square) & position.pieces(them, KNIGHT);
}

void MoveGenerator::find_pins(
    const Position &position,
    Square king_square,
    uint64_t snipers,
    SliderAttacks attacks,
    Masks &masks
) {
  const uint64_t our_occupancy = position.occupancy[position.to_move];
  const uint64_t king_bb = square_to_bit(king_square);

  const uint64_t xray = attacks(king_square, position.occupancy[ANY_COLOR] ^ our_occupancy);

  uint64_t pinners = xray & snipers;
  while (pinners) {
    const Square pinner = pop_lsb_square(pinners);
    const uint64_t pinner_bb = square_to_bit(pinner);

    const uint64_t between = attacks(king_square, pinner_bb) & attacks(pinner, king_bb);
    const uint64_t pinned = between & our_occupancy;

    if (count_bits(pinned) != 1) continue;

    const Square pinned_square = lsb_square(pinned);
    masks.pinned_pieces |= pinned;
    masks.pin_rays[pinned_square] = between | pinner_bb;
  }
}

// NOTE: the king only ever asks about the few squares it could step to, so it checks
// those instead of every node building the whole enemy attack map
uint64_t MoveGenerator::safe_king_squares(const Position &position, uint64_t squares) {
  const Color us = position.to_move;
  const Color them = opposite_color(us);

  // NOTE: otherwise the king could step back along a checking ray
  const uint64_t occupancy_without_king = position.occupancy[ANY_COLOR] ^ position.pieces(us, KING);

  uint64_t safe = 0;
  while (squares) {
    const Square square = pop_lsb_square(squares);
    if (!is_square_attacked(position, square, them, occupancy_without_king)) {
      safe |= square_to_bit(square);
    }
  }

  return safe;
}

bool MoveGenerator::is_square_attacked(const Position &position, Square square, Color enemy_color) {
  return is_square_attacked(position, square, enemy_color, position.occupancy[ANY_COLOR]);
}

bool MoveGenerator::is_square_attacked(
    const Position &position,
    Square square,
    Color enemy_color,
    uint64_t all_pieces
) {

  if (Attacks::pawn_attacks(opposite_color(enemy_color), square)
      & position.pieces(enemy_color, PAWN)) {
    return true;
  }

  if (Attacks::knight_attacks(square) & position.pieces(enemy_color, KNIGHT)) return true;

  const uint64_t diagonal_attackers =
      position.pieces(enemy_color, BISHOP) | position.pieces(enemy_color, QUEEN);
  if (diagonal_attackers && (Attacks::bishop_attacks(square, all_pieces) & diagonal_attackers)) {
    return true;
  }

  const uint64_t straight_attackers =
      position.pieces(enemy_color, ROOK) | position.pieces(enemy_color, QUEEN);
  if (straight_attackers && (Attacks::rook_attacks(square, all_pieces) & straight_attackers)) {
    return true;
  }

  if (Attacks::king_attacks(square) & position.pieces(enemy_color, KING)) return true;

  return false;
}

bool MoveGenerator::is_in_check(const Position &position, Color color) {
  return is_square_attacked(position, find_king(position, color), opposite_color(color));
}

bool MoveGenerator::is_legal_move(Position &position, const Move &move) {
  const Color side = position.to_move;

  position.make_move(move);
  const bool in_check = is_in_check(position, side);
  position.undo_move();

  return !in_check;
}

Square MoveGenerator::find_king(const Position &position, Color color) {
  return lsb_square(position.pieces(color, KING));
}
