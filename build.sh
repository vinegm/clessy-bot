#!/usr/bin/env bash
# ./build.sh                       release build
# ./build.sh --pgo [network.nnue]  profile-guided release build: an instrumented build
#                                  plays a short datagen, the final build is optimized
#                                  for what it measured. Pass a network so the profile
#                                  covers NNUE inference too, not only the HCE

set -e

network=""
if [[ "$1" == "--pgo" && -n "$2" ]]; then
  network="$(realpath "$2")"
fi

mkdir -p build
cd "build"

if [[ "$1" != "--pgo" ]]; then
  cmake -DCMAKE_BUILD_TYPE=Release -DCLESSY_PGO=off ..
  # cmake -DCMAKE_BUILD_TYPE=Debug ..
  make
  exit
fi

# NOTE: a stale profile from older sources would steer the optimizer wrong
rm -rf pgo
cmake -DCMAKE_BUILD_TYPE=Release -DCLESSY_PGO=generate ..
make

workload="$(mktemp -d)"
trap 'rm -rf "$workload"' EXIT

./clessy datagen --games 12 --nodes 5000 --seed 1 --out "$workload/hce.binpack" >/dev/null
if [[ -n "$network" ]]; then
  ./clessy datagen --games 12 --nodes 5000 --seed 2 --eval-file "$network" \
    --out "$workload/nnue.binpack" >/dev/null
fi

cmake -DCMAKE_BUILD_TYPE=Release -DCLESSY_PGO=use ..
make
