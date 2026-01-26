#!/usr/bin/env bash
set -euo pipefail

ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
OUTPUT_DIR="${OUTPUT_DIR:-${ROOT}/public}"
BRANCH_NAME="${BRANCH_NAME:-}"
EMULATORS_FILTER="${EMULATORS:-}"

while [[ $# -gt 0 ]]; do
  case "$1" in
    --branch)
      BRANCH_NAME="$2"
      shift 2
      ;;
    --emulators)
      EMULATORS_FILTER="$2"
      shift 2
      ;;
    --output)
      OUTPUT_DIR="$2"
      shift 2
      ;;
    *)
      echo "Unknown argument: $1"
      exit 1
      ;;
  esac
done

if [[ -z "${BRANCH_NAME}" ]]; then
  BRANCH_NAME="local"
fi

declare -A EMU_LABELS
declare -A EMU_DEFINES

EMU_LABELS[lv15_sp22_v2]="Bilyouma LV15 SP22 v2"
EMU_DEFINES[lv15_sp22_v2]="-DBattleEmulatorLV15=1 -DMULTITHREADING=1"

EMU_IDS=(lv19_sp35 lv13_sp13 lv15_sp22_v2)

should_build() {
  local id="$1"
  if [[ -z "${EMULATORS_FILTER}" ]]; then
    return 0
  fi
  IFS=',' read -r -a filter_ids <<< "${EMULATORS_FILTER}"
  for item in "${filter_ids[@]}"; do
    if [[ "${item}" == "${id}" ]]; then
      return 0
    fi
  done
  return 1
}

SRC_FILES=(
  main.cpp
  lcg.cpp
  BattleEmulator.cpp
  camera.cpp
  debug.cpp
  ActionOptimizer.cpp
  EnhancedCostCalculator.cpp
  EnhancedHashCalculator.cpp
  EnhancedHeapQueue.cpp
  InputBuilder.cpp
  SimpleParameterOptimizer.cpp
)

EMCC_FLAGS=(
  -std=c++17
  -O3
  -sALLOW_MEMORY_GROWTH=1
  -sENVIRONMENT=web
  -sWASM_BIGINT=1
  -sNO_EXIT_RUNTIME=1
  -sINVOKE_RUN=0
  -sFILESYSTEM=0
  "-sEXPORTED_FUNCTIONS=['_wasm_prepare_input','_wasm_get_last_error','_wasm_bruteforce_range','_wasm_search_dump']"
  "-sEXPORTED_RUNTIME_METHODS=['ccall','cwrap','UTF8ToString']"
)

manifest_entries=()
branch_dir="${OUTPUT_DIR}/branches/${BRANCH_NAME}"
mkdir -p "${branch_dir}"

for id in "${EMU_IDS[@]}"; do
  if ! should_build "${id}"; then
    continue
  fi

  out_dir="${branch_dir}/${id}"
  mkdir -p "${out_dir}"

  emcc "${SRC_FILES[@]}" \
    ${EMU_DEFINES[${id}]} \
    "${EMCC_FLAGS[@]}" \
    -o "${out_dir}/emulator.js"

  manifest_entries+=("{\"id\":\"${id}\",\"label\":\"${EMU_LABELS[${id}]}\",\"branch\":\"${BRANCH_NAME}\",\"module\":\"branches/${BRANCH_NAME}/${id}/emulator.js\",\"defaultThreads\":4}")
done

manifest_path="${branch_dir}/manifest.json"
if [[ ${#manifest_entries[@]} -eq 0 ]]; then
  echo "[]" > "${manifest_path}"
else
  printf "[%s]\n" "$(IFS=,; echo "${manifest_entries[*]}")" > "${manifest_path}"
fi

echo "Manifest: ${manifest_path}"
