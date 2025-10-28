#include "board.hpp"

#include "chess_types.hpp"

#include <vector>

#define title_padding line_padding
#define board_padding (title_padding + line_padding * 2)
#define next_move_padding (board_padding + board_height + line_padding)
#define board_width 18  // 8 + 8 for 2 chars per square, + 2 for borders
#define board_height 10 // 8 for squares + 2 for borders

void BoardWin::draw() {
  WINDOW *parent_win = handler->get_main_win();
  int parent_height, parent_width;
  getmaxyx(parent_win, parent_height, parent_width);

  box(parent_win, 0, 0);

  std::string title = "CLESS - Chess Game";
  std::vector<std::string> instructions =
      {"Arrow keys/hjkl: move cursor", "Space/Enter: select", "o: flip board", "q: return to menu"};

  int instructions_count = instructions.size();
  int instructions_line = parent_height - line_padding - instructions_count;

  modifier_wrapper(parent_win, A_BOLD, [&]() {
    mvwprintw_centered(parent_win, parent_width, title_padding, title);
  });

  modifier_wrapper(parent_win, A_DIM, [&]() {
    for (size_t i = 0; i < instructions_count; i++) {
      mvwprintw_centered(parent_win, parent_width, instructions_line + i, instructions[i]);
    }
  });

  int board_start_x = (parent_width - board_width) / 2;
  WINDOW *board_win_ptr =
      derwin(parent_win, board_height, board_width, board_padding, board_start_x);
  board_win = UniqueWindow(board_win_ptr);

  modifier_wrapper(board_win_ptr, A_DIM, [&]() {
    printw_rank_labels();
    printw_file_labels();
  });

  game_loop();
}

void BoardWin::game_loop() {
  WINDOW *board_win_ptr = board_win.get();
  WINDOW *parent_win = handler->get_main_win();

  int _, parent_width;
  getmaxyx(parent_win, _, parent_width);

  int pressed_key;
  bool is_white_orientation = (board_orientation == BOARD_ORIENTATION_WHITE);
  int orientation_mod = is_white_orientation ? 1 : -1;
  std::string to_move_text;

  keypad(board_win_ptr, true);
  while (true) {
    to_move_text = (game.to_move() == PieceColor::WHITE) ? "White to move" : "Black to move";

    modifier_wrapper(parent_win, A_BOLD, [&]() {
      mvwprintw_centered(parent_win, parent_width, next_move_padding, to_move_text);
    });

    wrefresh(parent_win);

    printw_board();

    pressed_key = wgetch(board_win_ptr);
    switch (pressed_key) {
      case ' ':
      case 10: handle_piece_selection(); break;

      case 'k':
      case KEY_UP:
        highlighted_square += NORTH * orientation_mod;
        highlighted_square %= 64;
        break;

      case 'j':
      case KEY_DOWN:
        highlighted_square -= NORTH * orientation_mod;
        highlighted_square %= 64;
        break;

      case 'h':
      case KEY_LEFT: {
        const int col = highlighted_square % 8;
        const int left_edge = (orientation_mod == 1) ? 0 : 7;

        if (col == left_edge) {
          highlighted_square += 7 * orientation_mod;
          break;
        }

        highlighted_square -= EAST * orientation_mod;
        break;
      }

      case 'l':
      case KEY_RIGHT: {
        const int col = highlighted_square % 8;
        const int right_edge = (orientation_mod == 1) ? 7 : 0;

        if (col == right_edge) {
          highlighted_square -= 7 * orientation_mod;
          break;
        }

        highlighted_square += EAST * orientation_mod;
        break;
      }

      case 'o':
        orientation_mod *= -1;
        board_orientation = !board_orientation;
        highlighted_square = 63 - highlighted_square;
        modifier_wrapper(board_win_ptr, A_DIM, [&]() { printw_rank_labels(); });
        break;

      case 'q': handler->next_window = menu_win_name; return;

      case KEY_RESIZE: handler->event = handler->resize_event; return;

      case ERR: break;

      default: break;
    }
  }
}

/**
 * @brief Render the board
 */
void BoardWin::printw_board() {
  WINDOW *board_win_ptr = board_win.get();
  for (int draw_rank = 0; draw_rank < 8; draw_rank++) {
    for (int draw_file = 0; draw_file < 8; draw_file++) {
      const int square = get_square_from_orientation(draw_rank, draw_file);
      const int color_pair = get_square_color(square);

      char piece_char = get_piece_char_at(static_cast<Square>(square));
      modifier_wrapper(board_win_ptr, COLOR_PAIR(color_pair), [&]() {
        mvwprintw(board_win_ptr, draw_rank + line_padding, draw_file * 2 + 1, "%c ", piece_char);
      });
    }
  }
  wrefresh(board_win_ptr);
}

