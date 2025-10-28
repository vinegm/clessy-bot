#include "chess_types.hpp"
#include "engine.hpp"

#include <criterion/criterion.h>
#include <string>

struct ExpectedResult {
  std::string initial_fen = INITIAL_POSITION_FEN;
  std::optional<std::string> fen_after_move{};

  bool result{};
  PieceColor next_turn{};

  Piece moving_piece{};
  std::optional<Piece> piece_at_capture{};

  Move move{};
  std::optional<Square> capture_square{};
};

std::string piece_to_string(const Piece &piece) {
  std::string piece_str;

  switch (piece.color) {
    case WHITE: piece_str += "White "; break;
    case BLACK: piece_str += "Black "; break;
    default: piece_str += "Unknown "; break;
  }

  switch (piece.type) {
    case PIECE_PAWN: piece_str += "Pawn"; break;
    case PIECE_ROOK: piece_str += "Rook"; break;
    case PIECE_KNIGHT: piece_str += "Knight"; break;
    case PIECE_BISHOP: piece_str += "Bishop"; break;
    case PIECE_QUEEN: piece_str += "Queen"; break;
    case PIECE_KING: piece_str += "King"; break;
    case PIECE_NONE: piece_str = "Empty"; break;
    default: piece_str = "Unknown"; break;
  }

  return piece_str;
}

/**
 * @brief Check positions inferring info from ExpectedResult
 *
 * @param board
 * @param expected
 * @param after_move
 */
void check_positions(ClessEngine &board, const ExpectedResult &expected, bool after_move) {
  Piece piece_at_from = board.get_piece_at(expected.move.from);
  Piece piece_at_to = board.get_piece_at(expected.move.to);
  const char *context = after_move ? "after move" : "before move";

  if (after_move && expected.result) {
    Piece empty_piece = {WHITE, PIECE_NONE};
    cr_assert_eq(
        piece_at_from,
        empty_piece,
        "Piece at 'from' mismatch %s: got %s, expected Empty",
        context,
        piece_to_string(piece_at_from).c_str()
    );

    cr_assert_eq(
        piece_at_to,
        expected.moving_piece,
        "Piece at 'to' mismatch %s: got %s, expected %s",
        context,
        piece_to_string(piece_at_to).c_str(),
        piece_to_string(expected.moving_piece).c_str()
    );
  } else if (!after_move) {
    cr_assert_eq(
        piece_at_from,
        expected.moving_piece,
        "Piece at 'from' mismatch %s: got %s, expected %s",
        context,
        piece_to_string(piece_at_from).c_str(),
        piece_to_string(expected.moving_piece).c_str()
    );
  }

  if (expected.capture_square.has_value() && expected.capture_square != expected.move.to
      && expected.piece_at_capture.has_value()) {
    Piece piece_at_capture = board.get_piece_at(expected.capture_square.value());

    if (after_move && expected.result) {
      Piece empty_piece = {WHITE, PIECE_NONE};
      cr_assert_eq(
          piece_at_capture,
          empty_piece,
          "Piece at 'capture' mismatch %s: got %s, expected Empty",
          context,
          piece_to_string(piece_at_capture).c_str()
      );
    } else if (!after_move) {
      cr_assert_eq(
          piece_at_capture,
          expected.piece_at_capture.value(),
          "Piece at 'capture' mismatch %s: got %s, expected %s",
          context,
          piece_to_string(piece_at_capture).c_str(),
          piece_to_string(expected.piece_at_capture.value()).c_str()
      );
    }
  }
}

/**
 * @brief Fully tests state before, after and undoing a move
 *
 * @param expected
 */
void check_case(ExpectedResult &expected) {
  ClessEngine board(expected.initial_fen, true);

  check_positions(board, expected, false);

  bool result = board.make_move(expected.move);
  if (expected.fen_after_move.has_value()) {
    cr_assert_eq(
        board.get_fen(),
        expected.fen_after_move.value(),
        "FEN mismatch after making move"
    );
  }
  cr_assert_eq(result, expected.result, "Move result mismatch");
  cr_assert_eq(board.to_move(), expected.next_turn, "Expected turn to switch correctly after move");
  check_positions(board, expected, true);

  if (!expected.result) return;
  board.undo_move();
  cr_assert_neq(
      board.to_move(),
      expected.next_turn,
      "Expected turn to switch correctly after undoing move"
  );
  cr_assert_eq(board.get_fen(), expected.initial_fen, "FEN mismatch after undoing move");
  check_positions(board, expected, false);
}

