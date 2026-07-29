#pragma once

#include "position.hpp"

/// @brief Hand-crafted evaluation (material + piece-square tables), the
/// fallback for when no NNUE network is loaded.
int evaluate_hce(const Position &pos);
