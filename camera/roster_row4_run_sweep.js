// One-shot driver for roster_row4_consumer_sweep.js.
// Run only after row4Arm has paused the ROM at overlay26:021E094C.
// The persistent script performs every physical row+4 write and observation;
// this driver only restores the identical emulator baseline between masks.

const SCRIPT_NAME = "roster_row4_consumer_sweep";
const CONTROLLED_ROWS = 4;
const PER_MASK_TIMEOUT_MS = 120000;

function requireOk(result, operation) {
  if (!result || result.ok !== true) {
    const detail = result && result.error
      ? `${result.error.code || "ERROR"}: ${result.error.message || "unknown error"}`
      : "missing result";
    throw new Error(`${operation} failed: ${detail}`);
  }
  return result;
}

async function callProbe(name, params = {}) {
  const response = requireOk(await mcp.call("callPScriptMcp", {
    scriptName: SCRIPT_NAME,
    name,
    blocking: true,
    timeoutMs: PER_MASK_TIMEOUT_MS,
    params,
  }), name);
  return response.value;
}

const initial = await callProbe("row4Status");
if (!initial.capture) {
  throw new Error("row4Arm has not captured 021E094C yet");
}
if (initial.activeMask !== null) {
  throw new Error(`mask ${initial.activeMask} is already active`);
}
if (initial.capture.controlledRows !== CONTROLLED_ROWS) {
  throw new Error(
    `captured controlledRows=${initial.capture.controlledRows}; expected ${CONTROLLED_ROWS}`
  );
}
const baselineName = initial.capture.baselineName;
if (!baselineName) throw new Error("capture has no saved 021E094C baseline");

const patternCount = 2 ** CONTROLLED_ROWS;
for (let mask = 0; mask < patternCount; mask++) {
  requireOk(await mcp.call("restoreAnalysisBaseline", {
    name: baselineName,
  }), `restoreAnalysisBaseline mask=${mask}`);
  const applied = await callProbe("row4ApplyMask", {
    mask,
    controlledRows: CONTROLLED_ROWS,
  });
  if (!applied || applied.readyToResume !== true || applied.mask !== mask) {
    throw new Error(`row4ApplyMask returned an invalid result for mask ${mask}`);
  }

  requireOk(await mcp.call("resume", {}), `resume mask=${mask}`);
  const paused = requireOk(await mcp.call("waitForPause", {
    timeoutMs: PER_MASK_TIMEOUT_MS,
  }), `waitForPause mask=${mask}`);
  if (paused.paused !== true) {
    throw new Error(`mask ${mask} did not stop at the consumer terminal`);
  }

  const status = await callProbe("row4Status");
  if (!status.completedMasks.includes(mask)) {
    throw new Error(`mask ${mask} reached pause without a recorded terminal result`);
  }
}

requireOk(await mcp.call("restoreAnalysisBaseline", {
  name: baselineName,
}), "final restoreAnalysisBaseline");

const finalStatus = await callProbe("row4Status");
if (!finalStatus.complete || finalStatus.completedMasks.length !== patternCount) {
  throw new Error(
    `sweep incomplete: ${finalStatus.completedMasks.length}/${patternCount} masks`
  );
}

return {
  ok: true,
  scriptName: SCRIPT_NAME,
  baselineName,
  controlledRows: CONTROLLED_ROWS,
  patternCount,
  completedMasks: finalStatus.completedMasks,
  fullResultsMcp: "row4Results",
  restoredToCapture: true,
};
