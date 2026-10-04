#pragma once

#include "chess_types.hpp"

#include <cstdint>
#include <ostream>
#include <vector>

class Position;

// NOTE: this is the authoritative spec, clessy-nnue/clessy_nnue/binpack.py mirrors it
//
// Every field is little-endian. The file is:
//
//     magic   u32   "CBIN"
//     version u32
//     game[]        until end of file
//
// A game is:
//
//     result  u8    0 black win, 1 draw, 2 white win
//     plies   u16   number of ply records that follow
//     position      the packed position the game started from
//     ply[plies]
//
// A packed position is:
//
//     occupancy  u64            occupied squares, little-endian rank-file
//     pieces     u8[(n + 1)/2]  4 bits per occupied square, ascending, low nibble first;
//                               code = color * 6 + (piece_type - 1)
//     flags      u8             bit 0 side to move (1 = black)
//                               bits 1-4 castling rights, as CastlingRights
//                               bit 5 an en passant square follows
//     en passant u8             only present when flags bit 5 is set
//     halfmove   u8             clamped to 255
//     fullmove   u16
//
// A ply is 5 bytes:
//
//     move   u16   from | to << 6 | promotion << 12 | special << 14
//                  special: 0 normal, 1 castling, 2 en passant, 3 promotion
//                  promotion: 0 knight, 1 bishop, 2 rook, 3 queen
//     score  i16   centipawns from the side to move
//     flags  u8    bit 0: this ply is a training sample
//
// The move has no capture flag, the reader matches it against the legal moves.
class BinpackWriter {
public:
  static constexpr uint32_t MAGIC = 0x4e494243; // "CBIN"
  static constexpr uint32_t VERSION = 1;

  enum Result : uint8_t {
    BLACK_WIN = 0,
    DRAW = 1,
    WHITE_WIN = 2
  };

  explicit BinpackWriter(std::ostream &out) : out(out) {}

  void write_header();

  void begin_game(const Position &start, Result result, uint16_t plies);

  void add_ply(const Move &move, int score, bool is_sample);

  void end_game();

private:
  std::ostream &out;
  std::vector<uint8_t> buffer;

  void put_u8(uint8_t value);
  void put_u16(uint16_t value);
  void put_u32(uint32_t value);
  void put_u64(uint64_t value);

  void pack_position(const Position &pos);

  static uint16_t promotion_code(PieceType type);
  static uint16_t special_code(const Move &move);

  static uint16_t encode_move(const Move &move);
};
