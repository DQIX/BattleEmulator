// @script-id roster_row4_consumer_sweep
//
// ROM consumer probe for overlay26:021E08BC.
// Arm it before the desired presentation setup.  It pauses at 021E094C,
// immediately after 021E1958 has built the physical 12-byte rows and before
// the first row+4 consumer.  applyMask writes native ARM u32 values to each
// physical row+4.  Restore the captured emulator baseline between masks so all
// 2^N zero/nonzero masks exercise the same consumer input state.

const CPU = "arm9";
const ROW_BUILD_RETURN = 0x021e094c;
const GOAL_SETUP_TERMINAL = 0x021e0f40;
const NORMAL_CONSUMER = 0x021e1fd8;
const FALLBACK_CONSUMER = 0x021e2664;
const ROW4_NONZERO_BRANCH = 0x021e0ce8;
const ROW4_ZERO_BRANCH = 0x021e0cf8;
const ROW_OFFSET_FROM_SP = 0x15c;
const ROW_STRIDE = 12;
const MAX_ROWS = 12;

let armState = null;
let capture = null;
let activeRun = null;
let results = [];

function requireBlocking(context, name) {
  if (!context || !context.blocking) throw new Error(`${name} requires blocking:true`);
}

function u32(value) {
  return Number(value) >>> 0;
}

function swap32(value) {
  const v = u32(value);
  return u32(
    ((v & 0x000000ff) << 24) |
    ((v & 0x0000ff00) << 8) |
    ((v & 0x00ff0000) >>> 8) |
    ((v & 0xff000000) >>> 24)
  );
}

function parseU32(value, name) {
  const parsed = typeof value === "string" ? Number(BigInt(value)) : Number(value);
  if (!Number.isInteger(parsed) || parsed < 0 || parsed > 0xffffffff) {
    throw new Error(`${name} must be an unsigned 32-bit integer or integer string`);
  }
  return u32(parsed);
}

function hex32(value) {
  return `0x${u32(value).toString(16).padStart(8, "0")}`;
}

async function reg(name) {
  return u32(await memory.getregister(name, CPU));
}

async function native32(address) {
  return swap32(await memory.read32(u32(address), CPU));
}

async function writeNative32(address, value) {
  await memory.writedword(u32(address), swap32(value), CPU);
}

async function readBytes(address, length) {
  const bytes = [];
  for (let index = 0; index < length; index++) {
    bytes.push(await memory.read8(u32(address + index), CPU));
  }
  return bytes;
}

async function snapshotRowAt(rowAddress, rowIndex) {
  const object = await native32(rowAddress + 8);
  const presentation = object === 0 ? 0 : await native32(object + 0x13c);
  const nodes = presentation === 0 ? null : {
    start: await memory.read8(presentation + 0x1c, CPU),
    goal: await memory.read8(presentation + 0x1d, CPU),
    auxiliary: await memory.read8(presentation + 0x1e, CPU),
    target: await memory.read8(presentation + 0x1f, CPU),
    routeCount: await memory.read8(presentation + 0x34, CPU),
  };
  return {
    rowIndex,
    rowAddress: hex32(rowAddress),
    actorId: await memory.read8(rowAddress, CPU),
    presentationClass: await memory.read8(rowAddress + 1, CPU),
    field4: hex32(await native32(rowAddress + 4)),
    object: hex32(object),
    presentation: hex32(presentation),
    nodes,
    rawRow: await readBytes(rowAddress, ROW_STRIDE),
  };
}

async function snapshotRows(rowBase, count) {
  const rows = [];
  for (let index = 0; index < count; index++) {
    rows.push(await snapshotRowAt(rowBase + index * ROW_STRIDE, index));
  }
  return rows;
}

function rowIndexForPointer(rowPointer) {
  if (!capture) return null;
  const delta = u32(rowPointer - capture.rowBase);
  if ((delta % ROW_STRIDE) !== 0) return null;
  const index = delta / ROW_STRIDE;
  return index >= 0 && index < capture.rowCount ? index : null;
}

async function recordConsumer(kind) {
  if (!activeRun || !capture) return;
  const rowPointer = await reg("r0");
  const rowIndex = rowIndexForPointer(rowPointer);
  activeRun.consumers.push({
    sequence: activeRun.consumers.length,
    kind,
    pc: hex32(await reg("pc")),
    lr: hex32(await reg("r14")),
    rowIndex,
    row: rowIndex === null ? null : await snapshotRowAt(rowPointer, rowIndex),
  });
}

