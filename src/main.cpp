#include "attacks.hpp"
#include "transposition_table.hpp"
#include "uci.hpp"

int main() {
  init_attack_tables();
  TranspositionTable::resize(TranspositionTable::DEFAULT_MB);

  ClessUCI uci;
  uci.run();

  return 0;
}
