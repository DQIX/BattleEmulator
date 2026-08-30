// Generate the unmanaged compile-time data used by freecam_fast_runtime.hpp.
// Verified metadata binaries are copied byte-for-byte. Target classification is
// copied from dq9-action-target-classification.csv into fixed 1024-entry byte
// tables. No BattleEmulator common-ID mapping or free-camera decision logic
// lives in this generator.
//
// Usage:
//   node build_freecam_fast_generated.mjs

import { readFile, writeFile } from "node:fs/promises";
import path from "node:path";

const root = import.meta.dirname;
const actionCount = 1024;
const inputs = [
  ["kCameraMetadataBytes", "freecam-camera-metadata.bin"],
  ["kActionMetadataBytes", "freecam-action-metadata.bin"],
  ["kMembershipMetadataBytes", "freecam-membership-metadata.bin"],
  ["kMonsterPresentationMetadataBytes", "freecam-monster-presentation-metadata.bin"],
];

function formatBytes(bytes) {
  const lines = [];
  const perLine = 32;
  for (let offset = 0; offset < bytes.length; offset += perLine) {
    lines.push(`    ${[...bytes.subarray(offset, offset + perLine)].join(", ")}`);
  }
  return lines.join(",\n");
}

function parseTargetClassification(csv) {
  const lines = csv.trimEnd().split(/\r?\n/);
  const header = lines[0].split(",");
  const actionIndex = header.indexOf("action_id_decimal");
  const sideIndex = header.indexOf("target_side_code");
  const scopeIndex = header.indexOf("target_scope_code");
  if (actionIndex < 0 || sideIndex < 0 || scopeIndex < 0) {
    throw new Error("dq9-action-target-classification.csv is missing required columns");
  }

  const targetSide = new Uint8Array(actionCount);
  const targetScope = new Uint8Array(actionCount);
  let mappedRows = 0;

  for (let lineIndex = 1; lineIndex < lines.length; ++lineIndex) {
    if (!lines[lineIndex]) continue;
    const fields = lines[lineIndex].split(",");
    const actionId = Number(fields[actionIndex]);
    const side = Number(fields[sideIndex]);
    const scope = Number(fields[scopeIndex]);
    if (!Number.isInteger(actionId) || actionId < 0 || actionId >= actionCount) {
      throw new Error(`invalid action_id_decimal at CSV line ${lineIndex + 1}`);
    }
    if (!Number.isInteger(side) || side < 0 || side > 255) {
      throw new Error(`invalid target_side_code at CSV line ${lineIndex + 1}`);
    }
    if (!Number.isInteger(scope) || scope < 0 || scope > 255) {
      throw new Error(`invalid target_scope_code at CSV line ${lineIndex + 1}`);
    }
    targetSide[actionId] = side;
    targetScope[actionId] = scope;
    ++mappedRows;
  }

  return { targetSide, targetScope, mappedRows };
}

const chunks = [
  "#pragma once",
  "",
  "#include <array>",
  "#include <cstdint>",
  "",
  "namespace dq9::freecam::generated {",
  "",
];

const summary = {};
for (const [name, file] of inputs) {
  const bytes = await readFile(path.join(root, file));
  summary[file] = bytes.length;
  chunks.push(`inline constexpr std::array<std::uint8_t, ${bytes.length}> ${name} = {`);
  chunks.push(formatBytes(bytes));
  chunks.push("};", "");
}

const targetCsvName = "dq9-action-target-classification.csv";
const targetCsv = await readFile(path.join(root, targetCsvName), "utf8");
const { targetSide, targetScope, mappedRows } = parseTargetClassification(targetCsv);
chunks.push(`inline constexpr std::array<std::uint8_t, ${actionCount}> kTargetSideCode = {`);
chunks.push(formatBytes(targetSide));
chunks.push("};", "");
chunks.push(`inline constexpr std::array<std::uint8_t, ${actionCount}> kTargetScopeCode = {`);
chunks.push(formatBytes(targetScope));
chunks.push("};", "");
chunks.push("} // namespace dq9::freecam::generated", "");

const output = chunks.join("\n");
const outputPath = path.join(root, "freecam_fast_generated.hpp");
await writeFile(outputPath, output, "utf8");
console.log(JSON.stringify({
  output: outputPath,
  sourceBytes: Buffer.byteLength(output),
  inputs: summary,
  targetClassification: { file: targetCsvName, mappedRows, actionCount },
}, null, 2));
