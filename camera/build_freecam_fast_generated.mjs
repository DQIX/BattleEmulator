// Generate the unmanaged compile-time data used by freecam_fast_runtime.hpp.
// Verified metadata binaries are copied byte-for-byte. Already-mined fixed
// action data from dq9-action-target-classification.csv is copied into fixed
// 1024-entry constexpr tables. The expensive "does any ROM-mined free-camera
// trigger source exist for this DQ9 action?" reduction is also performed here,
// once, from BACT + all actor memberships + fallback memberships.
// No BattleEmulator common-ID mapping lives in this generator.
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
  ["kRendererMetadataBytes", "freecam-renderer-metadata.bin"],
];

function formatBytes(bytes) {
  const lines = [];
  const perLine = 32;
  for (let offset = 0; offset < bytes.length; offset += perLine) {
    lines.push(`    ${[...bytes.subarray(offset, offset + perLine)].join(", ")}`);
  }
  return lines.join(",\n");
}

function parseActionClassification(csv) {
  const lines = csv.trimEnd().split(/\r?\n/);
  const header = lines[0].split(",");
  const actionIndex = header.indexOf("action_id_decimal");
  const actionNameIndex = header.indexOf("action_name");
  const sideIndex = header.indexOf("target_side_code");
  const scopeIndex = header.indexOf("target_scope_code");
  const repeatModeIndex = header.indexOf("repeat_mode_code");
  const operationTypeIndex = header.indexOf("operation_type");
  const resourceCostIndex = header.indexOf("resource_cost");
  const judgment1Index = header.indexOf("target_handler_judgment1");
  const judgment2Index = header.indexOf("target_handler_judgment2");
  if ([
    actionIndex, actionNameIndex, sideIndex, scopeIndex, repeatModeIndex, operationTypeIndex,
    resourceCostIndex, judgment1Index, judgment2Index,
  ].some((index) => index < 0)) {
    throw new Error("dq9-action-target-classification.csv is missing required columns");
  }

  const present = new Uint8Array(actionCount);
  const targetSide = new Uint8Array(actionCount);
  const targetScope = new Uint8Array(actionCount);
  const repeatMode = new Uint8Array(actionCount);
  const operationType = new Uint8Array(actionCount);
  const resourceCost = new Uint8Array(actionCount);
  const targetHandlerJudgment1 = new Uint16Array(actionCount);
  const targetHandlerJudgment2 = new Uint16Array(actionCount);
  const psycheUpMatches = [];
  let mappedRows = 0;

  for (let lineIndex = 1; lineIndex < lines.length; ++lineIndex) {
    if (!lines[lineIndex]) continue;
    const fields = lines[lineIndex].split(",");
    const actionId = Number(fields[actionIndex]);
    const actionName = fields[actionNameIndex];
    const side = Number(fields[sideIndex]);
    const scope = Number(fields[scopeIndex]);
    const repeat = Number(fields[repeatModeIndex]);
    const operation = Number(fields[operationTypeIndex]);
    const cost = Number(fields[resourceCostIndex]);
    const judgment1 = Number(fields[judgment1Index]);
    const judgment2 = Number(fields[judgment2Index]);
    if (!Number.isInteger(actionId) || actionId < 0 || actionId >= actionCount) {
      throw new Error(`invalid action_id_decimal at CSV line ${lineIndex + 1}`);
    }
    if (!Number.isInteger(side) || side < 0 || side > 255) {
      throw new Error(`invalid target_side_code at CSV line ${lineIndex + 1}`);
    }
    if (!Number.isInteger(scope) || scope < 0 || scope > 255) {
      throw new Error(`invalid target_scope_code at CSV line ${lineIndex + 1}`);
    }
    for (const [name, value, max] of [
      ["repeat_mode_code", repeat, 255],
      ["operation_type", operation, 255],
      ["resource_cost", cost, 255],
      ["target_handler_judgment1", judgment1, 0xffff],
      ["target_handler_judgment2", judgment2, 0xffff],
    ]) {
      if (!Number.isInteger(value) || value < 0 || value > max) {
        throw new Error(`invalid ${name} at CSV line ${lineIndex + 1}`);
      }
    }
    present[actionId] = 1;
    targetSide[actionId] = side;
    targetScope[actionId] = scope;
    repeatMode[actionId] = repeat;
    operationType[actionId] = operation;
    resourceCost[actionId] = cost;
    targetHandlerJudgment1[actionId] = judgment1;
    targetHandlerJudgment2[actionId] = judgment2;
    if (actionName === "ためる" && side === 2 && scope === 1) {
      psycheUpMatches.push(actionId);
    }
    ++mappedRows;
  }

  if (psycheUpMatches.length !== 1) {
    throw new Error(`expected exactly one ally/self ためる action, found ${psycheUpMatches.length}`);
  }

  return {
    present,
    targetSide,
    targetScope,
    repeatMode,
    operationType,
    resourceCost,
    targetHandlerJudgment1,
    targetHandlerJudgment2,
    psycheUpActionId: psycheUpMatches[0],
    mappedRows,
  };
}