/**
 * @brief Render the rank labels on the board
 */
void BoardWin::printw_rank_labels() {
  WINDOW *board_win_ptr = board_win.get();

  switch (board_orientation) {
    case BOARD_ORIENTATION_WHITE:
      for (int i = 0; i < 8; i++) {
        mvwprintw(board_win_ptr, i + line_padding, 0, "%d", 8 - i);
        mvwprintw(board_win_ptr, i + line_padding, board_width - 1, "%d", 8 - i);
      }
      break;

    case BOARD_ORIENTATION_BLACK:
      for (int i = 0; i < 8; i++) {
        mvwprintw(board_win_ptr, i + line_padding, 0, "%d", i + 1);
        mvwprintw(board_win_ptr, i + line_padding, board_width - 1, "%d", i + 1);
      }
      break;
  }
}

/**
 * @brief Render the file labels on the board
 */
void BoardWin::printw_file_labels() {
  WINDOW *board_win_ptr = board_win.get();

  mvwprintw(board_win_ptr, 0, 1, "a b c d e f g h");
  mvwprintw(board_win_ptr, board_height - 1, 1, "a b c d e f g h");
}

/**
 * @brief Get the square based on the board orientation
 *
 * @param draw_rank
 * @param draw_file
 * @return int
 */
int BoardWin::get_square_from_orientation(int draw_rank, int draw_file) {
  int rank, file;
  switch (board_orientation) {
    case BOARD_ORIENTATION_WHITE:
      rank = 7 - draw_rank;
      file = draw_file;
      return rank * 8 + file;

    case BOARD_ORIENTATION_BLACK:
      rank = draw_rank;
      file = 7 - draw_file;
      return rank * 8 + file;

    default: return -1; // Error case, should not happen
  }
}

/**
 * @brief Get the color of the square
 *
 * @param square
 * @return int
 */
int BoardWin::get_square_color(int square) {
  if (square == highlighted_square) return HIGHLIGHTED_SQUARE;

  if (square == selected_square) return SELECTED_SQUARE;

  if (selected_square.has_value()) {
    MoveList legal_moves = game.get_legal_moves_from(static_cast<Square>(selected_square.value()));
    for (int i = 0; i < legal_moves.count; i++) {
      if (legal_moves[i].to == square) return LEGAL_MOVE_SQUARE;
    }
  }

  int is_white_square = ((square / 8 + square % 8) % 2 == 0) ? 1 : 0;
  return is_white_square ? WHITE_SQUARE : BLACK_SQUARE;
}

/**
 * @brief Handle piece selection and movement logic
 */
void BoardWin::handle_piece_selection() {
  // Select
  if (!selected_square.has_value()) {
    Piece piece = game.get_piece_at(static_cast<Square>(highlighted_square));
    if (piece.type != PIECE_NONE && piece.color == game.to_move())
      selected_square = highlighted_square;
    return;
  }

  // Deselect
  if (highlighted_square == selected_square) {
    selected_square = std::nullopt;
    return;
  }

  // Make move
  MoveList legal_moves = game.get_legal_moves_from(static_cast<Square>(selected_square.value()));

  Move *found_move = nullptr;
  for (int i = 0; i < legal_moves.count; i++) {
    if (legal_moves[i].to == highlighted_square) {
      found_move = &legal_moves[i];
      break;
    }
  }

  bool move_successful = false;
  if (found_move) { move_successful = game.make_move(*found_move); }

  // Deselect after move
  if (move_successful) {
    selected_square = std::nullopt;
    return;
  }

  // Failed move, select new highlighted square
  selected_square = std::nullopt;
  handle_piece_selection();
}

/**
 * @brief Get the character representation of a piece at a square
 */
char BoardWin::get_piece_char_at(Square square) {
  Piece piece = game.get_piece_at(square);

  if (piece.type == PIECE_NONE) { return ' '; }

  char piece_char;
  switch (piece.type) {
    case PIECE_PAWN: piece_char = 'P'; break;
    case PIECE_ROOK: piece_char = 'R'; break;
    case PIECE_KNIGHT: piece_char = 'N'; break;
    case PIECE_BISHOP: piece_char = 'B'; break;
    case PIECE_QUEEN: piece_char = 'Q'; break;
    case PIECE_KING: piece_char = 'K'; break;
    default: piece_char = ' '; break;
  }

  // Convert to lowercase for black pieces
  if (piece.color == BLACK) { piece_char = tolower(piece_char); }

  return piece_char;
}
