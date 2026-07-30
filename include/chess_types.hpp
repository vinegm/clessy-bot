#pragma once

#include <cstdint>

#define RANK_1 0xFFULL
#define RANK_2 (RANK_1 << (8 * 1))
#define RANK_3 (RANK_1 << (8 * 2))
#define RANK_4 (RANK_1 << (8 * 3))
#define RANK_5 (RANK_1 << (8 * 4))
#define RANK_6 (RANK_1 << (8 * 5))
#define RANK_7 (RANK_1 << (8 * 6))
#define RANK_8 (RANK_1 << (8 * 7))

#define FILE_A 0x0101010101010101ULL
#define FILE_B (FILE_A << 1)
#define FILE_C (FILE_A << 2)
#define FILE_D (FILE_A << 3)
#define FILE_E (FILE_A << 4)
#define FILE_F (FILE_A << 5)
#define FILE_G (FILE_A << 6)
#define FILE_H (FILE_A << 7)

#define MAX_POSSIBLE_LEGAL_MOVES 256
#define INITIAL_POSITION_FEN "rnbqkbnr/pppppppp/8/8/8/8/PPPPPPPP/RNBQKBNR w KQkq - 0 1"

// clang-format off
// Little-endian rank-file
enum Square : uint8_t {
  A8 = 7 * 8, B8, C8, D8, E8, F8, G8, H8,
  A7 = 6 * 8, B7, C7, D7, E7, F7, G7, H7,
  A6 = 5 * 8, B6, C6, D6, E6, F6, G6, H6,
  A5 = 4 * 8, B5, C5, D5, E5, F5, G5, H5,
  A4 = 3 * 8, B4, C4, D4, E4, F4, G4, H4,
  A3 = 2 * 8, B3, C3, D3, E3, F3, G3, H3,
  A2 = 1 * 8, B2, C2, D2, E2, F2, G2, H2,
  A1 = 0 * 8, B1, C1, D1, E1, F1, G1, H1,
};
// clang-format on

enum MoveDirection : int8_t {
  NORTH = 8,
  SOUTH = -8,
  EAST = 1,
  WEST = -1
};

enum CastlingRights : uint8_t {
  NO_CASTLING = 0,
  W_CASTLE_KING = 1 << 0,
  W_CASTLE_QUEEN = 1 << 1,
  B_CASTLE_KING = 1 << 2,
  B_CASTLE_QUEEN = 1 << 3,

  W_ANY_CASTLING = W_CASTLE_KING | W_CASTLE_QUEEN,
  B_ANY_CASTLING = B_CASTLE_KING | B_CASTLE_QUEEN,
  ANY_CASTLING_KING = W_CASTLE_KING | B_CASTLE_KING,
  ANY_CASTLING_QUEEN = W_CASTLE_QUEEN | B_CASTLE_QUEEN,

  ANY_CASTLING = W_CASTLE_KING | W_CASTLE_QUEEN | B_CASTLE_KING | B_CASTLE_QUEEN
};

constexpr CastlingRights operator~(CastlingRights a) {
  return CastlingRights(~static_cast<uint8_t>(a));
}

constexpr CastlingRights operator|(CastlingRights a, CastlingRights b) {
  return CastlingRights(static_cast<uint8_t>(a) | static_cast<uint8_t>(b));
}

constexpr CastlingRights operator&(CastlingRights a, CastlingRights b) {
  return CastlingRights(static_cast<uint8_t>(a) & static_cast<uint8_t>(b));
}

constexpr CastlingRights &operator|=(CastlingRights &a, CastlingRights b) {
  a = a | b;
  return a;
}

constexpr CastlingRights &operator&=(CastlingRights &a, CastlingRights b) {
  a = a & b;
  return a;
}

enum PieceType : uint8_t {
  NO_TYPE,
  PAWN,
  KNIGHT,
  BISHOP,
  ROOK,
  QUEEN,
  KING,
  ANY_TYPE,
  TYPE_NB
};

enum Color : uint8_t {
  WHITE,
  BLACK,
  ANY_COLOR
};

// clang-format off
enum Piece: uint8_t {
  NO_PIECE = 0,
  W_PAWN = PAWN, W_KNIGHT, W_BISHOP, W_ROOK, W_QUEEN, W_KING,
  B_PAWN = PAWN + 8, B_KNIGHT, B_BISHOP, B_ROOK, B_QUEEN, B_KING,
  PIECE_NB = 16
};
// clang-format on

enum MoveType : uint8_t {
  NORMAL_MOVE = 0,
  CAPTURE = 1 << 0,
  CASTLING = 1 << 1,
  PROMOTION = 1 << 2,
  EN_PASSANT = 1 << 3
};

/**
 * A move packed into 16 bits: from in bits 0-5, to in bits 6-11, a flag
 * nibble in bits 12-15.
 *
 * The nibble collapses MoveType and the promotion piece into one value, which
 * is the only way both fit alongside two squares. That makes the encoding
 * canonical — one bit pattern per legal move — so equality is a single
 * integer compare and the whole move fits in a transposition table slot.
 *
 * MoveType stays the construction vocabulary because it reads better at the
 * generator's call sites; it is translated on the way in and never stored.
 */