Test(unique_moves, e4) {
  ExpectedResult expected = {
      .result = true,
      .next_turn = BLACK,
      .moving_piece = {WHITE, PIECE_PAWN},
      .move = {E2, E4},
  };

  check_case(expected);
}

Test(unique_moves, dxe6_en_passant) {
  ExpectedResult expected = {
      .initial_fen = "rnbqkbnr/ppp2ppp/8/3Pp3/8/8/PPPP1PPP/RNBQKBNR w KQkq e6 0 1",
      .result = true,
      .next_turn = BLACK,
      .moving_piece = {WHITE, PIECE_PAWN},
      .piece_at_capture = std::make_optional<Piece>({BLACK, PIECE_PAWN}),
      .move = {D5, E6, EN_PASSANT},
      .capture_square = E5,
  };

  check_case(expected);
}

Test(unique_moves, dxe6_invalid_en_passant) {
  ExpectedResult expected = {
      .initial_fen = "rnbqkbnr/ppp2ppp/8/3Pp3/8/8/PPPP1PPP/RNBQKBNR w KQkq - 0 1",
      .result = false,
      .next_turn = WHITE,
      .moving_piece = {WHITE, PIECE_PAWN},
      .move = {D5, E6, EN_PASSANT},
  };

  check_case(expected);
}

Test(unique_moves, Kxe2) {
  ExpectedResult expected = {
      .initial_fen = "4k3/R7/8/8/8/8/4r3/4K3 w - - 0 1",
      .result = true,
      .next_turn = BLACK,
      .moving_piece = {WHITE, PIECE_KING},
      .move = {E1, E2, CAPTURE},
  };
  check_case(expected);
}

Test(unique_moves, illegal_move_due_to_pin) {
  ExpectedResult expected = {
      .initial_fen = "4k3/8/8/8/7b/8/5R2/4K3 w - - 0 1",
      .result = false,
      .next_turn = WHITE,
      .moving_piece = {WHITE, PIECE_ROOK},
      .move = {F2, F8},
  };
  check_case(expected);
}

Test(unique_moves, king_moves_into_check) {
  ExpectedResult expected = {
      .initial_fen = "4k3/8/8/8/8/8/7r/4K3 w - - 0 1",
      .result = false,
      .next_turn = WHITE,
      .moving_piece = {WHITE, PIECE_KING},
      .move = {E1, E2},
  };
  check_case(expected);
}

Test(unique_moves, kingside_castle_white) {
  ExpectedResult expected = {
      .initial_fen = "rnbqkbnr/pppppppp/8/8/8/8/PPPPPPPP/RNBQK2R w KQkq - 0 1",
      .result = true,
      .next_turn = BLACK,
      .moving_piece = {WHITE, PIECE_KING},
      .move = {E1, G1, CASTLING},
      .capture_square = std::nullopt,
  };
  check_case(expected);
}

Test(unique_moves, queenside_castle_black) {
  ExpectedResult expected = {
      .initial_fen = "r3kbnr/pppppppp/8/8/8/8/PPPPPPPP/RNBQKBNR b KQkq - 0 1",
      .result = true,
      .next_turn = WHITE,
      .moving_piece = {BLACK, PIECE_KING},
      .move = {E8, C8, CASTLING},
  };
  check_case(expected);
}

Test(unique_moves, invalid_castle_through_check) {
  ExpectedResult expected = {
      .initial_fen = "rnbqkbn1/ppppppp1/8/6r1/8/8/PPPPP2P/RNBQK2R b KQq - 0 1",
      .result = false,
      .next_turn = BLACK,
      .moving_piece = {WHITE, PIECE_KING},
      .move = {E1, G1, CASTLING},
  };
  check_case(expected);
}

Test(unique_moves, e5_wrong_turn) {
  ExpectedResult expected = {
      .result = false,
      .next_turn = WHITE,
      .moving_piece = {BLACK, PIECE_PAWN},
      .move = {E7, E5},
  };

  check_case(expected);
}

Test(unique_moves, e5_from_empty_square) {
  ExpectedResult expected = {
      .result = false,
      .next_turn = WHITE,
      .moving_piece = {WHITE, PIECE_NONE},
      .move = {E4, E5},
  };

  check_case(expected);
}
