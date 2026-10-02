#pragma once

#include "chess_types.hpp"

#include <cstdint>
#include <ostream>
#include <vector>

class Position;

/**
 * @brief Writer for the .binpack training-data format.
 *
 * This header is the authoritative spec; `clessy-nnue/clessy_nnue/binpack.py`
 * reads what it writes and mirrors the layout below.
 *
 * The text format stores one independent FEN per position, which costs about
 * 75 bytes for a position that a preceding one already almost fully described.
 * Self-play produces whole games, so this stores a game as one packed starting
 * position plus the move played at every ply, and the reader replays them.
 * Positions the filter rejected still contribute their move — the chain cannot
 * skip a ply — but carry a clear sample bit. That lands around 5 bytes per ply.
 *
 * Every field is little-endian. The file is:
 *
 *     magic   u32   "CBIN"
 *     version u32
 *     game[]        until end of file
 *
 * A game is:
 *
 *     result  u8    0 black win, 1 draw, 2 white win, from White
 *     plies   u16   number of ply records that follow
 *     position      the packed position the game started from
 *     ply[plies]
 *
 * A packed position is:
 *
 *     occupancy u64            occupied squares, little-endian rank-file
 *     pieces    u8[(n + 1)/2]  4 bits per occupied square, squares in
 *                              ascending order, low nibble of a byte first;
 *                              code = color * 6 + (piece_type - 1)
 *     flags     u8             bit 0 side to move (1 = black)
 *                              bits 1-4 castling rights, as CastlingRights
 *                              bit 5 an en passant square follows
 *     ep        u8             only present when flags bit 5 is set
 *     halfmove  u8             clamped to 255, which no legal game reaches
 *     fullmove  u16
 *
 * A ply is 5 bytes:
 *
 *     move   u16   from | to << 6 | promotion << 12 | special << 14
 *                  special: 0 normal, 1 castling, 2 en passant, 3 promotion
 *                  promotion: 0 knight, 1 bishop, 2 rook, 3 queen
 *     score  i16   centipawns from the side to move
 *     flags  u8    bit 0: this ply is a training sample
 *
 * The move drops the capture flag the engine's Move carries, because a reader
 * holding the position can see it. Both sides recover the full move by
 * matching from/to/promotion against the legal moves, the same way the UCI
 * layer resolves a move string.
 */
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

  /**
   * @brief Open a game record. Every ply must follow before end_game.
   *
   * @param start Position the game was played from
   * @param result Outcome, from White
   * @param plies How many ply records will be added
   */
  void begin_game(const Position &start, Result result, uint16_t plies);

  void add_ply(const Move &move, int score, bool is_sample);

  /// @brief Write the buffered game out as one unit.
  void end_game();

private:
  std::ostream &out;
  std::vector<uint8_t> buffer;

  void put_u8(uint8_t value);
  void put_u16(uint16_t value);
  void put_u32(uint32_t value);
  void put_u64(uint64_t value);

  void pack_position(const Position &pos);

  /// @brief The two bits a promotion piece is stored in.
  static uint16_t promotion_code(PieceType type);

  static uint16_t encode_move(const Move &move);
};
