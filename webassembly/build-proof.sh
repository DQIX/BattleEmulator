#!/usr/bin/env bash
set -euo pipefail
ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
OUTPUT_DIR="${OUTPUT_DIR:-${ROOT}/public/proof}"
mkdir -p "${OUTPUT_DIR}"
COMMON=(
  lcg.cpp
  BattleEmulator.cpp
  camera.cpp
  debug.cpp
  proof/ProofTypes.cpp
  proof/ExactReplay.cpp
  proof/RuleProgram.cpp
  proof/SymbolicStepper.cpp
  proof/ProofKernel.cpp
  proof/CliCommon.cpp
)
FLAGS=(
  -std=c++20
  -O3
  -DMULTITHREADING
  -DOPTIMIZATION_O3_ENABLED
  -DD20_PROOF_NUMERIC_CONTRACT_V1
  -fno-fast-math
  -ffp-contract=off
  -sALLOW_MEMORY_GROWTH=1
  -sENVIRONMENT=worker,web
  -sWASM_BIGINT=1
  -sNO_EXIT_RUNTIME=1
  -sINVOKE_RUN=0
  -sFILESYSTEM=0
  -sWASM_ASYNC_COMPILATION=0
  -sSINGLE_FILE=1
  -sASSERTIONS=1
)
cd "${ROOT}"
emcc proof/kernel_main.cpp "${COMMON[@]}" "${FLAGS[@]}" -o "${OUTPUT_DIR}/d20-proof-kernel.js"
emcc proof/worldline_main.cpp proof/WorldlineSolver.cpp "${COMMON[@]}" "${FLAGS[@]}" -o "${OUTPUT_DIR}/d20-worldline.js"
echo "Proof WASM modules written to ${OUTPUT_DIR}"
