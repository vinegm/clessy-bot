#include "attacks.hpp"
#include "uci.hpp"

int main() {
  init_attack_tables();

  ClessUCI uci;
  uci.run();

  return 0;
}
