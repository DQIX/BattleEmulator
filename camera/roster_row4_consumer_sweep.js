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
const FIRST_ROW_INITIALIZATION = 0x021e0980;
const GOAL_SETUP_TERMINAL = 0x021e0f40;
const NORMAL_CONSUMER = 0x021e1fd8;
const FALLBACK_CONSUMER = 0x021e2664;
const ROW4_NONZERO_BRANCH = 0x021e0ce8;
const ROW4_ZERO_BRANCH = 0x021e0cf8;
const ROW_OFFSET_FROM_SP = 0x15c;
const ROW_STRIDE = 12;
const MAX_ROWS = 12;
const CAPTURE_BASELINE_NAME = "roster-row4-consumer-at-021e094c";

let armState = null;
let capture = null;
let activeRun = null;
let results = [];
let naturalCaptureEnabled = false;
let naturalTurnSerial = -1;
let naturalActionIndex = -1;
let naturalLastActionIndex = -1;
let naturalCurrentAction = null;
let naturalActiveBuild = null;
let naturalResults = [];
let naturalTurnOrders = [];
let naturalExperimentMetadata = null;
let naturalExperiments = [];

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

async function native16(address) {
  const low = await memory.read8(u32(address), CPU);
  const high = await memory.read8(u32(address + 1), CPU);
  return (low | (high << 8)) >>> 0;
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
  const rowPointer = await reg("r0");
  if (activeRun && capture) {
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
  if (naturalActiveBuild) {
    const delta = u32(rowPointer - naturalActiveBuild.rowBase);
    const rowIndex = (delta % ROW_STRIDE) === 0 && delta / ROW_STRIDE < naturalActiveBuild.rowCount
      ? delta / ROW_STRIDE
      : null;
    naturalActiveBuild.consumers.push({
      sequence: naturalActiveBuild.consumers.length,
      kind,
      pc: hex32(await reg("pc")),
      lr: hex32(await reg("r14")),
      rowIndex,
      row: rowIndex === null ? null : await snapshotRowAt(rowPointer, rowIndex),
    });
  }
}

async function recordRow4Decision(kind) {
  const rowPointer = await reg("r10");
  if (activeRun && capture) {
    const rowIndex = rowIndexForPointer(rowPointer);
    activeRun.decisions.push({
      sequence: activeRun.decisions.length,
      kind,
      pc: hex32(await reg("pc")),
      rowIndex,
      row: rowIndex === null ? null : await snapshotRowAt(rowPointer, rowIndex),
    });
  }
  if (naturalActiveBuild) {
    const delta = u32(rowPointer - naturalActiveBuild.rowBase);
    const rowIndex = (delta % ROW_STRIDE) === 0 && delta / ROW_STRIDE < naturalActiveBuild.rowCount
      ? delta / ROW_STRIDE
      : null;
    naturalActiveBuild.decisions.push({
      sequence: naturalActiveBuild.decisions.length,
      kind,
      pc: hex32(await reg("pc")),
      rowIndex,
      row: rowIndex === null ? null : await snapshotRowAt(rowPointer, rowIndex),
    });
  }
}

memory.registerexec(0x021e08bc, async () => {
  if (!naturalCaptureEnabled) return;
  const controller = await reg("r0");
  const actionIndex = await native32(controller + 0x57c8);
  const combat = await native32(controller + 0x218);
  const actionStruct = u32(combat + 0x821c + actionIndex * 0x28);
  const actorRecord = await native32(actionStruct + 0x10);
  const targetRecord = await native32(actionStruct + 0x14);
  const newTurn = naturalTurnSerial < 0 || actionIndex <= naturalLastActionIndex;
  if (newTurn) naturalTurnSerial++;
  naturalLastActionIndex = actionIndex;
  naturalActionIndex = actionIndex;
  const actionCount = await native32(combat + 0x8e24);
  if (newTurn) {
    const order = [];
    for (let index = 0; index < actionCount; index++) {
      const record = u32(combat + 0x821c + index * 0x28);
      const recordActor = await native32(record + 0x10);
      const recordTarget = await native32(record + 0x14);
      order.push({
        actionIndex: index,
        dq9ActionId: await native16(record),
        actorId: recordActor === 0 ? null : await native16(recordActor + 0x20),
        targetId: recordTarget === 0 ? null : await native16(recordTarget + 0x0e),
      });
    }
    naturalTurnOrders[naturalTurnSerial] = order;
  }
  naturalCurrentAction = {
    actionIndex,
    actionCount,
    dq9ActionId: await native16(actionStruct),
    actorId: actorRecord === 0 ? null : await native16(actorRecord + 0x20),
    targetId: targetRecord === 0 ? null : await native16(targetRecord + 0x0e),
  };
}, { cpu: CPU });

memory.registerexec(ROW_BUILD_RETURN, async () => {
  const rowCount = await reg("r0");
  if (naturalCaptureEnabled) {
    if (rowCount < 1 || rowCount > MAX_ROWS) {
      throw new Error(`021E1958 returned invalid natural row count ${rowCount}`);
    }
    const sp = await reg("r13");
    const rowBase = u32(sp + ROW_OFFSET_FROM_SP);
    naturalActiveBuild = {
      sequence: naturalResults.length,
      turnSerial: naturalTurnSerial,
      action: naturalCurrentAction === null ? null : { ...naturalCurrentAction },
      presentationActionOrder: naturalTurnOrders[naturalTurnSerial]
        ? naturalTurnOrders[naturalTurnSerial].map((item) => ({ ...item }))
        : [],
      pc: hex32(ROW_BUILD_RETURN),
      sp: hex32(sp),
      rowBase,
      rowBaseHex: hex32(rowBase),
      rowCount,
      originalRows: await snapshotRows(rowBase, rowCount),
      decisions: [],
      consumers: [],
      after: null,
      returnValue: null,
      terminalPc: null,
    };
  }
  if (!armState) return;
  if (armState.hitsSeen < armState.skipHits) {
    armState.hitsSeen++;
    return;
  }

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
    baselineName: CAPTURE_BASELINE_NAME,
    originalRows: await snapshotRows(rowBase, rowCount),
  };
  armState = null;
  activeRun = null;
  results = [];
  print(`row+4 capture: base=${capture.rowBaseHex} rows=${rowCount} controlled=${capture.controlledRows}`);
  await mcp.call("pause", {});
  const saved = await mcp.call("saveAnalysisBaseline", {
    name: CAPTURE_BASELINE_NAME,
    replace: true,
  });
  if (!saved || saved.ok !== true) {
    throw new Error("failed to save the exact 021E094C consumer baseline");
  }
  print(`row+4 baseline saved: ${CAPTURE_BASELINE_NAME}`);
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
  if (naturalActiveBuild) {
    naturalActiveBuild.after = await snapshotRows(
      naturalActiveBuild.rowBase,
      naturalActiveBuild.rowCount
    );
    naturalActiveBuild.returnValue = await reg("r0");
    naturalActiveBuild.terminalPc = hex32(GOAL_SETUP_TERMINAL);
    naturalResults.push(naturalActiveBuild);
    naturalActiveBuild = null;
  }
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

async function adoptBaselineHandler(params, context) {
  requireBlocking(context, "adoptBaseline");
  const pc = await reg("pc");
  const sp = await reg("r13");
  const rowCount = await reg("r0");
  const beforeFirstConsumer = pc >= ROW_BUILD_RETURN && pc < FIRST_ROW_INITIALIZATION;
  if (!beforeFirstConsumer) {
    throw new Error(
      `restore ${CAPTURE_BASELINE_NAME} first; PC ${hex32(pc)} is not in the pre-consumer window`
    );
  }
  if (rowCount < 1 || rowCount > MAX_ROWS) {
    throw new Error(`pre-consumer baseline has invalid row count ${rowCount}`);
  }
  const controlledRows = params && params.controlledRows !== undefined
    ? Number(params.controlledRows)
    : Math.min(rowCount, 4);
  if (!Number.isInteger(controlledRows) || controlledRows < 1 || controlledRows > rowCount) {
    throw new Error(`controlledRows must be an integer in 1..${rowCount}`);
  }
  const rowBase = u32(sp + ROW_OFFSET_FROM_SP);
  capture = {
    pc: hex32(pc),
    sp,
    spHex: hex32(sp),
    rowBase,
    rowBaseHex: hex32(rowBase),
    rowCount,
    controlledRows,
    baselineName: CAPTURE_BASELINE_NAME,
    originalRows: await snapshotRows(rowBase, rowCount),
  };
  armState = null;
  activeRun = null;
  results = [];
  return { adopted: true, capture };
}

async function applyMaskHandler(params, context) {
  requireBlocking(context, "applyMask");
  if (!capture) throw new Error("No captured 021E094C state; call arm, then enter one presentation setup");
  if (activeRun) throw new Error(`mask 0x${activeRun.mask.toString(16)} is still active`);

  const pc = await reg("pc");
  const sp = await reg("r13");
  const beforeFirstConsumer = pc >= ROW_BUILD_RETURN && pc < FIRST_ROW_INITIALIZATION;
  if (!beforeFirstConsumer || sp !== capture.sp) {
    throw new Error(
      `Restore the captured pre-consumer baseline before applyMask; expected PC in ${hex32(ROW_BUILD_RETURN)}..${hex32(FIRST_ROW_INITIALIZATION - 4)} SP=${capture.spHex}, got PC=${hex32(pc)} SP=${hex32(sp)}`
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

async function naturalArmHandler(params, context) {
  requireBlocking(context, "naturalArm");
  naturalCaptureEnabled = true;
  naturalTurnSerial = -1;
  naturalActionIndex = -1;
  naturalLastActionIndex = -1;
  naturalCurrentAction = null;
  naturalActiveBuild = null;
  naturalResults = [];
  naturalTurnOrders = [];
  naturalExperimentMetadata = params?.experiment ?? null;
  return { armed: true, experiment: naturalExperimentMetadata };
}

function compactNaturalResult(item) {
  if (!item) return null;
  return {
    sequence: item.sequence,
    turnSerial: item.turnSerial,
    presentationAction: item.action,
    presentationActionOrder: item.presentationActionOrder,
    rowCount: item.rowCount,
    physicalRows: item.originalRows.map((row) => ({
      rowIndex: row.rowIndex,
      actorId: row.actorId,
      presentationClass: row.presentationClass,
      row4Raw: row.field4,
      row4Nonzero: row.field4 !== "0x00000000",
    })),
    consultedRows: item.decisions.map((decision) => ({
      sequence: decision.sequence,
      rowIndex: decision.rowIndex,
      actorId: decision.row ? decision.row.actorId : null,
      branch: decision.kind,
    })),
    consumers: item.consumers.map((consumer) => ({
      sequence: consumer.sequence,
      rowIndex: consumer.rowIndex,
      actorId: consumer.row ? consumer.row.actorId : null,
      kind: consumer.kind,
    })),
    resultingCameraState: (item.after ?? []).map((row) => ({
      rowIndex: row.rowIndex,
      actorId: row.actorId,
      nodes: row.nodes,
    })),
    terminalPc: item.terminalPc,
    returnValue: item.returnValue,
  };
}

function compactNaturalExperiment(metadata, items) {
  return {
    experiment: metadata,
    presentationActionOrder: items.length === 0 ? [] : items[0].presentationActionOrder,
    builds: items.map((item) => ({
      sequence: item.sequence,
      turnSerial: item.turnSerial,
      presentationAction: item.action,
      physicalRows: item.originalRows.map((row) => ({
        rowIndex: row.rowIndex,
        actorId: row.actorId,
        row4Raw: row.field4,
        row4Nonzero: row.field4 !== "0x00000000",
      })),
      consultedRows: item.decisions.map((decision) => ({
        rowIndex: decision.rowIndex,
        actorId: decision.row ? decision.row.actorId : null,
        branch: decision.kind,
      })),
      consumers: item.consumers.map((consumer) => ({
        rowIndex: consumer.rowIndex,
        actorId: consumer.row ? consumer.row.actorId : null,
        kind: consumer.kind,
      })),
      resultingCameraState: (item.after ?? []).map((row) => ({
        rowIndex: row.rowIndex,
        actorId: row.actorId,
        start: row.nodes?.start ?? null,
        goal: row.nodes?.goal ?? null,
        auxiliary: row.nodes?.auxiliary ?? null,
        target: row.nodes?.target ?? null,
        routeCount: row.nodes?.routeCount ?? null,
      })),
    })),
  };
}

async function naturalArchiveHandler(_params, context) {
  requireBlocking(context, "naturalArchive");
  const archived = compactNaturalExperiment(naturalExperimentMetadata, naturalResults);
  naturalExperiments.push(archived);
  naturalCaptureEnabled = false;
  naturalActiveBuild = null;
  return {
    archivedIndex: naturalExperiments.length - 1,
    experiment: archived.experiment,
    actionCount: archived.presentationActionOrder.length,
    buildCount: archived.builds.length,
  };
}

async function naturalExperimentsHandler(params, context) {
  requireBlocking(context, "naturalExperiments");
  if (params && params.index !== undefined) {
    const index = Number(params.index);
    if (!Number.isInteger(index) || index < 0) throw new Error("index must be a non-negative integer");
    return { index, result: naturalExperiments[index] ?? null };
  }
  return {
    count: naturalExperiments.length,
    experiments: naturalExperiments.map((item, index) => ({
      index,
      experiment: item.experiment,
      actionCount: item.presentationActionOrder.length,
      buildCount: item.builds.length,
    })),
  };
}

async function naturalResultsHandler(params, context) {
  requireBlocking(context, "naturalResults");
  if (params && params.sequence !== undefined) {
    const sequence = Number(params.sequence);
    if (!Number.isInteger(sequence) || sequence < 0) {
      throw new Error("sequence must be a non-negative integer");
    }
    return {
      armed: naturalCaptureEnabled,
      result: compactNaturalResult(
        naturalResults.find((item) => item.sequence === sequence) ?? null
      ),
    };
  }
  return {
    armed: naturalCaptureEnabled,
    activeBuild: naturalActiveBuild,
    resultCount: naturalResults.length,
    results: naturalResults.map(compactNaturalResult),
  };
}

async function naturalStopHandler(_params, context) {
  requireBlocking(context, "naturalStop");
  naturalCaptureEnabled = false;
  naturalActiveBuild = null;
  return { armed: false, resultCount: naturalResults.length };
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
    name: "row4AdoptBaseline",
    description: "Adopts the already-saved 021E094C pre-consumer baseline without executing another battle turn.",
    handler: adoptBaselineHandler,
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
  {
    name: "row4NaturalArm",
    description: "Clears and arms non-mutating capture of every natural 021E1958 physical row build, action identity, consulted row, and terminal state.",
    handler: naturalArmHandler,
  },
  {
    name: "row4NaturalResults",
    description: "Returns compact per-setup action identity, every physical row actor/raw row+4 value, consulted branches, and terminal camera nodes.",
    handler: naturalResultsHandler,
  },
  {
    name: "row4NaturalStop",
    description: "Stops natural row capture while retaining the completed results for retrieval.",
    handler: naturalStopHandler,
  },
  {
    name: "row4NaturalArchive",
    description: "Archives the current compact natural run across State restores for later matrix retrieval.",
    handler: naturalArchiveHandler,
  },
  {
    name: "row4NaturalExperiments",
    description: "Lists archived matrix runs, or returns one compact run by index.",
    handler: naturalExperimentsHandler,
  },
];
