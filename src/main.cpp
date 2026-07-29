#include "attacks.hpp"
#include "datagen.hpp"
#include "tt.hpp"
#include "uci.hpp"

#include <string>

int main(int argc, char **argv) {
  init_attack_tables();

  if (argc > 1 && std::string(argv[1]) == "datagen") {
    ClessDatagen datagen;
    return datagen.run(argc - 2, argv + 2);
  }

  TT::resize(TT::DEFAULT_MB);

  ClessUCI uci;
  uci.run();

  return 0;
}