async function recordRow4Decision(kind) {
  if (!activeRun || !capture) return;
  const rowPointer = await reg("r10");
  const rowIndex = rowIndexForPointer(rowPointer);
  activeRun.decisions.push({
    sequence: activeRun.decisions.length,
    kind,
    pc: hex32(await reg("pc")),
    rowIndex,
    row: rowIndex === null ? null : await snapshotRowAt(rowPointer, rowIndex),
  });
}

memory.registerexec(ROW_BUILD_RETURN, async () => {
  if (!armState) return;
  if (armState.hitsSeen < armState.skipHits) {
    armState.hitsSeen++;
    return;
  }

  const rowCount = await reg("r0");
  if (rowCount < 1 || rowCount > MAX_ROWS) {
    throw new Error(`021E1958 returned invalid row count ${rowCount}`);
  }
  const sp = await reg("r13");
  const rowBase = u32(sp + ROW_OFFSET_FROM_SP);
  capture = {
    pc: hex32(ROW_BUILD_RETURN),
    sp,
    spHex: hex32(sp),
    rowBase,
    rowBaseHex: hex32(rowBase),
    rowCount,
    controlledRows: Math.min(rowCount, armState.controlledRows),
    originalRows: await snapshotRows(rowBase, rowCount),
  };
  armState = null;
  activeRun = null;
  results = [];
  print(`row+4 capture: base=${capture.rowBaseHex} rows=${rowCount} controlled=${capture.controlledRows}`);
  await mcp.call("pause", {});
}, { cpu: CPU });

memory.registerexec(NORMAL_CONSUMER, async () => {
  await recordConsumer("normal-021E1FD8");
}, { cpu: CPU });

memory.registerexec(FALLBACK_CONSUMER, async () => {
  await recordConsumer("fallback-021E2664");
}, { cpu: CPU });

memory.registerexec(ROW4_NONZERO_BRANCH, async () => {
  await recordRow4Decision("row4-nonzero");
}, { cpu: CPU });

memory.registerexec(ROW4_ZERO_BRANCH, async () => {
  await recordRow4Decision("row4-zero-or-missing-row");
}, { cpu: CPU });

memory.registerexec(GOAL_SETUP_TERMINAL, async () => {
  if (!activeRun || !capture) return;
  const completed = {
    mask: activeRun.mask,
    maskHex: `0x${activeRun.mask.toString(16)}`,
    controlledRows: activeRun.controlledRows,
    injectedValues: activeRun.injectedValues,
    before: activeRun.before,
    decisions: activeRun.decisions,
    consumers: activeRun.consumers,
    after: await snapshotRows(capture.rowBase, capture.rowCount),
    returnValue: await reg("r0"),
    terminalPc: hex32(GOAL_SETUP_TERMINAL),
  };
  results = results.filter((item) => item.mask !== completed.mask);
  results.push(completed);
  results.sort((left, right) => left.mask - right.mask);
  activeRun = null;
  print(`row+4 result: mask=${completed.maskHex} consumers=${completed.consumers.length}`);
  await mcp.call("pause", {});
}, { cpu: CPU });

async function armHandler(params, context) {
  requireBlocking(context, "arm");
  const controlledRows = params && params.controlledRows !== undefined
    ? Number(params.controlledRows)
    : 4;
  const skipHits = params && params.skipHits !== undefined ? Number(params.skipHits) : 0;
  if (!Number.isInteger(controlledRows) || controlledRows < 1 || controlledRows > MAX_ROWS) {
    throw new Error(`controlledRows must be an integer in 1..${MAX_ROWS}`);
  }
  if (!Number.isInteger(skipHits) || skipHits < 0) {
    throw new Error("skipHits must be a non-negative integer");
  }
  armState = { controlledRows, skipHits, hitsSeen: 0 };
  capture = null;
  activeRun = null;
  results = [];
  return { armed: true, controlledRows, skipHits };
}