function parseSimpleCsv(csv) {
  const lines = csv.trimEnd().split(/\r?\n/);
  const header = lines[0].split(",");
  return lines.slice(1).filter(Boolean).map((line) => {
    const fields = line.split(",");
    return Object.fromEntries(header.map((name, index) => [name, fields[index] ?? ""]));
  });
}

function parseFreeCameraBehavior(csv) {
  const behavior = new Uint8Array(actionCount);
  let rows = 0;
  for (const row of parseSimpleCsv(csv)) {
    const actionId = Number(row.action_id_decimal);
    if (!Number.isInteger(actionId) || actionId < 0 || actionId >= actionCount) {
      throw new Error("freecam-action-trigger-table.csv contains invalid action_id_decimal");
    }
    let code = 0;
    switch (row.camera_cpp_behavior) {
      case "free_camera_candidate": code = 1; break;
      case "tracking_camera_one_rng": code = 2; break;
      case "no_free_camera_control": code = 3; break;
      case "state_dependent": code = 4; break;
      default: code = 0; break;
    }
    behavior[actionId] = code;
    ++rows;
  }
  return { behavior, rows };
}

function formatU16(values) {
  const lines = [];
  const perLine = 24;
  for (let offset = 0; offset < values.length; offset += perLine) {
    lines.push(`    ${[...values.subarray(offset, offset + perLine)].join(", ")}`);
  }
  return lines.join(",\n");
}

function presentationExpansionDepths(monsterPresentationMetadata) {
  const capacity = monsterPresentationMetadata.readUInt32LE(8);
  const depths = new Set([0]);
  for (let monsterId = 0; monsterId < capacity; ++monsterId) {
    const depth = monsterPresentationMetadata[20 + monsterId];
    if (depth !== 0xff) depths.add(depth);
  }
  return [...depths].sort((a, b) => a - b);
}

function emitPaintWalk(lines, {
  origin,
  maximumDistance,
  holdDepth,
  currentDistance,
  level,
  indent,
}) {
  const remainingHold = holdDepth === 0 ? 0 : holdDepth - 1;
  const nextDistance = remainingHold === 0 ? currentDistance + 1 : currentDistance;
  const node = `node${level}`;
  lines.push(`${indent}for (const std::uint8_t ${node} : PresentationNeighbors(${origin})) {`);
  lines.push(`${indent}    if (${node} == kInvalidPresentationNode) continue;`);
  if (remainingHold === 0) {
    lines.push(`${indent}    if (occupancy[${node}] >= 0xf2) continue;`);
  }
  if (nextDistance < maximumDistance) {
    emitPaintWalk(lines, {
      origin: node,
      maximumDistance,
      holdDepth: remainingHold,
      currentDistance: nextDistance,
      level: level + 1,
      indent: `${indent}    `,
    });
  }
  if (remainingHold === 0) {
    lines.push(
      `${indent}    if (occupancy[${node}] == 0 || occupancy[${node}] > ${nextDistance}) `
      + `occupancy[${node}] = ${nextDistance};`,
    );
  }
  lines.push(`${indent}}`);
}

function emitPaintKernel(depth, adjacentOnly) {
  const holdDepth = depth === 0 ? 1 : depth + 1;
  const maximumDistance = adjacentOnly ? 1 : (depth === 0 ? 2 : depth + 2);
  const suffix = adjacentOnly ? "Adjacent" : "Full";
  const lines = [
    `DQ9_FREECAM_FORCE_INLINE void PaintPresentationDistanceGenerated${suffix}${depth}(`,
    "    PresentationOccupancyMap& occupancy,",
    "    const std::uint8_t origin",
    ") noexcept {",
  ];
  emitPaintWalk(lines, {
    origin: "origin",
    maximumDistance,
    holdDepth,
    currentDistance: 0,
    level: 0,
    indent: "    ",
  });
  lines.push("}", "");
  return lines;
}

