#include "binpack.hpp"

#include "position.hpp"

#include <algorithm>

uint16_t BinpackWriter::promotion_code(PieceType type) {
  switch (type) {
    case KNIGHT: return 0;
    case BISHOP: return 1;
    case ROOK: return 2;
    default: return 3; // QUEEN
  }
}

uint16_t BinpackWriter::encode_move(const Move &move) {
  uint16_t special = 0;
  if (move.is_castling()) {
    special = 1;
  } else if (move.is_en_passant()) {
    special = 2;
  } else if (move.is_promotion()) {
    special = 3;
  }

  uint16_t promotion = move.is_promotion() ? promotion_code(move.promotion_piece) : 0;

  return static_cast<uint16_t>(move.from) | static_cast<uint16_t>(move.to) << 6 | promotion << 12
         | special << 14;
}

void BinpackWriter::put_u8(uint8_t value) { buffer.push_back(value); }

void BinpackWriter::put_u16(uint16_t value) {
  put_u8(static_cast<uint8_t>(value));
  put_u8(static_cast<uint8_t>(value >> 8));
}

void BinpackWriter::put_u32(uint32_t value) {
  put_u16(static_cast<uint16_t>(value));
  put_u16(static_cast<uint16_t>(value >> 16));
}

void BinpackWriter::put_u64(uint64_t value) {
  put_u32(static_cast<uint32_t>(value));
  put_u32(static_cast<uint32_t>(value >> 32));
}

void BinpackWriter::write_header() {
  buffer.clear();
  put_u32(MAGIC);
  put_u32(VERSION);
  end_game();
}

void BinpackWriter::pack_position(const Position &pos) {
  uint64_t occupancy = pos.occupancy[ANY_COLOR];
  put_u64(occupancy);

  // Squares come out of the bitboard in ascending order, which is the order
  // the reader walks the same bitboard in.
  uint8_t low_nibble = 0;
  bool half_full = false;

  uint64_t remaining = occupancy;
  while (remaining) {
    Square square = pop_lsb_square(remaining);
    uint8_t piece = pos.lookup_table[square];
    uint8_t code = static_cast<uint8_t>(decode_color(piece) * 6 + (decode_type(piece) - 1));

    if (!half_full) {
      low_nibble = code;
      half_full = true;
      continue;
    }

    put_u8(static_cast<uint8_t>(low_nibble | code << 4));
    half_full = false;
  }

  if (half_full) put_u8(low_nibble);

  uint8_t flags = (pos.to_move == BLACK) ? 1 : 0;
  flags |= static_cast<uint8_t>(pos.castling_rights) << 1;
  if (pos.en_passant_square) flags |= 1 << 5;
  put_u8(flags);

  if (pos.en_passant_square) put_u8(static_cast<uint8_t>(*pos.en_passant_square));

  put_u8(static_cast<uint8_t>(std::clamp(pos.halfmove_clock, 0, 255)));
  put_u16(static_cast<uint16_t>(pos.fullmove_counter));
}

void BinpackWriter::begin_game(const Position &start, Result result, uint16_t plies) {
  buffer.clear();

  put_u8(result);
  put_u16(plies);
  pack_position(start);
}

void BinpackWriter::add_ply(const Move &move, int score, bool is_sample) {
  put_u16(encode_move(move));

  // Mate scores reach MATE_SCORE at most, so nothing the search reports can
  // overflow the field.
  put_u16(static_cast<uint16_t>(static_cast<int16_t>(std::clamp(score, -32767, 32767))));

  put_u8(is_sample ? 1 : 0);
}

void BinpackWriter::end_game() {
  out.write(
      reinterpret_cast<const char *>(buffer.data()),
      static_cast<std::streamsize>(buffer.size())
  );
  buffer.clear();
}
