#include "attacks.hpp"
#include "tt.hpp"
#include "uci.hpp"

int main() {
  init_attack_tables();
  TT::resize(TT::DEFAULT_MB);

  ClessUCI uci;
  uci.run();

  return 0;
}