function emitClassWalk(lines, {
  origin,
  maximumDepth,
  holdDepth,
  currentDepth,
  level,
  indent,
}) {
  if (maximumDepth === currentDepth) return;
  const remainingHold = holdDepth === 0 ? 0 : holdDepth - 1;
  const nextDepth = remainingHold === 0 ? currentDepth + 1 : currentDepth;
  const node = `node${level}`;
  lines.push(`${indent}for (const std::uint8_t ${node} : PresentationNeighbors(${origin})) {`);
  lines.push(`${indent}    if (${node} == kInvalidPresentationNode) continue;`);
  if (remainingHold === 0) {
    lines.push(`${indent}    if (occupancy[${node}] >= 0xf2) continue;`);
  }
  if (nextDepth < maximumDepth) {
    emitClassWalk(lines, {
      origin: node,
      maximumDepth,
      holdDepth: remainingHold,
      currentDepth: nextDepth,
      level: level + 1,
      indent: `${indent}    `,
    });
  }
  if (remainingHold === 0) {
    lines.push(`${indent}    occupancy[${node}] = presentationClass;`);
  }
  lines.push(`${indent}}`);
}

function emitClassKernel(depth, held) {
  const holdDepth = held ? (depth === 0 ? 1 : depth + 1) : 0;
  const suffix = held ? "Held" : "Footprint";
  const lines = [
    `DQ9_FREECAM_FORCE_INLINE void PaintPresentationClassGenerated${suffix}${depth}(`,
    "    PresentationOccupancyMap& occupancy,",
    "    const std::uint8_t origin,",
    "    const std::uint8_t presentationClass",
    ") noexcept {",
  ];
  emitClassWalk(lines, {
    origin: "origin",
    maximumDepth: depth,
    holdDepth,
    currentDepth: 0,
    level: 0,
    indent: "    ",
  });
  lines.push("}", "");
  return lines;
}

function buildPresentationClassSource(depths) {
  const lines = [
    "// Generated by build_freecam_fast_generated.mjs. Do not hand-edit.",
    "// This compiles geometry-depth metadata only; free-camera trigger eligibility remains runtime-gated.",
    "#if defined(_MSC_VER)",
    "#define DQ9_FREECAM_FORCE_INLINE __forceinline",
    "#elif defined(__GNUC__) || defined(__clang__)",
    "#define DQ9_FREECAM_FORCE_INLINE inline __attribute__((always_inline))",
    "#else",
    "#define DQ9_FREECAM_FORCE_INLINE inline",
    "#endif",
    "",
  ];
  for (const depth of depths) {
    lines.push(...emitClassKernel(depth, false));
    lines.push(...emitClassKernel(depth, true));
  }
  lines.push(
    "template <bool Held>",
    "DQ9_FREECAM_FORCE_INLINE void PaintPresentationClassForExpansion(",
    "    PresentationOccupancyMap& occupancy,",
    "    const std::uint8_t origin,",
    "    const std::uint8_t presentationClass,",
    "    const std::uint8_t expansion",
    ") noexcept {",
    "    switch (expansion) {",
  );
  for (const depth of depths) {
    lines.push(
      `        case ${depth}:`,
      "            if constexpr (Held) {",
      `                PaintPresentationClassGeneratedHeld${depth}(occupancy, origin, presentationClass);`,
      "            } else {",
      `                PaintPresentationClassGeneratedFootprint${depth}(occupancy, origin, presentationClass);`,
      "            }",
      "            return;",
    );
  }
  lines.push(
    "        default:",
    "            break;",
    "    }",
    "    const std::uint8_t holdDepth = Held",
    "        ? (expansion == 0 ? UINT8_C(1) : static_cast<std::uint8_t>(expansion + 1))",
    "        : UINT8_C(0);",
    "    PaintPresentationClass(occupancy, origin, presentationClass, expansion, holdDepth);",
    "}",
    "",
    "#undef DQ9_FREECAM_FORCE_INLINE",
    "",
  );
  return lines.join("\n");
}

