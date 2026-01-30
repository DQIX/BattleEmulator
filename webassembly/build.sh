#!/usr/bin/env bash
set -euo pipefail

ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
OUTPUT_DIR="${OUTPUT_DIR:-${ROOT}/public}"
BRANCH_NAME="${BRANCH_NAME:-local}"

while [[ $# -gt 0 ]]; do
  case "$1" in
    --branch)
      BRANCH_NAME="$2"
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

# ------------------------------------------------------------
# ブランチごとのビルド variant 定義
# 形式:
#   variant_name:compiler_defines
# ------------------------------------------------------------
declare -A BRANCH_VARIANTS

BRANCH_VARIANTS[yo2_lv5_algorithm_v4]="
isilyudaru_v6:-DMULTITHREADING=1
"



#BRANCH_VARIANTS[webassembly]="
#default:-DMULTITHREADING=0
#"

# 未定義ブランチ用フォールバック
DEFAULT_VARIANTS="
default:
"

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
)

EMCC_FLAGS=(
  -std=c++20
  -O3
  -sALLOW_MEMORY_GROWTH=1
  -sENVIRONMENT=worker,web
  -sWASM_BIGINT=1
  -sNO_EXIT_RUNTIME=1
  -sINVOKE_RUN=0
  -sFILESYSTEM=0

  -sWASM_ASYNC_COMPILATION=0
  -sSINGLE_FILE=1

  "-sEXPORTED_FUNCTIONS=['_wasm_prepare_input','_wasm_get_last_error','_wasm_bruteforce_range','_wasm_get_turn_processed','_wasm_get_found_seeds','_wasm_search_dump']"
  "-sEXPORTED_RUNTIME_METHODS=['ccall','cwrap','UTF8ToString']"
)

EMCC_FLAGS+=(
  -DNDEBUG
  -D_LIBCPP_ASSERTIONS=0
  -D_LIBCPP_ENABLE_ASSERTIONS=0
  -sASSERTIONS=0
)


variants="${BRANCH_VARIANTS[${BRANCH_NAME}]:-${DEFAULT_VARIANTS}}"

branch_dir="${OUTPUT_DIR}/branches/${BRANCH_NAME}"
mkdir -p "${branch_dir}"

manifest_entries=()

while read -r line; do
  [[ -z "$line" ]] && continue

  variant="${line%%:*}"
  defines="${line#*:}"

  out_dir="${branch_dir}/${variant}"
  mkdir -p "${out_dir}"

  echo "==> Building ${BRANCH_NAME} / ${variant}"

  emcc "${SRC_FILES[@]}" \
    ${defines} \
    "${EMCC_FLAGS[@]}" \
    -o "${out_dir}/emulator.js"

  manifest_entries+=(
    "{\"id\":\"${variant}\",\"label\":\"${variant}\",\"branch\":\"${BRANCH_NAME}\",\"module\":\"branches/${BRANCH_NAME}/${variant}/emulator.js\",\"defaultThreads\":4}"
  )
done <<< "${variants}"

manifest_path="${branch_dir}/manifest.json"
if [[ ${#manifest_entries[@]} -eq 0 ]]; then
  echo "[]" > "${manifest_path}"
else
  printf "[%s]\n" "$(IFS=,; echo "${manifest_entries[*]}")" > "${manifest_path}"
fi

echo "Manifest written: ${manifest_path}"