async function applyMaskHandler(params, context) {
  requireBlocking(context, "applyMask");
  if (!capture) throw new Error("No captured 021E094C state; call arm, then enter one presentation setup");
  if (activeRun) throw new Error(`mask 0x${activeRun.mask.toString(16)} is still active`);

  const pc = await reg("pc");
  const sp = await reg("r13");
  if (pc !== ROW_BUILD_RETURN || sp !== capture.sp) {
    throw new Error(
      `Restore the captured baseline before applyMask; expected PC=${hex32(ROW_BUILD_RETURN)} SP=${capture.spHex}, got PC=${hex32(pc)} SP=${hex32(sp)}`
    );
  }

  const controlledRows = params && params.controlledRows !== undefined
    ? Number(params.controlledRows)
    : capture.controlledRows;
  if (!Number.isInteger(controlledRows) || controlledRows < 1 || controlledRows > capture.rowCount) {
    throw new Error(`controlledRows must be an integer in 1..${capture.rowCount}`);
  }
  if (controlledRows > 20) throw new Error("controlledRows above 20 cannot be represented by a numeric mask");

  const mask = Number(params && params.mask);
  const patternCount = 2 ** controlledRows;
  if (!Number.isInteger(mask) || mask < 0 || mask >= patternCount) {
    throw new Error(`mask must be an integer in 0..${patternCount - 1}`);
  }
  const commonNonzero = params && params.nonzeroValue !== undefined
    ? parseU32(params.nonzeroValue, "nonzeroValue")
    : null;
  if (commonNonzero === 0) throw new Error("nonzeroValue must not be zero");

  const before = await snapshotRows(capture.rowBase, capture.rowCount);
  const injectedValues = [];
  for (let index = 0; index < controlledRows; index++) {
    const isNonzero = ((mask >>> index) & 1) !== 0;
    const value = isNonzero
      ? (commonNonzero === null ? u32(0xa5000000 | (index + 1)) : commonNonzero)
      : 0;
    const address = capture.rowBase + index * ROW_STRIDE + 4;
    await writeNative32(address, value);
    const actual = await native32(address);
    if (actual !== value) {
      throw new Error(`row ${index} write verification failed: expected ${hex32(value)}, got ${hex32(actual)}`);
    }
    injectedValues.push({ rowIndex: index, address: hex32(address), value: hex32(value), nonzero: isNonzero });
  }

  activeRun = {
    mask,
    controlledRows,
    injectedValues,
    before,
    decisions: [],
    consumers: [],
  };
  return {
    readyToResume: true,
    mask,
    maskHex: `0x${mask.toString(16)}`,
    controlledRows,
    patternCount,
    injectedValues,
  };
}

async function applyNextMaskHandler(params, context) {
  requireBlocking(context, "applyNextMask");
  if (!capture) throw new Error("No captured 021E094C state");
  const controlledRows = params && params.controlledRows !== undefined
    ? Number(params.controlledRows)
    : capture.controlledRows;
  const completed = new Set(results.map((item) => item.mask));
  const patternCount = 2 ** controlledRows;
  let nextMask = 0;
  while (nextMask < patternCount && completed.has(nextMask)) nextMask++;
  if (nextMask >= patternCount) {
    return { complete: true, controlledRows, patternCount, completedMasks: results.length };
  }
  return applyMaskHandler({ ...params, controlledRows, mask: nextMask }, context);
}

async function statusHandler(_params, context) {
  requireBlocking(context, "status");
  const controlledRows = capture ? capture.controlledRows : null;
  const patternCount = controlledRows === null ? null : 2 ** controlledRows;
  return {
    armed: armState !== null,
    armState,
    capture,
    activeMask: activeRun ? activeRun.mask : null,
    controlledRows,
    patternCount,
    completedMasks: results.map((item) => item.mask),
    complete: patternCount !== null && results.length === patternCount,
  };
}

async function resultsHandler(_params, context) {
  requireBlocking(context, "results");
  return {
    capture,
    results,
    completedMasks: results.map((item) => item.mask),
  };
}

print("roster row+4 consumer sweep registered");

return [
  {
    name: "row4Arm",
    description: "Arms the next 021E094C capture. The ROM pauses after 021E1958 and before row+4 is consumed.",
    handler: armHandler,
  },
  {
    name: "row4ApplyMask",
    description: "At the restored 021E094C baseline, writes one exact physical-row zero/nonzero mask and verifies every native u32 write.",
    handler: applyMaskHandler,
  },
  {
    name: "row4ApplyNextMask",
    description: "Applies the first missing zero/nonzero mask after the caller restores the captured 021E094C baseline.",
    handler: applyNextMaskHandler,
  },
  {
    name: "row4Status",
    description: "Returns capture metadata, active mask, exhaustive mask count, and completed-mask list.",
    handler: statusHandler,
  },
  {
    name: "row4Results",
    description: "Returns full non-quantized before/consumer/after observations for every completed mask.",
    handler: resultsHandler,
  },
];