function buildPresentationPaintSource(depths) {
  const lines = [
    "// Generated by build_freecam_fast_generated.mjs. Do not hand-edit.",
    "#if defined(_MSC_VER)",
    "#define DQ9_FREECAM_FORCE_INLINE __forceinline",
    "#elif defined(__GNUC__) || defined(__clang__)",
    "#define DQ9_FREECAM_FORCE_INLINE inline __attribute__((always_inline))",
    "#else",
    "#define DQ9_FREECAM_FORCE_INLINE inline",
    "#endif",
    "",
  ];
  for (const depth of depths) {
    lines.push(...emitPaintKernel(depth, false));
    lines.push(...emitPaintKernel(depth, true));
  }
  lines.push(
    "template <bool AdjacentOnly>",
    "DQ9_FREECAM_FORCE_INLINE void PaintPresentationDistanceForExpansion(",
    "    PresentationOccupancyMap& occupancy,",
    "    const std::uint8_t origin,",
    "    const std::uint8_t expansion",
    ") noexcept {",
    "    switch (expansion) {",
  );
  for (const depth of depths) {
    lines.push(
      `        case ${depth}:`,
      "            if constexpr (AdjacentOnly) {",
      `                PaintPresentationDistanceGeneratedAdjacent${depth}(occupancy, origin);`,
      "            } else {",
      `                PaintPresentationDistanceGeneratedFull${depth}(occupancy, origin);`,
      "            }",
      "            return;",
    );
  }
  lines.push(
    "        default:",
    "            break;",
    "    }",
    "    const std::uint8_t maximumLayer = expansion == 0",
    "        ? 2",
    "        : static_cast<std::uint8_t>(expansion + 2);",
    "    const std::uint8_t holdDepth = expansion == 0",
    "        ? 1",
    "        : static_cast<std::uint8_t>(expansion + 1);",
    "    PaintPresentationDistance(",
    "        occupancy,",
    "        origin,",
    "        AdjacentOnly ? UINT8_C(1) : maximumLayer,",
    "        holdDepth",
    "    );",
    "}",
    "",
    "#undef DQ9_FREECAM_FORCE_INLINE",
    "",
  );
  return lines.join("\n");
}

