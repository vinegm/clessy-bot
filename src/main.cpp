#include "datagen.hpp"
#include "transposition_table.hpp"
#include "uci.hpp"

#include <string>

int main(int argc, char **argv) {
  if (argc > 1 && std::string(argv[1]) == "datagen") {
    Datagen datagen;
    // NOTE: "datagen" stands in as the program name CLI11 skips
    return datagen.run(argc - 1, argv + 1);
  }

  TranspositionTable::resize(TranspositionTable::DEFAULT_MB);

  Uci uci;
  uci.run();

  return 0;
}