struct Move {
  // Layout of the flag nibble. The capture and promotion bits are positional
  // so the predicates below stay single masks: 0b0100 is set by every capture
  // (en passant and promotion-captures included) and 0b1000 by every
  // promotion. The low two bits of a promotion select the piece.
  enum Flag : uint16_t {
    FLAG_QUIET = 0,
    FLAG_CASTLING = 2,
    FLAG_CAPTURE = 4,
    FLAG_EN_PASSANT = 5,
    FLAG_PROMOTION = 8,
    FLAG_PROMOTION_CAPTURE = 12
  };

  uint16_t data = 0;

  constexpr Move() = default;

  // Not explicit: the generator writes `{from, to, PROMOTION, QUEEN}`.
  constexpr Move(Square from, Square to, MoveType type = NORMAL_MOVE, PieceType promotion = NO_TYPE)
      : data(
            static_cast<uint16_t>(from) | static_cast<uint16_t>(to) << 6
            | pack_flags(type, promotion) << 12
        ) {}

  constexpr Square from() const { return Square(data & 0x3F); }
  constexpr Square to() const { return Square((data >> 6) & 0x3F); }
  constexpr uint16_t flags() const { return data >> 12; }
  constexpr uint16_t raw() const { return data; }

  constexpr bool is_capture() const { return (flags() & 4) != 0; }
  constexpr bool is_promotion() const { return (flags() & 8) != 0; }
  constexpr bool is_castling() const { return flags() == FLAG_CASTLING; }
  constexpr bool is_en_passant() const { return flags() == FLAG_EN_PASSANT; }

  constexpr PieceType promotion_piece() const {
    return is_promotion() ? PieceType(KNIGHT + (flags() & 3)) : NO_TYPE;
  }

  // A default-constructed move. No real move has from == to, so this cannot
  // collide with one, which is what lets killers and TT moves start empty.
  constexpr bool is_null() const { return data == 0; }

  constexpr bool operator==(const Move &other) const { return data == other.data; }
  constexpr bool operator!=(const Move &other) const { return data != other.data; }

private:
  static constexpr uint16_t pack_flags(MoveType type, PieceType promotion) {
    if (type & PROMOTION) {
      uint16_t base = (type & CAPTURE) ? FLAG_PROMOTION_CAPTURE : FLAG_PROMOTION;
      return base + static_cast<uint16_t>(promotion - KNIGHT);
    }

    if (type & EN_PASSANT) return FLAG_EN_PASSANT;
    if (type & CASTLING) return FLAG_CASTLING;
    if (type & CAPTURE) return FLAG_CAPTURE;

    return FLAG_QUIET;
  }
};

static_assert(sizeof(Move) == 2, "Move must stay 16 bits wide");

constexpr Color opposite_color(Color color) { return (color == WHITE) ? BLACK : WHITE; }

constexpr uint64_t square_to_bit(Square square) { return 1ULL << square; }
constexpr int square_file(int sq) { return sq % 8; }
constexpr int square_rank(int sq) { return sq / 8; }

constexpr Square indexes_to_square(int rank, int file) { return Square((rank) * 8 + (file)); }

constexpr Piece encode_piece(Color color, PieceType type) { return Piece((color << 3) + type); }

constexpr Color decode_color(uint8_t code) { return Color(code >> 3); }
constexpr PieceType decode_type(uint8_t code) { return PieceType(code & 7); }

/**
 * Get the index of the least significant bit set in a bitboard.
 *
 * @param bitboard
 * @return Square
 */
constexpr Square lsb_square(uint64_t bitboard) { return Square(__builtin_ctzll(bitboard)); }

/**
 * Pop the least significant bit from a bitboard and return it.
 *
 * @param bitboard
 * @return uint64_t
 */
constexpr uint64_t pop_lsb(uint64_t &bitboard) {
  uint64_t lsb = bitboard & -bitboard;
  bitboard &= bitboard - 1;
  return lsb;
}

/**
 * Pop the least significant bit from a bitboard and return its index.
 *
 * @param bitboard
 * @return Square
 */
constexpr Square pop_lsb_square(uint64_t &bitboard) {
  Square index = lsb_square(bitboard);
  pop_lsb(bitboard);
  return index;
}

/**
 * Count the number of bits set in a bitboard.
 *
 * @param bitboard
 * @return int
 */
constexpr int count_bits(uint64_t bitboard) { return __builtin_popcountll(bitboard); }

/**
 * Move a single bit in a bitboard by a specified number of squares in a given direction.
 *
 * @tparam direction The direction to move (NORTH, SOUTH, EAST, WEST)
 * @param bb The input bitboard
 * @param squares The number of squares to move in that direction (must be positive)
 * @return uint64_t The new bitboard with the bit moved
 */
template<MoveDirection direction>
constexpr uint64_t move_bit(uint64_t bb, int squares) {
  int src = lsb_square(bb);
  int dst = src + static_cast<int>(direction) * squares;

  return 1ULL << dst;
}

/**
 * Move a single bit in a bitboard by a specified number of squares in a given direction.
 *
 * @param bb The input bitboard (should have exactly one bit set)
 * @param direction The direction to move
 * @param squares The number of squares to move in that direction (must be positive)
 * @return uint64_t The new bitboard with the bit moved
 */
constexpr uint64_t move_bit(uint64_t bb, MoveDirection direction, int squares) {
  switch (direction) {
    case NORTH: return move_bit<NORTH>(bb, squares);
    case SOUTH: return move_bit<SOUTH>(bb, squares);
    case EAST: return move_bit<EAST>(bb, squares);
    case WEST: return move_bit<WEST>(bb, squares);
  }

  return 0;
}