function buildHasAnyMinedFreeCameraTriggerSource(cameraMetadata, actionMetadata, membershipMetadata) {
  const result = new Uint8Array(actionCount);
  const actorProfileCount = membershipMetadata.readUInt32LE(12);
  const headerSize = 32;
  const actorCellsBytes = actorProfileCount * actionCount * 8;
  const fallbackCellsOffset = headerSize + actorCellsBytes;

  const hasBact = (actionId) =>
    ((cameraMetadata[16 + (actionId >> 3)] >> (actionId & 7)) & 1) !== 0;

  const fallbackLookupActionId = (actionId) =>
    actionMetadata.readUInt16LE(20 + actionId * 2);
  const membershipPresentAt = (offset) => membershipMetadata.readUInt16LE(offset + 4) !== 0;

  for (let actionId = 0; actionId < actionCount; ++actionId) {
    if (hasBact(actionId)) {
      result[actionId] = 1;
      continue;
    }
    let present = false;
    for (let profile = 0; profile < actorProfileCount; ++profile) {
      const cell = profile * actionCount + actionId;
      if (membershipPresentAt(headerSize + cell * 8)) {
        present = true;
        break;
      }
    }
    if (!present) {
      const fallbackId = fallbackLookupActionId(actionId);
      if (fallbackId !== 0xffff && fallbackId < actionCount) {
        present = membershipPresentAt(fallbackCellsOffset + fallbackId * 8);
      }
    }
    result[actionId] = present ? 1 : 0;
  }
  return result;
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
const inputBytes = new Map();
for (const [name, file] of inputs) {
  const bytes = await readFile(path.join(root, file));
  inputBytes.set(file, bytes);
  summary[file] = bytes.length;
  chunks.push(`inline constexpr std::array<std::uint8_t, ${bytes.length}> ${name} = {`);
  chunks.push(formatBytes(bytes));
  chunks.push("};", "");
}

const targetCsvName = "dq9-action-target-classification.csv";
const targetCsv = await readFile(path.join(root, targetCsvName), "utf8");
const classification = parseActionClassification(targetCsv);
const {
  present,
  targetSide,
  targetScope,
  repeatMode,
  operationType,
  resourceCost,
  targetHandlerJudgment1,
  targetHandlerJudgment2,
  psycheUpActionId,
  mappedRows,
} = classification;
const actionMetadata = inputBytes.get("freecam-action-metadata.bin");
const presentationTypeOffset = actionMetadata.readUInt32LE(16);
if (presentationTypeOffset + actionCount > actionMetadata.length) {
  throw new Error("freecam-action-metadata.bin presentation-type table is truncated");
}
const psycheUpPresentationType = actionMetadata[presentationTypeOffset + psycheUpActionId];
const psycheUpPresentationTypeActionIds = [];
for (let actionId = 0; actionId < actionCount; ++actionId) {
  if (actionMetadata[presentationTypeOffset + actionId] === psycheUpPresentationType) {
    psycheUpPresentationTypeActionIds.push(actionId);
  }
}
const tensionGainPresentationType = psycheUpPresentationType;
const tensionGainPresentationTypeActionIds = psycheUpPresentationTypeActionIds;

const rendererMetadata = inputBytes.get("freecam-renderer-metadata.bin");
if (rendererMetadata.length !== 12 || rendererMetadata.subarray(0, 4).toString("ascii") !== "FCMR") {
  throw new Error("freecam-renderer-metadata.bin has an invalid header");
}
if (rendererMetadata.readUInt32LE(4) !== 1) {
  throw new Error("freecam-renderer-metadata.bin has an unsupported version");
}
const tensionHudRendererFontKind = rendererMetadata[8];
const tensionHudRendererGlyphWidth = rendererMetadata[9];
const tensionHudRendererGlyphHeight = rendererMetadata[10];
const tensionHudRendererDrawNibble = rendererMetadata[11];
const tensionHudRendererResiduePrefixMask =
  0x03
  | (tensionHudRendererGlyphWidth !== 0 ? 0x04 : 0)
  | (tensionHudRendererDrawNibble !== 0 ? 0x08 : 0);
const hasAnyMinedFreeCameraTriggerSource = buildHasAnyMinedFreeCameraTriggerSource(
  inputBytes.get("freecam-camera-metadata.bin"),
  inputBytes.get("freecam-action-metadata.bin"),
  inputBytes.get("freecam-membership-metadata.bin"),
);
const triggerTablePath = path.resolve(root, "freecam-action-trigger-table.csv");
const triggerTableCsv = await readFile(triggerTablePath, "utf8");
const { behavior: cameraBehavior, rows: triggerRows } = parseFreeCameraBehavior(triggerTableCsv);
const freeCameraMapperAllowed = new Uint8Array(actionCount);
for (let actionId = 0; actionId < actionCount; ++actionId) {
  const behavior = cameraBehavior[actionId];
  const behaviorCanUseFreeCamera = behavior === 1 || behavior === 4;
  freeCameraMapperAllowed[actionId] =
    behaviorCanUseFreeCamera && hasAnyMinedFreeCameraTriggerSource[actionId] !== 0 ? 1 : 0;
}
chunks.push(`inline constexpr std::array<std::uint8_t, ${actionCount}> kActionClassificationPresent = {`);
chunks.push(formatBytes(present));
chunks.push("};", "");
chunks.push(`inline constexpr std::array<std::uint8_t, ${actionCount}> kTargetSideCode = {`);
chunks.push(formatBytes(targetSide));
chunks.push("};", "");
chunks.push(`inline constexpr std::array<std::uint8_t, ${actionCount}> kTargetScopeCode = {`);
chunks.push(formatBytes(targetScope));
chunks.push("};", "");
chunks.push(`inline constexpr std::array<std::uint8_t, ${actionCount}> kRepeatModeCode = {`);
chunks.push(formatBytes(repeatMode));
chunks.push("};", "");
chunks.push(`inline constexpr std::array<std::uint8_t, ${actionCount}> kOperationTypeCode = {`);
chunks.push(formatBytes(operationType));
chunks.push("};", "");
chunks.push(`inline constexpr std::uint16_t kPsycheUpActionId = UINT16_C(${psycheUpActionId});`);
chunks.push(`inline constexpr std::uint8_t kTensionGainPresentationType = UINT8_C(${tensionGainPresentationType});`);
chunks.push(`inline constexpr std::uint8_t kTensionHudRendererFontKind = UINT8_C(${tensionHudRendererFontKind});`);
chunks.push(`inline constexpr std::uint8_t kTensionHudRendererGlyphWidth = UINT8_C(${tensionHudRendererGlyphWidth});`);
chunks.push(`inline constexpr std::uint8_t kTensionHudRendererGlyphHeight = UINT8_C(${tensionHudRendererGlyphHeight});`);
chunks.push(`inline constexpr std::uint8_t kTensionHudRendererDrawNibble = UINT8_C(${tensionHudRendererDrawNibble});`);
chunks.push(`inline constexpr std::uint8_t kTensionHudRendererResiduePrefixMask = UINT8_C(${tensionHudRendererResiduePrefixMask});`);
chunks.push("");
chunks.push(`inline constexpr std::array<std::uint8_t, ${actionCount}> kResourceCost = {`);
chunks.push(formatBytes(resourceCost));
chunks.push("};", "");
chunks.push(`inline constexpr std::array<std::uint16_t, ${actionCount}> kTargetHandlerJudgment1 = {`);
chunks.push(formatU16(targetHandlerJudgment1));
chunks.push("};", "");
chunks.push(`inline constexpr std::array<std::uint16_t, ${actionCount}> kTargetHandlerJudgment2 = {`);
chunks.push(formatU16(targetHandlerJudgment2));
chunks.push("};", "");
chunks.push(`inline constexpr std::array<std::uint8_t, ${actionCount}> kHasAnyMinedFreeCameraTriggerSource = {`);
chunks.push(formatBytes(hasAnyMinedFreeCameraTriggerSource));
chunks.push("};", "");
chunks.push(`inline constexpr std::array<std::uint8_t, ${actionCount}> kCameraBehaviorCode = {`);
chunks.push(formatBytes(cameraBehavior));
chunks.push("};", "");
chunks.push(`inline constexpr std::array<std::uint8_t, ${actionCount}> kFreeCameraMapperAllowed = {`);
chunks.push(formatBytes(freeCameraMapperAllowed));
chunks.push("};", "");
chunks.push("} // namespace dq9::freecam::generated", "");

const output = chunks.join("\n");
const outputPath = path.join(root, "freecam_fast_generated.hpp");
await writeFile(outputPath, output, "utf8");
const paintDepths = presentationExpansionDepths(
  inputBytes.get("freecam-monster-presentation-metadata.bin"),
);
const classOutput = buildPresentationClassSource(paintDepths);
const classOutputPath = path.join(root, "freecam_class_generated.hpp");
await writeFile(classOutputPath, classOutput, "utf8");
const paintOutput = buildPresentationPaintSource(paintDepths);
const paintOutputPath = path.join(root, "freecam_paint_generated.hpp");
await writeFile(paintOutputPath, paintOutput, "utf8");
console.log(JSON.stringify({
  output: outputPath,
  sourceBytes: Buffer.byteLength(output),
  presentationPaint: {
    output: paintOutputPath,
    sourceBytes: Buffer.byteLength(paintOutput),
    expansionDepths: paintDepths,
  },
  presentationClass: {
    output: classOutputPath,
    sourceBytes: Buffer.byteLength(classOutput),
    expansionDepths: paintDepths,
  },
  inputs: summary,
  actionClassification: {
    file: targetCsvName,
    mappedRows,
    actionCount,
    psycheUpActionId,
    tensionGainPresentationType,
    tensionGainPresentationTypeActionIds,
    tensionHudRenderer: {
      fontKind: tensionHudRendererFontKind,
      glyphWidth: tensionHudRendererGlyphWidth,
      glyphHeight: tensionHudRendererGlyphHeight,
      drawNibble: tensionHudRendererDrawNibble,
      residuePrefixMask: tensionHudRendererResiduePrefixMask,
    },
  },
  triggerTable: { file: triggerTablePath, rows: triggerRows },
  minedFreeCameraTriggerCandidates: hasAnyMinedFreeCameraTriggerSource.reduce((sum, value) => sum + value, 0),
  freeCameraMapperAllowed: freeCameraMapperAllowed.reduce((sum, value) => sum + value, 0),
}, null, 2));
