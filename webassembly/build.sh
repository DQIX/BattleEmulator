#!/usr/bin/env bash
set -euo pipefail

ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
OUTPUT_DIR="${OUTPUT_DIR:-${ROOT}/public}"
BRANCH_NAME="${BRANCH_NAME:-local}"
PTHREAD_POOL_SIZE="${PTHREAD_POOL_SIZE:-8}"

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

BRANCH_VARIANTS[bilyouma_new_arugo]="
bilyouma_v6:-DBattleEmulatorLV15=1 -DMULTITHREADING=1
"

BRANCH_VARIANTS[gilyumei_eizou]="
bilyouma_v6:-DBattleEmulatorLV15=1 -DMULTITHREADING=1
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
  InputBuilder.cpp
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
  -sASSERTIONS=1
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
  rm -rf "${out_dir}"
  mkdir -p "${out_dir}"

  echo "==> Building ${BRANCH_NAME} / ${variant}"

  multithreaded=false
  variant_flags=()
  if [[ " ${defines} " == *" -DMULTITHREADING=1 "* ]]; then
    multithreaded=true
    variant_flags+=(
      -pthread
      "-sPTHREAD_POOL_SIZE=${PTHREAD_POOL_SIZE}"
    )
  fi

  emcc "${SRC_FILES[@]}" \
    ${defines} \
    "${variant_flags[@]}" \
    "${EMCC_FLAGS[@]}" \
    -o "${out_dir}/emulator.js"

  runtime_assets=()
  while IFS= read -r -d '' asset_path; do
    asset_name="$(basename "${asset_path}")"
    [[ "${asset_name}" == "emulator.js" ]] && continue
    runtime_assets+=("${asset_name}=branches/${BRANCH_NAME}/${variant}/${asset_name}")
  done < <(find "${out_dir}" -maxdepth 1 -type f -print0)

  if [[ "${multithreaded}" == "true" && ${#runtime_assets[@]} -eq 0 ]]; then
    echo "multithreaded build produced no external runtime asset: ${BRANCH_NAME}/${variant}" >&2
    exit 1
  fi

  manifest_entries+=("$(python3 - \
    "${variant}" \
    "${BRANCH_NAME}" \
    "branches/${BRANCH_NAME}/${variant}/emulator.js" \
    "${multithreaded}" \
    "${PTHREAD_POOL_SIZE}" \
    "${runtime_assets[@]}" <<'PY'
import json
import sys

variant, branch, module, multithreaded, pthread_pool_size, *assets = sys.argv[1:]
entry = {
    "id": variant,
    "label": variant,
    "branch": branch,
    "module": module,
    "defaultThreads": 4,
    "multithreaded": multithreaded == "true",
}
if entry["multithreaded"]:
    entry["pthreadPoolSize"] = int(pthread_pool_size)
if assets:
    entry["runtimeAssets"] = dict(asset.split("=", 1) for asset in assets)
print(json.dumps(entry, separators=(",", ":")))
PY
  )")
done <<< "${variants}"

manifest_path="${branch_dir}/manifest.json"
if [[ ${#manifest_entries[@]} -eq 0 ]]; then
  echo "[]" > "${manifest_path}"
else
  printf "[%s]\n" "$(IFS=,; echo "${manifest_entries[*]}")" > "${manifest_path}"
fi

echo "Manifest written: ${manifest_path}"
