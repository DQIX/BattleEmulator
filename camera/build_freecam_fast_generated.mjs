// Generate the unmanaged compile-time byte dump used by freecam_fast_runtime.hpp.
// No filtering, mapping, or free-camera logic lives here: each verified metadata
// binary is copied byte-for-byte into one constexpr std::array<uint8_t>.
//
// Usage:
//   node build_freecam_fast_generated.mjs

import { readFile, writeFile } from "node:fs/promises";
import path from "node:path";

const root = import.meta.dirname;
const inputs = [
  ["kCameraMetadataBytes", "freecam-camera-metadata.bin"],
  ["kActionMetadataBytes", "freecam-action-metadata.bin"],
  ["kMembershipMetadataBytes", "freecam-membership-metadata.bin"],
];

function formatBytes(bytes) {
  const lines = [];
  const perLine = 32;
  for (let offset = 0; offset < bytes.length; offset += perLine) {
    lines.push(`    ${[...bytes.subarray(offset, offset + perLine)].join(", ")}`);
  }
  return lines.join(",\n");
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
chunks.push("} // namespace dq9::freecam::generated", "");

const output = chunks.join("\n");
const outputPath = path.join(root, "freecam_fast_generated.hpp");
await writeFile(outputPath, output, "utf8");
console.log(JSON.stringify({ output: outputPath, sourceBytes: Buffer.byteLength(output), inputs: summary }, null, 2));
