#pragma once

#include <cassert>
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
  WHITE_CASTLE_KING = 1,
  WHITE_CASTLE_QUEEN = 2,
  BLACK_CASTLE_KING = 4,
  BLACK_CASTLE_QUEEN = 8,

  ANY_CASTLING = WHITE_CASTLE_KING | WHITE_CASTLE_QUEEN | BLACK_CASTLE_KING | BLACK_CASTLE_QUEEN
};

enum PieceType : uint8_t {
  PIECE_NONE,
  PIECE_PAWN,
  PIECE_KNIGHT,
  PIECE_BISHOP,
  PIECE_ROOK,
  PIECE_QUEEN,
  PIECE_KING
};

enum Color : uint8_t {
  WHITE,
  BLACK,
  ANY
};

enum BitboardIndex : uint8_t {
  WHITE_PAWN,
  WHITE_KNIGHT,
  WHITE_BISHOP,
  WHITE_ROOK,
  WHITE_QUEEN,
  WHITE_KING,
  BLACK_PAWN,
  BLACK_KNIGHT,
  BLACK_BISHOP,
  BLACK_ROOK,
  BLACK_QUEEN,
  BLACK_KING
};

struct Piece {
  Color color;
  PieceType type;

  bool operator==(const Piece &other) const {
    bool eq_color = color == other.color;
    bool eq_type = type == other.type;

    return eq_color && eq_type;
  }

  bool operator!=(const Piece &other) const { return !(*this == other); }
};

enum MoveType : uint8_t {
  NORMAL_MOVE = 0,
  CAPTURE = 1 << 0,
  CASTLING = 1 << 1,
  PROMOTION = 1 << 2,
  EN_PASSANT = 1 << 3
};

struct Move {
  Square from;
  Square to;
  MoveType type = NORMAL_MOVE;
  PieceType promotion_piece = PIECE_NONE;

  bool is_capture() const { return (type & (CAPTURE | EN_PASSANT)) != 0; }
  bool is_promotion() const { return (type & PROMOTION) != 0; }
  bool is_castling() const { return (type & CASTLING) != 0; }
  bool is_en_passant() const { return (type & EN_PASSANT) != 0; }

  bool operator==(const Move &other) const {
    bool eq_to = to == other.to;
    bool eq_from = from == other.from;
    bool eq_type = type == other.type;
    bool eq_promotion = promotion_piece == other.promotion_piece;

    return eq_to && eq_from && eq_type && eq_promotion;
  }

  bool operator!=(const Move &other) const { return !(*this == other); }
};

constexpr Color opposite_color(Color color) { return (color == WHITE) ? BLACK : WHITE; }

constexpr uint64_t square_to_bit(Square square) { return 1ULL << square; }
constexpr int square_file(int sq) { return sq % 8; }
constexpr int square_rank(int sq) { return sq / 8; }

constexpr Square indexes_to_square(int rank, int file) {
  return static_cast<Square>((rank) * 8 + (file));
}

/**
 * @brief Get the index for the bitboard corresponding to a piece color and type.
 *
 * @param color
 * @param type
 * @return BitboardIndex
 */
constexpr BitboardIndex bitboard_index(Color color, PieceType type) {
  return static_cast<BitboardIndex>((color * 6) + type - 1);
}

constexpr uint8_t encode_piece(Color color, PieceType type) { return (color << 7) | type; }

constexpr Color decode_color(uint8_t code) { return static_cast<Color>(code >> 7); }
constexpr PieceType decode_type(uint8_t code) { return static_cast<PieceType>(code & 0x7F); }

constexpr Piece decode_piece(uint8_t code) {
  Color color = decode_color(code);
  PieceType type = decode_type(code);

  return Piece{color, type};
}

/**
 * @brief Get the index of the least significant bit set in a bitboard.
 *
 * @param bitboard
 * @return int
 */
constexpr int lsb_index(uint64_t bitboard) { return __builtin_ctzll(bitboard); }

/**
 * @brief Pop the least significant bit from a bitboard and return it.
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
 * @brief Pop the least significant bit from a bitboard and return its index.
 *
 * @param bitboard
 * @return int
 */
constexpr int pop_lsb_index(uint64_t &bitboard) {
  int index = lsb_index(bitboard);
  bitboard &= bitboard - 1;
  return index;
}

/**
 * @brief Count the number of bits set in a bitboard.
 *
 * @param bitboard
 * @return int
 */
constexpr int count_bits(uint64_t bitboard) { return __builtin_popcountll(bitboard); }

/**
 * @brief Move a single bit in a bitboard by a specified number of squares in a given direction.
 *
 * @tparam direction The direction to move (NORTH, SOUTH, EAST, WEST)
 * @param bb The input bitboard
 * @param squares The number of squares to move in that direction (must be positive)
 * @return uint64_t The new bitboard with the bit moved
 */
template<MoveDirection direction>
constexpr uint64_t move_bit(uint64_t bb, int squares) {
  assert(bb != 0);
  assert(count_bits(bb) == 1);
  assert(squares > 0);

  uint64_t result = bb;
  for (int i = 0; i < squares; i++) {
    if constexpr (direction == NORTH) {
      assert((result & RANK_8) == 0);
      result <<= 8;
      continue;
    }

    if constexpr (direction == SOUTH) {
      assert((result & RANK_1) == 0);
      result >>= 8;
      continue;
    }

    if constexpr (direction == EAST) {
      assert((result & FILE_H) == 0);
      result <<= 1;
      continue;
    }

    if constexpr (direction == WEST) {
      assert((result & FILE_A) == 0);
      result >>= 1;
      continue;
    }

    static_assert(
        direction == NORTH || direction == SOUTH || direction == EAST || direction == WEST,
        "Invalid direction"
    );
  }

  return result;
}

/**
 * @brief Move a single bit in a bitboard by a specified number of squares in a given direction.
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
    default: assert(false); return 0;
  }
}
