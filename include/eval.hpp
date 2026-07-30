#pragma once

#include "position.hpp"

// Just a class of static methods, so no need to instantiate it.
// NOTE: might look into making this a namespace instead.
class Eval {
public:
  Eval() = delete;

  // Centipawns from the side to move's perspective.
  static int evaluate(const Position &pos);

  // Which evaluation the next evaluate call will use.
  static const char *source_name();

private:
  // Hand-crafted evaluation (material + piece-square tables),
  // the fallback for when NNUE is not available.
  static int evaluate_hce(const Position &pos);
};
