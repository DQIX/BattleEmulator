const ui = {
  emulatorSelect: document.getElementById("emulatorSelect"),
  emulatorMeta: document.getElementById("emulatorMeta"),
  emulatorStatus: document.getElementById("emulatorStatus"),
  offsetSeconds: document.getElementById("offsetSeconds"),
  searchRangeSeconds: document.getElementById("searchRangeSeconds"),
  threads: document.getElementById("threads"),
  actionInput: document.getElementById("actionInput"),
  runButton: document.getElementById("runButton"),
  autoTimerPreview: document.getElementById("autoTimerPreview"),
  autoTimerStatus: document.getElementById("autoTimerStatus"),
  autoTimerStartButton: document.getElementById("autoTimerStartButton"),
  autoTimerUseButton: document.getElementById("autoTimerUseButton"),
  autoTimerFractionField: document.getElementById("autoTimerFractionField"),
  autoTimerFractionInput: document.getElementById("autoTimerFractionInput"),
  autoTimerResetButton: document.getElementById("autoTimerResetButton"),
  autoTimerResetConfirm: document.getElementById("autoTimerResetConfirm"),
  autoTimerResetConfirmButton: document.getElementById("autoTimerResetConfirmButton"),
  autoTimerResetCancelButton: document.getElementById("autoTimerResetCancelButton"),
  seedHex: document.getElementById("seedHex"),
  seedSpeed: document.getElementById("seedSpeed"),
  seedElapsed: document.getElementById("seedElapsed"),
  seedDrift: document.getElementById("seedDrift"),
  seedState: document.getElementById("seedState"),
  dumpOutput: document.getElementById("dumpOutput"),
  logOutput: document.getElementById("logOutput"),
  themeSelect: document.getElementById("themeSelect"),
  langSelect: document.getElementById("langSelect"),
  preloadToggle: document.getElementById("preloadToggle"),
  memoTableBody: document.getElementById("memoTableBody"),
  memoEmpty: document.getElementById("memoEmpty"),
  memoScroll: document.getElementById("memoScroll"),
  memoCopyMarkdown: document.getElementById("memoCopyMarkdown"),
  memoCopyCsv: document.getElementById("memoCopyCsv")
};

const DEFAULT_OFFSET_SECONDS = 15;
const OFFSET_STORAGE_KEY = "dq9OffsetSeconds";
const DEFAULT_SEARCH_RANGE_SECONDS = 6;
const SEARCH_RANGE_STORAGE_KEY = "dq9SearchRangeSeconds";
const AUTO_TIMER_MAX_SECONDS = 30 * 60 * 60;
const AUTO_TIMER_CORRECTION_LIMIT_MS = 20 * 60 * 1000;
const AUTO_TIMER_FRACTION_SCALE = 10000;
const AUTO_TIMER_FRACTION_HIDE_DELAY_MS = 5 * 60 * 1000;
const SEED_MEMO_STORAGE_KEY = "dq9SeedMemoList";
const SEED_MEMO_LIMIT = 200;
const SEED_TIME_SCALE = 100n;
const SEED_SECONDS_NUMERATOR = 10000n;
const SEED_SECONDS_DIVISOR = 799n;

const state = {
  emulators: [],
  active: null,
  running: false,
  lang: document.documentElement.dataset.lang || "ja",
  theme: document.documentElement.dataset.theme || "lightSepia",
  emulatorStatusKey: "idle",
  preload: localStorage.getItem("dq9Preload") === "1",
  offsetSeconds: DEFAULT_OFFSET_SECONDS,
  searchRangeSeconds: DEFAULT_SEARCH_RANGE_SECONDS,
  preloadQueue: Promise.resolve(),
  moduleCache: new Map(),
  workerScriptText: "",
  workerBlobUrl: "",
  memoList: [],
  urlOverrides: null,
  urlOverridesAppliedNonEmu: false,
  urlOverridesAppliedEmu: false,
  autoTimerAnchor: null,
  autoTimerTickHandle: null,
  autoTimerAppliedPrefix: "",
  autoTimerLastUse: null,
  autoTimerFractionHideHandle: null,
  autoTimerFractionSourceTimeText: "",
  autoTimerCorrectionCount: 0
};

const logLines = [];

function getDictionary() {
  const config = window.APP_CONFIG || {};
  return config.i18n ? config.i18n[state.lang] : null;
}

function t(key, fallback) {
  const dict = getDictionary();
  if (dict && dict[key]) {
    return dict[key];
  }
  return fallback;
}

function pad2(value) {
  return String(value).padStart(2, "0");
}

function formatInputTime(parsed) {
  return `${pad2(parsed.hours)}:${pad2(parsed.minutes)}:${pad2(parsed.seconds)}`;
}

function formatTimerPreview(totalSeconds) {
  if (!Number.isFinite(totalSeconds) || totalSeconds < 0) {
    return "--:--:--";
  }
  const clamped = Math.min(AUTO_TIMER_MAX_SECONDS, Math.floor(totalSeconds));
  const hours = Math.floor(clamped / 3600);
  const minutes = Math.floor((clamped % 3600) / 60);
  const seconds = clamped % 60;
  return `${pad2(hours)}:${pad2(minutes)}:${pad2(seconds)}`;
}

function formatActionTime(totalSeconds) {
  const clamped = Math.max(0, Math.min(AUTO_TIMER_MAX_SECONDS, Math.floor(totalSeconds)));
  const hours = Math.floor(clamped / 3600);
  const minutes = Math.floor((clamped % 3600) / 60);
  const seconds = clamped % 60;
  return `${hours} ${minutes} ${seconds}`;
}

function formatFractionDigits(value) {
  return String(value).padStart(4, "0");
}

function normalizeFractionDigits(value) {
  if (value === null || value === undefined || value === "") {
    return null;
  }
  const numeric = Number.parseInt(String(value), 10);
  if (!Number.isFinite(numeric)) {
    return null;
  }
  return Math.max(0, Math.min(AUTO_TIMER_FRACTION_SCALE - 1, numeric));
}

function splitPreciseSeconds(totalSeconds) {
  const safeSeconds = Math.max(0, Number.isFinite(totalSeconds) ? totalSeconds : 0);
  const totalUnits = Math.floor((safeSeconds + 1e-9) * AUTO_TIMER_FRACTION_SCALE);
  const wholeSeconds = Math.floor(totalUnits / AUTO_TIMER_FRACTION_SCALE);
  const fraction = totalUnits % AUTO_TIMER_FRACTION_SCALE;
  return {
    wholeSeconds,
    fraction
  };
}

function parsedToTotalSeconds(parsed) {
  if (!parsed) {
    return 0;
  }
  return parsed.hours * 3600 + parsed.minutes * 60 + parsed.seconds;
}

function getAutoTimerFractionDigits() {
  return normalizeFractionDigits(ui.autoTimerFractionInput ? ui.autoTimerFractionInput.value : "");
}

function clearAutoTimerFractionHideTimer() {
  if (state.autoTimerFractionHideHandle !== null) {
    clearTimeout(state.autoTimerFractionHideHandle);
    state.autoTimerFractionHideHandle = null;
  }
}

function updateAutoTimerFractionVisibility() {
  if (!ui.autoTimerFractionField) {
    return;
  }
  const digits = getAutoTimerFractionDigits();
  const hidden = digits === null || digits === 0;
  ui.autoTimerFractionField.classList.toggle("is-hidden", hidden);
  if (ui.autoTimerFractionInput) {
    ui.autoTimerFractionInput.disabled = hidden;
  }
}

function setAutoTimerFractionDigits(value, timeText = "") {
  if (!ui.autoTimerFractionInput) {
    return;
  }
  const digits = normalizeFractionDigits(value);
  ui.autoTimerFractionInput.value = digits === null ? "" : formatFractionDigits(digits);
  state.autoTimerFractionSourceTimeText = digits === null || digits === 0 ? "" : timeText;
  updateAutoTimerFractionVisibility();
}

function restartAutoTimerFractionHideTimer() {
  clearAutoTimerFractionHideTimer();
  state.autoTimerFractionHideHandle = setTimeout(() => {
    setAutoTimerFractionDigits(null, "");
  }, AUTO_TIMER_FRACTION_HIDE_DELAY_MS);
}

function extractInputTimeText(text) {
  const tokens = String(text || "").trim().split(/\s+/).filter(Boolean);
  if (tokens.length < 3) {
    return "";
  }
  return tokens.slice(0, 3).join(" ");
}

function formatDatePartsUTC(date) {
  const year = date.getUTCFullYear();
  const month = pad2(date.getUTCMonth() + 1);
  const day = pad2(date.getUTCDate());
  const hours = pad2(date.getUTCHours());
  const minutes = pad2(date.getUTCMinutes());
  const seconds = pad2(date.getUTCSeconds());
  return `${year}-${month}-${day} ${hours}:${minutes}:${seconds}`;
}

function formatRealTimeDisplay(savedAt) {
  if (!savedAt) {
    return "-";
  }
  const base = new Date(savedAt);
  if (Number.isNaN(base.getTime())) {
    return "-";
  }
  if (state.lang === "ja") {
    const jstDate = new Date(base.getTime() + 9 * 60 * 60 * 1000);
    return `${formatDatePartsUTC(jstDate)} JST`;
  }
  return `${formatDatePartsUTC(base)} UTC`;
}

async function hashStringSHA256(text) {
  const data = new TextEncoder().encode(text);
  const buf = await crypto.subtle.digest("SHA-256", data);
  const hashArray = Array.from(new Uint8Array(buf));
  return hashArray.map(b => b.toString(16).padStart(2, "0")).join("");
}


function loadSeedMemos() {
  const stored = localStorage.getItem(SEED_MEMO_STORAGE_KEY);
  if (!stored) {
    return [];
  }
  try {
    const parsed = JSON.parse(stored);
    return Array.isArray(parsed) ? parsed : [];
  } catch (err) {
    return [];
  }
}

function saveSeedMemos(list) {
  localStorage.setItem(SEED_MEMO_STORAGE_KEY, JSON.stringify(list));
}

async function buildMemoFingerprint(entry) {
  const payload = {
    input: entry.input,
    timeText: entry.timeText,
    driftText: entry.driftText || "",
    offsetSeconds: entry.offsetSeconds,
    searchRangeSeconds: entry.searchRangeSeconds
  };
  return await hashStringSHA256(JSON.stringify(payload));
}

function buildOverrideUrl(entry) {
  const url = new URL(window.location.href);
  url.search = "";
  url.searchParams.set("emu", entry.emulatorLabel);
  url.searchParams.set("offset", String(entry.offsetSeconds));
  url.searchParams.set("range", String(entry.searchRangeSeconds));
  url.searchParams.set("input", entry.input);
  return url.toString();
}

function copyText(text) {
  if (!text) {
    return;
  }
  if (navigator.clipboard && navigator.clipboard.writeText) {
    navigator.clipboard.writeText(text).catch(() => {});
    return;
  }
  const temp = document.createElement("textarea");
  temp.value = text;
  temp.style.position = "fixed";
  temp.style.opacity = "0";
  document.body.appendChild(temp);
  temp.focus();
  temp.select();
  try {
    document.execCommand("copy");
  } catch (err) {}
  document.body.removeChild(temp);
}

function appendLog(line) {
  const timestamp = new Date().toLocaleTimeString();
  logLines.push(`[${timestamp}] ${line}`);
  ui.logOutput.value = logLines.join("\n");
  ui.logOutput.scrollTop = ui.logOutput.scrollHeight;
}

function setSeedState(text, count) {
  const label = t(text, text);
  if (typeof count === "number") {
    ui.seedState.textContent = `${label} (${count})`;
  } else {
    ui.seedState.textContent = label;
  }
}

function renderSeedMemos(list) {
  if (!ui.memoTableBody || !ui.memoEmpty || !ui.memoScroll) {
    return;
  }
  ui.memoTableBody.innerHTML = "";
  if (!list.length) {
    ui.memoEmpty.hidden = false;
    ui.memoScroll.hidden = true;
    return;
  }
  ui.memoEmpty.hidden = true;
  ui.memoScroll.hidden = false;

  list.forEach((entry) => {
    entry.memo = entry.memo || "";
    const row = document.createElement("tr");
    row.className = "memo-row";

    const cellTime = document.createElement("td");
    cellTime.textContent = entry.timeText;

    const cellRealTime = document.createElement("td");
    cellRealTime.textContent = formatRealTimeDisplay(entry.savedAt);

    const cellEmu = document.createElement("td");
    cellEmu.textContent = entry.emulatorLabel;

    const cellOffset = document.createElement("td");
    cellOffset.textContent = String(entry.offsetSeconds);

    const cellRange = document.createElement("td");
    cellRange.textContent = String(entry.searchRangeSeconds);

    const cellDrift = document.createElement("td");
    cellDrift.textContent = entry.driftText || "-";

    const cellInput = document.createElement("td");
    cellInput.className = "memo-input";
    const inputText = document.createElement("span");
    inputText.textContent = entry.input;
    inputText.title = entry.input;
    const copyInput = document.createElement("a");
    copyInput.href = "#";
    copyInput.className = "memo-copy";
    copyInput.dataset.copyInput = entry.id;
    copyInput.textContent = t("memoCopyInput", "Copy input");
    cellInput.appendChild(inputText);
    cellInput.appendChild(copyInput);

    const cellNote = document.createElement("td");
    const noteInput = document.createElement("input");
    noteInput.type = "text";
    noteInput.className = "memo-note-input";
    noteInput.value = entry.memo;
    noteInput.dataset.memoId = entry.id;
    cellNote.appendChild(noteInput);

    const cellUrl = document.createElement("td");
    const copyUrl = document.createElement("a");
    copyUrl.href = buildOverrideUrl(entry);
    copyUrl.className = "memo-link";
    copyUrl.dataset.copyUrl = entry.id;
    copyUrl.textContent = t("memoCopyUrl", "Copy URL");
    cellUrl.appendChild(copyUrl);

    row.appendChild(cellTime);
    row.appendChild(cellRealTime);
    row.appendChild(cellEmu);
    row.appendChild(cellOffset);
    row.appendChild(cellRange);
    row.appendChild(cellDrift);
    row.appendChild(cellInput);
    row.appendChild(cellNote);
    row.appendChild(cellUrl);
    ui.memoTableBody.appendChild(row);
  });
}

async function recordSeedMemo(parsed, inputText, driftText) {
  if (!state.active || !parsed) {
    return;
  }
  const entry = {
    emulatorLabel: state.active.label,
    input: inputText,
    timeText: formatInputTime(parsed),
    driftText: driftText || "",
    offsetSeconds: state.offsetSeconds,
    searchRangeSeconds: state.searchRangeSeconds,
    savedAt: new Date().toISOString()
  };
  entry.id = await buildMemoFingerprint(entry);
  const exists = state.memoList.some((item) => item.id === entry.id);
  if (exists) {
    return;
  }
  state.memoList.unshift(entry);
  if (state.memoList.length > SEED_MEMO_LIMIT) {
    state.memoList.length = SEED_MEMO_LIMIT;
  }
  saveSeedMemos(state.memoList);
  renderSeedMemos(state.memoList);
}

function memoToMarkdown(list) {
  const header = ["Time", "Real", "Emu", "Offset", "Range", "Drift", "Input", "Memo"];
  const rows = list.map((entry) => [
    entry.timeText,
    formatRealTimeDisplay(entry.savedAt),
    entry.emulatorLabel,
    entry.offsetSeconds,
    entry.searchRangeSeconds,
    entry.driftText || "-",
    entry.input.replace(/\n/g, " "),
    entry.memo || ""
  ]);
  const lines = [
    `| ${header.join(" | ")} |`,
    `| ${header.map(() => "---").join(" | ")} |`,
    ...rows.map((row) => `| ${row.join(" | ")} |`)
  ];
  return lines.join("\n");
}

function memoToCsv(list) {
  const escapeField = (value) => `"${String(value).replace(/"/g, '""')}"`;
  const header = ["Time", "Real", "Emu", "Offset", "Range", "Drift", "Input", "Memo"];
  const rows = list.map((entry) => [
    entry.timeText,
    formatRealTimeDisplay(entry.savedAt),
    entry.emulatorLabel,
    entry.offsetSeconds,
    entry.searchRangeSeconds,
    entry.driftText || "-",
    entry.input.replace(/\n/g, " "),
    entry.memo || ""
  ]);
  return [header, ...rows].map((row) => row.map(escapeField).join(",")).join("\n");
}

function parseUrlOverrides() {
  const url = new URL(window.location.href);
  const params = url.searchParams;
  const overrides = {};
  let changed = false;

  if (params.has("emu")) {
    const emu = params.get("emu").trim();
    if (emu) {
      overrides.emu = emu;
      params.delete("emu");
      changed = true;
    }
  }

  if (params.has("offset")) {
    const v = normalizeOffsetSeconds(params.get("offset"));
    if (v !== null) {
      overrides.offsetSeconds = v;
      params.delete("offset");
      changed = true;
    }
  }

  if (params.has("range")) {
    const v = normalizeSearchRangeSeconds(params.get("range"));
    if (v !== null) {
      overrides.searchRangeSeconds = v;
      params.delete("range");
      changed = true;
    }
  }

  if (params.has("input")) {
    const input = params.get("input").trim();
    if (input) {
      overrides.actionInput = input;
      params.delete("input");
      changed = true;
    }
  }

  if (changed) {
    history.replaceState(null, "", url.toString());
  }

  return Object.keys(overrides).length ? overrides : null;
}


function findEmulatorIndexByLabel(label) {
  return state.emulators.findIndex((emu) => emu.label === label);
}

function applyUrlOverrides(applyEmu) {
  const overrides = state.urlOverrides;
  if (!overrides) {
    return;
  }
  if (!state.urlOverridesAppliedNonEmu) {
    if (typeof overrides.offsetSeconds === "number") {
      setOffsetSeconds(overrides.offsetSeconds);
    }
    if (typeof overrides.searchRangeSeconds === "number") {
      setSearchRangeSeconds(overrides.searchRangeSeconds);
    }
    if (overrides.actionInput && ui.actionInput) {
      ui.actionInput.value = overrides.actionInput;
    }
    state.urlOverridesAppliedNonEmu = true;
  }
  if (applyEmu && overrides.emu && !state.urlOverridesAppliedEmu) {
    const index = findEmulatorIndexByLabel(overrides.emu);
    if (index >= 0) {
      ui.emulatorSelect.selectedIndex = index;
      setActiveEmulator(index);
      state.urlOverridesAppliedEmu = true;
    }
  }
}

function formatScaledSeconds(scaledValue, scale) {
  const digits = scale.toString().length - 1;
  const sign = scaledValue < 0n ? "-" : "";
  const absValue = scaledValue < 0n ? -scaledValue : scaledValue;
  const whole = absValue / scale;
  const fraction = absValue % scale;
  return `${sign}${whole.toString()}.${fraction.toString().padStart(digits, "0")}`;
}

function computeSeedSecondsScaled(seed) {
  const shifted = seed >> 16n;
  return (shifted * SEED_SECONDS_NUMERATOR) / SEED_SECONDS_DIVISOR;
}

function computeSeedSecondsNumber(seed) {
  return Number(computeSeedSecondsScaled(seed)) / Number(SEED_TIME_SCALE);
}

function computeRealSecondsScaled(parsed, preciseTimeUnits = null) {
  if (preciseTimeUnits !== null) {
    return preciseTimeUnits / BigInt(AUTO_TIMER_FRACTION_SCALE / Number(SEED_TIME_SCALE));
  }
  const totalSeconds = parsed.hours * 3600 + parsed.minutes * 60 + parsed.seconds;
  return BigInt(totalSeconds) * SEED_TIME_SCALE;
}

function computeSeedDriftText(seed, parsed, preciseTimeUnits = null) {
  const seedSecondsScaled = computeSeedSecondsScaled(seed);
  const realSecondsScaled = computeRealSecondsScaled(parsed, preciseTimeUnits);
  const driftScaled = realSecondsScaled - seedSecondsScaled;
  return formatScaledSeconds(driftScaled, SEED_TIME_SCALE);
}

function setSeedValues(seedText, parsedTime) {
  if (!seedText) {
    ui.seedHex.textContent = "-";
    ui.seedSpeed.textContent = "-";
    ui.seedElapsed.textContent = "-";
    ui.seedDrift.textContent = "-";
    return;
  }
  const seed = BigInt(seedText);
  ui.seedHex.textContent = `0x${seed.toString(16)}`;
  ui.seedDrift.textContent = parsedTime ? computeSeedDriftText(seed, parsedTime) : "-";
}

function clearOutputs() {
  ui.dumpOutput.value = "";
  setSeedValues("");
  setSeedState("waiting");
}

function parseIntValue(el) {
  const value = Number.parseInt(el.value, 10);
  return Number.isFinite(value) ? value : 0;
}

function normalizeOffsetSeconds(value) {
  const parsed = Number.parseInt(value, 10);
  if (!Number.isFinite(parsed) || parsed < 0) {
    return DEFAULT_OFFSET_SECONDS;
  }
  return parsed;
}

function normalizeSearchRangeSeconds(value) {
  const parsed = Number.parseInt(value, 10);
  if (!Number.isFinite(parsed)) {
    return DEFAULT_SEARCH_RANGE_SECONDS;
  }
  return Math.min(15, Math.max(2, parsed));
}

function loadOffsetSeconds() {
  const stored = localStorage.getItem(OFFSET_STORAGE_KEY);
  return normalizeOffsetSeconds(stored);
}

function setOffsetSeconds(value) {
  const normalized = normalizeOffsetSeconds(value);
  state.offsetSeconds = normalized;
  if (ui.offsetSeconds) {
    ui.offsetSeconds.value = String(normalized);
  }
  localStorage.setItem(OFFSET_STORAGE_KEY, String(normalized));
  if (
    state.autoTimerAnchor &&
    state.autoTimerAppliedPrefix &&
    ui.actionInput &&
    ui.actionInput.value.startsWith(state.autoTimerAppliedPrefix)
  ) {
    const appliedSeconds = computeAutoTimerAppliedSeconds();
    if (appliedSeconds !== null) {
      const suffix = ui.actionInput.value.slice(state.autoTimerAppliedPrefix.length);
      const timeText = formatActionTime(appliedSeconds);
      ui.actionInput.value = `${timeText}${suffix}`;
      state.autoTimerAppliedPrefix = `${timeText} `;
      if (state.autoTimerLastUse) {
        state.autoTimerLastUse.timeText = timeText;
      }
      if (state.autoTimerFractionSourceTimeText) {
        state.autoTimerFractionSourceTimeText = timeText;
      }
    }
  }
  updateAutoTimerPreview();
}

function loadSearchRangeSeconds() {
  const stored = localStorage.getItem(SEARCH_RANGE_STORAGE_KEY);
  return normalizeSearchRangeSeconds(stored);
}

function setSearchRangeSeconds(value) {
  const normalized = normalizeSearchRangeSeconds(value);
  state.searchRangeSeconds = normalized;
  if (ui.searchRangeSeconds) {
    ui.searchRangeSeconds.value = String(normalized);
  }
  localStorage.setItem(SEARCH_RANGE_STORAGE_KEY, String(normalized));
}

function initMemoLedger() {
  state.memoList = loadSeedMemos();
  renderSeedMemos(state.memoList);
  state.urlOverrides = parseUrlOverrides();
  state.urlOverridesAppliedNonEmu = false;
  state.urlOverridesAppliedEmu = false;
  applyUrlOverrides(false);
}

function setAutoTimerStatusText(message) {
  if (ui.autoTimerStatus) {
    ui.autoTimerStatus.textContent = message;
  }
}

function setAutoTimerResetConfirmVisible(visible) {
  if (!ui.autoTimerResetConfirm) {
    return;
  }
  ui.autoTimerResetConfirm.classList.toggle("is-hidden", !visible);
}

function getAutoTimerStatusReadyText() {
  return t(
    "autoTimerStatusReady",
    "Preview is locked to performance.now() and ignores wall-clock changes."
  );
}

function getAutoTimerStatusIdleText() {
  return t(
    "autoTimerStatusIdle",
    "No timer anchor yet. Press Start Timer when the in-game timer begins."
  );
}

function computeAutoTimerSeconds(nowPerf = performance.now()) {
  if (!state.autoTimerAnchor) {
    return null;
  }
  const elapsedSeconds = Math.max(0, (nowPerf - state.autoTimerAnchor.perfNow) / 1000);
  return state.autoTimerAnchor.totalSeconds + elapsedSeconds;
}

function computeAutoTimerAppliedSeconds(nowPerf = performance.now()) {
  const seconds = computeAutoTimerSeconds(nowPerf);
  if (seconds === null) {
    return null;
  }
  return seconds - normalizeOffsetSeconds(state.offsetSeconds);
}

function updateAutoTimerButtons() {
  const hasAnchor = Boolean(state.autoTimerAnchor);
  if (ui.autoTimerStartButton) {
    ui.autoTimerStartButton.disabled = state.running;
  }
  if (ui.autoTimerUseButton) {
    ui.autoTimerUseButton.disabled = !hasAnchor || state.running;
  }
  if (ui.autoTimerResetButton) {
    ui.autoTimerResetButton.disabled = !hasAnchor || state.running;
  }
  if (ui.autoTimerResetConfirmButton) {
    ui.autoTimerResetConfirmButton.disabled = !hasAnchor || state.running;
  }
}

function updateAutoTimerPreview() {
  const seconds = computeAutoTimerAppliedSeconds();
  if (ui.autoTimerPreview) {
    ui.autoTimerPreview.textContent = formatTimerPreview(seconds);
  }
  updateAutoTimerButtons();
}

function stopAutoTimerTicker() {
  if (state.autoTimerTickHandle !== null) {
    clearTimeout(state.autoTimerTickHandle);
    state.autoTimerTickHandle = null;
  }
}

function scheduleAutoTimerTick() {
  stopAutoTimerTicker();
  updateAutoTimerPreview();
  if (!state.autoTimerAnchor) {
    return;
  }
  const elapsedMs = Math.max(0, performance.now() - state.autoTimerAnchor.perfNow);
  const delay = 1000 - (Math.floor(elapsedMs) % 1000);
  state.autoTimerTickHandle = setTimeout(scheduleAutoTimerTick, Math.max(80, delay));
}

function setAutoTimerAnchorSeconds(totalSeconds, perfNow) {
  state.autoTimerAnchor = {
    totalSeconds,
    perfNow
  };
  setAutoTimerResetConfirmVisible(false);
  setAutoTimerStatusText(getAutoTimerStatusReadyText());
  scheduleAutoTimerTick();
}

function setAutoTimerAnchor(parsed, perfNow) {
  setAutoTimerAnchorSeconds(parsedToTotalSeconds(parsed), perfNow);
}

function startManualAutoTimer(perfNow = performance.now()) {
  setAutoTimerAnchorSeconds(normalizeOffsetSeconds(state.offsetSeconds), perfNow);
}

function clearAutoTimerAnchor() {
  state.autoTimerAnchor = null;
  state.autoTimerAppliedPrefix = "";
  state.autoTimerLastUse = null;
  state.autoTimerCorrectionCount = 0;
  clearAutoTimerFractionHideTimer();
  setAutoTimerFractionDigits(null, "");
  setAutoTimerResetConfirmVisible(false);
  stopAutoTimerTicker();
  updateAutoTimerPreview();
  setAutoTimerStatusText(getAutoTimerStatusIdleText());
}

function extractActionSuffix(text) {
  const tokens = text.trim().split(/\s+/).filter(Boolean);
  if (tokens.length <= 3) {
    return "";
  }
  return tokens.slice(3).join(" ");
}

function focusActionInputAtTop() {
  window.scrollTo({ top: 0, behavior: "smooth" });
  requestAnimationFrame(() => {
    if (!ui.actionInput) {
      return;
    }
    ui.actionInput.focus({ preventScroll: true });
    const end = ui.actionInput.value.length;
    ui.actionInput.setSelectionRange(end, end);
  });
}

function getPreciseSearchTimeUnits(parsed, rawInput) {
  const baseUnits = BigInt(parsedToTotalSeconds(parsed) * AUTO_TIMER_FRACTION_SCALE);
  const fractionDigits = getAutoTimerFractionDigits();
  if (
    fractionDigits === null ||
    fractionDigits === 0 ||
    extractInputTimeText(rawInput) !== state.autoTimerFractionSourceTimeText
  ) {
    return baseUnits;
  }
  return baseUnits + BigInt(fractionDigits);
}

function computeSeedRange(hours, minutes, seconds, offsetSeconds, preciseTimeUnits = null) {
  const seedShift = 65536n;
  const totalTimeUnits = preciseTimeUnits === null
    ? BigInt((hours * 3600 + minutes * 60 + seconds) * AUTO_TIMER_FRACTION_SCALE)
    : preciseTimeUnits;
  const offset = BigInt(normalizeOffsetSeconds(offsetSeconds) * AUTO_TIMER_FRACTION_SCALE);
  const range = BigInt(normalizeSearchRangeSeconds(state.searchRangeSeconds) * AUTO_TIMER_FRACTION_SCALE);
  const unitScale = BigInt(AUTO_TIMER_FRACTION_SCALE);
  const numerator1 = 2n * (totalTimeUnits - offset) - range;
  const time1 = (numerator1 * 100000n) / (2n * 12515n * unitScale);
  const numerator2 = 2n * (totalTimeUnits - offset) + range;
  const time2 = (numerator2 * 1000000n) / (2n * 125155n * unitScale);
  return { start: time1 * seedShift, end: time2 * seedShift };
}

function splitRange(start, end, threads) {
  const ranges = [];
  if (end <= start) {
    return ranges;
  }
  const length = end - start;
  const chunk = (length + BigInt(threads) - 1n) / BigInt(threads);
  for (let i = 0; i < threads; i += 1) {
    const rangeStart = start + chunk * BigInt(i);
    const rangeEnd = rangeStart + chunk > end ? end : rangeStart + chunk;
    if (rangeStart < rangeEnd) {
      ranges.push({ start: rangeStart, end: rangeEnd });
    }
  }
  return ranges;
}

function enqueuePreload(task) {
  state.preloadQueue = state.preloadQueue.then(task).catch(() => {});
  return state.preloadQueue;
}

async function ensureWorkerScript() {
  if (state.workerScriptText) {
    return;
  }
  try {
    const response = await fetch("worker.js");
    state.workerScriptText = await response.text();
    const blob = new Blob([state.workerScriptText], { type: "application/javascript" });
    state.workerBlobUrl = URL.createObjectURL(blob);
  } catch (err) {
    appendLog("worker.js preload failed");
  }
}

async function ensureModulePayload(moduleUrl) {
  if (state.moduleCache.has(moduleUrl)) {
    return state.moduleCache.get(moduleUrl);
  }

  const jsText = await fetch(moduleUrl).then((r) => r.text());

  const payload = { jsText };
  state.moduleCache.set(moduleUrl, payload);
  return payload;
}


function preloadModule(moduleUrl) {
  return enqueuePreload(async () => {
    if (!state.preload) {
      return;
    }
    try {
      await ensureWorkerScript();
      await ensureModulePayload(moduleUrl);
      appendLog(`preloaded ${moduleUrl}`);
    } catch (err) {
      appendLog("preload failed");
    }
  });
}

function createWorkerClient(workerUrl) {
  const worker = new Worker(workerUrl || "worker.js");
  let counter = 0;
  const pending = new Map();

  worker.onmessage = (event) => {
    const { id, type, ...payload } = event.data;
    const entry = pending.get(id);
    if (!entry) return;
    pending.delete(id);
    entry.resolve({ type, ...payload });
  };

  function call(type, payload) {
    return new Promise((resolve) => {
      const id = ++counter;
      pending.set(id, { resolve });
      worker.postMessage({ id, type, ...payload });
    });
  }

  return {
    worker,
    call,

    // ここは将来拡張用に残すだけでOK
    ready: () => Promise.resolve(),

    terminate() {
      worker.terminate();
      pending.clear();
    }
  };
}


async function loadManifest() {
  try {
    const response = await fetch("emulators.json", { cache: "no-store" });
    if (!response.ok) {
      throw new Error("manifest not found");
    }
    const data = await response.json();
    if (!Array.isArray(data) || !data.length) {
      throw new Error("manifest empty");
    }
    state.emulators = data;
    populateEmulators();
    applyUrlOverrides(true);
    state.emulatorStatusKey = "ready";
    ui.emulatorStatus.textContent = t("ready", "ready");
  } catch (err) {
    state.emulatorStatusKey = "missing";
    ui.emulatorStatus.textContent = t("missing", "missing");
    ui.emulatorSelect.innerHTML = "";
    ui.emulatorMeta.textContent = "emulators.json not available";
    ui.runButton.disabled = true;
    appendLog("manifest load failed");
  }
}

function populateEmulators() {
  ui.emulatorSelect.innerHTML = "";
  state.emulators.forEach((emu, index) => {
    const option = document.createElement("option");
    option.value = String(index);
    option.textContent = `${emu.label} [${emu.branch}]`;
    ui.emulatorSelect.appendChild(option);
  });
  ui.emulatorSelect.selectedIndex = 0;
  setActiveEmulator(0);
}

function setActiveEmulator(index) {
  const emulator = state.emulators[index];
  if (!emulator) {
    return;
  }
  state.active = emulator;
  ui.emulatorMeta.textContent = `${emulator.branch} :: ${emulator.module}`;
  appendLog(`selected emulator ${emulator.label}`);
  if (state.preload) {
    const moduleUrl = new URL(emulator.module, window.location.href).toString();
    preloadModule(moduleUrl);
  }
}

function parseInput(text) {
  const tokens = text.trim().split(/\s+/).filter(Boolean);
  if (tokens.length < 4) {
    return { error: "input needs time and actions" };
  }
  const hours = Number.parseInt(tokens[0], 10);
  const minutes = Number.parseInt(tokens[1], 10);
  const seconds = Number.parseInt(tokens[2], 10);
  if (![hours, minutes, seconds].every(Number.isFinite)) {
    return { error: "invalid time format" };
  }
  return {
    hours,
    minutes,
    seconds,
    actions: tokens.slice(3)
  };
}

function shouldApplyAutoTimerCorrection(parsed, rawInput, nowPerf = performance.now()) {
  if (!parsed || !state.autoTimerLastUse) {
    return false;
  }
  if (nowPerf - state.autoTimerLastUse.perfNow > AUTO_TIMER_CORRECTION_LIMIT_MS) {
    return false;
  }
  return true;
}

function applySearchResultAutoTimerCorrection(seed) {
  const lastUse = state.autoTimerLastUse;
  if (!lastUse) {
    return false;
  }
  const correctedDisplayedSeconds = computeSeedSecondsNumber(seed);
  const totalSeconds = correctedDisplayedSeconds + normalizeOffsetSeconds(lastUse.offsetSecondsAtUse);
  setAutoTimerAnchorSeconds(totalSeconds, lastUse.perfNow);
  state.autoTimerCorrectionCount += 1;
  const preciseTime = splitPreciseSeconds(correctedDisplayedSeconds);
  setAutoTimerFractionDigits(preciseTime.fraction, formatActionTime(preciseTime.wholeSeconds));
  return true;
}

function applyLanguage(lang) {
  const config = window.APP_CONFIG || {};
  const dictionary = config.i18n ? config.i18n[lang] : null;
  if (!dictionary) {
    return;
  }
  document.querySelectorAll("[data-i18n]").forEach((node) => {
    const key = node.getAttribute("data-i18n");
    if (dictionary[key]) {
      node.textContent = dictionary[key];
    }
  });
  if (state.emulatorStatusKey) {
    ui.emulatorStatus.textContent = dictionary[state.emulatorStatusKey] || state.emulatorStatusKey;
  }
  document.documentElement.dataset.lang = lang;
  document.documentElement.lang = lang;
  state.lang = lang;
  localStorage.setItem("dq9Lang", lang);
  renderSeedMemos(state.memoList);
  setAutoTimerStatusText(
    state.autoTimerAnchor ? getAutoTimerStatusReadyText() : getAutoTimerStatusIdleText()
  );
  updateAutoTimerPreview();
}

function applyTheme(theme) {
  document.documentElement.dataset.theme = theme;
  state.theme = theme;
  localStorage.setItem("dq9Theme", theme);
}

function initSettings() {
  const config = window.APP_CONFIG || {};
  const themes = config.themes || [];
  ui.themeSelect.innerHTML = "";
  themes.forEach((themeName) => {
    const option = document.createElement("option");
    option.value = themeName;
    option.textContent = themeName;
    ui.themeSelect.appendChild(option);
  });
  ui.themeSelect.value = state.theme;
  ui.langSelect.value = state.lang;
  ui.preloadToggle.checked = state.preload;
  applyLanguage(state.lang);
  applyTheme(state.theme);
  setOffsetSeconds(loadOffsetSeconds());
  setSearchRangeSeconds(loadSearchRangeSeconds());
  clearAutoTimerAnchor();
}

async function runSearch() {
  if (state.running || !state.active) {
    return;
  }

  const threads = Math.max(1, Math.min(32, parseIntValue(ui.threads) || 4));
  const rawInput = ui.actionInput.value;
  const input = rawInput.trim();
  const useAppliedOffset = Boolean(state.autoTimerAppliedPrefix) && rawInput.startsWith(state.autoTimerAppliedPrefix);

  if (!input) {
    appendLog("input is empty");
    return;
  }

  const parsed = parseInput(input);
  if (parsed.error) {
    appendLog(parsed.error);
    return;
  }
  const preciseTimeUnits = getPreciseSearchTimeUnits(parsed, input);
  const runStartedAtPerf = performance.now();
  const shouldCorrectAutoTimer = shouldApplyAutoTimerCorrection(parsed, input, runStartedAtPerf);

  const { start, end } = computeSeedRange(
    parsed.hours,
    parsed.minutes,
    parsed.seconds,
    useAppliedOffset ? 0 : state.offsetSeconds,
    preciseTimeUnits
  );
  const ranges = splitRange(start, end, threads);
  if (!ranges.length) {
    appendLog("invalid time range");
    return;
  }

  clearOutputs();
  state.running = true;
  ui.runButton.disabled = true;
  updateAutoTimerButtons();
  setSeedState("running", 0);
  appendLog(`range ${start} -> ${end} using ${ranges.length} workers`);
  const inputActions = parsed.actions.join(" ");

  const moduleUrl = new URL(state.active.module, window.location.href).toString();
  const payload = await ensureModulePayload(moduleUrl);
  await ensureWorkerScript();
  const clients = ranges.map(() => createWorkerClient("worker.js"));

  try {
    await Promise.all(clients.map((client) => client.ready()));

    const prepResults = await Promise.all(
        clients.map((client) =>
            client.call("prepare", {
              moduleKey: moduleUrl,
              moduleSource: payload.jsText,
              input: inputActions
            })
        )
    );


    const count = prepResults[0].count || 0;
    if (!count) {
      appendLog(prepResults[0].error || "input parse failed");
      setSeedState("error");
      return;
    }
    if (count > 1) {
      appendLog(`multiple combinations detected (${count}), using first`);
    }

    const brutePromises = clients.map((client, index) =>
        client.call("bruteforce", {
          moduleUrl,
          resultIndex: 0,
          startSeed: ranges[index].start.toString(),
          endSeed: ranges[index].end.toString(),
          moduleKey: moduleUrl,
          moduleSource: payload.jsText,
        })
    );

    const tracked = brutePromises.map((promise, index) =>
        promise.then((result) => ({ index, result }))
    );

    const pending = new Set(tracked);
    let foundSeed = "";
    let foundIndex = -1;
    let bestSpeed = null;
    let bestElapsed = null;
    let bestTurns = null;
    let totalFound = 0;
    while (pending.size) {
      const next = await Promise.race(Array.from(pending));
      pending.delete(tracked[next.index]);
      if (typeof next.result.found === "number") {
        totalFound += next.result.found;
        setSeedState("running", totalFound);
      }
      if (next.result.turns) {
        const turns = BigInt(next.result.turns);
        const elapsedMs = BigInt(next.result.elapsedMs || 1);
        const speed = (turns * 1000n) / (elapsedMs * 10000n);
        appendLog(
            `worker ${next.index + 1} turns=${turns} elapsed=${elapsedMs}ms speed=${speed} (m turns/s)`
        );
        if (!bestSpeed || speed > bestSpeed) {
          bestSpeed = speed;
          bestElapsed = elapsedMs;
          bestTurns = turns;
        }
      }
      if (next.result.seed) {
        if (foundSeed) {
          foundSeed = ""; // 複数ヒット検出
        } else {
          foundSeed = next.result.seed;
          foundIndex = next.index;
        }
      }
    }

    if (!foundSeed || totalFound !== 1) {
      setSeedState("notFound", totalFound);
      if (totalFound > 1) {
        appendLog("multiple seeds found");
      }
      appendLog("seed not found");
      return;
    }

    setSeedValues(foundSeed, parsed);
    setSeedState("found", totalFound);
    appendLog(`seed found ${foundSeed}`);
    if (bestSpeed && bestElapsed && bestTurns) {
      ui.seedSpeed.textContent = `${bestSpeed} (m turns/s)`;
      ui.seedElapsed.textContent = `${bestElapsed} ms`;
    }

    clients.forEach((client, index) => {
      if (index !== foundIndex) {
        client.terminate();
      }
    });

    const searchClient = clients[foundIndex];
    let searchResult = await searchClient.call("search", {
      moduleUrl,
      resultIndex: 0,
      seed: foundSeed,
      numThreads: threads,
      dropbug: true,
      moduleKey: moduleUrl,
      moduleSource: payload.jsText,
    });

    if(searchResult.type === "error"){
      console.info(searchResult.message);
      throw new Error(searchResult.message);
    }

    if (searchResult.output.startsWith("SearchRequest failed")) {
      searchResult = await searchClient.call("search", {
        moduleUrl,
        resultIndex: 0,
        seed: foundSeed,
        numThreads: threads,
        dropbug: false,
        moduleKey: moduleUrl,
        moduleSource: payload.jsText,
      });
    }

    ui.dumpOutput.value = searchResult.output;

    const turns = BigInt(searchResult.turns);
    const elapsedMs = BigInt(searchResult.elapsedMs || 1);
    const speed = (turns * 1000n) / (elapsedMs * 10000n);
    appendLog(
        `searcher worker turns=${turns} elapsed=${elapsedMs}ms speed=${speed} (m turns/s)`
    );

    const driftText = computeSeedDriftText(BigInt(foundSeed), parsed, preciseTimeUnits);
    if (shouldCorrectAutoTimer) {
      applySearchResultAutoTimerCorrection(BigInt(foundSeed));
    }
    recordSeedMemo(parsed, input, driftText);

    appendLog("dump table ready");
    searchClient.terminate();
  } catch (err) {
    appendLog(err ? String(err) : "run failed");
    setSeedState("error");
  } finally {
    clients.forEach((client) => client.terminate());
    state.running = false;
    ui.runButton.disabled = false;
    updateAutoTimerButtons();
  }
}

let becameActive = false;
let movementTimer = null;
const SAFE_TIME = 1500;

function setMovementTimerOnce() {
  if (movementTimer !== null) {
    return;
  }
  movementTimer = setTimeout(() => {
    becameActive = false;
    movementTimer = null;
  }, SAFE_TIME);
}

function throttle(fn, wait) {
  let lastTime = 0;
  return function throttled(...args) {
    const now = Date.now();
    if (now - lastTime >= wait) {
      lastTime = now;
      fn.apply(this, args);
    }
  };
}

function movePointerGuard() {
  if (!becameActive) {
    return;
  }
  setMovementTimerOnce();
}

function isPointerUnsafe() {
  return becameActive;
}

function initPointerSafety() {
  const throttledMove = throttle(movePointerGuard, 200);
  window.addEventListener("mousemove", throttledMove);
  document.addEventListener("mouseenter", () => {
    becameActive = true;
    movementTimer = null;
    setMovementTimerOnce();
  });
}

function applyAutoTimerToInput() {
  const predictedSeconds = computeAutoTimerAppliedSeconds();
  if (predictedSeconds === null) {
    return;
  }
  const suffix = extractActionSuffix(ui.actionInput.value);
  const perfNow = performance.now();
  const preciseTime = splitPreciseSeconds(predictedSeconds);
  const timeText = formatActionTime(preciseTime.wholeSeconds);
  ui.actionInput.value = `${timeText}${suffix ? ` ${suffix}` : " "}`;
  state.autoTimerAppliedPrefix = `${timeText} `;
  state.autoTimerLastUse = {
    perfNow,
    inputText: ui.actionInput.value,
    timeText,
    fractionDigits: preciseTime.fraction,
    offsetSecondsAtUse: state.offsetSeconds
  };
  setAutoTimerFractionDigits(preciseTime.fraction, timeText);
  restartAutoTimerFractionHideTimer();
  setAutoTimerResetConfirmVisible(false);
}

ui.emulatorSelect.addEventListener("change", (event) => {
  setActiveEmulator(Number(event.target.value));
  ui.actionInput.value = "";
  state.autoTimerAppliedPrefix = "";
  state.autoTimerLastUse = null;
  clearAutoTimerFractionHideTimer();
  setAutoTimerFractionDigits(null, "");
});

ui.runButton.addEventListener("click", () => {
  focusActionInputAtTop();
  runSearch();
});

ui.actionInput.addEventListener("keydown", (event) => {
  if (event.key === "Enter" && !event.shiftKey) {
    event.preventDefault();
    focusActionInputAtTop();
    runSearch();
  }
});

if (ui.autoTimerFractionInput) {
  ui.autoTimerFractionInput.addEventListener("input", () => {
    const digits = normalizeFractionDigits(ui.autoTimerFractionInput.value);
    if (digits === null || digits === 0) {
      clearAutoTimerFractionHideTimer();
      setAutoTimerFractionDigits(null, "");
      return;
    }
    ui.autoTimerFractionInput.value = formatFractionDigits(digits);
    updateAutoTimerFractionVisibility();
  });
}

if (ui.autoTimerStartButton) {
  ui.autoTimerStartButton.addEventListener("click", () => {
    startManualAutoTimer();
  });
}

if (ui.autoTimerUseButton) {
  ui.autoTimerUseButton.addEventListener("click", () => {
    applyAutoTimerToInput();
  });
}

if (ui.autoTimerResetButton) {
  ui.autoTimerResetButton.addEventListener("click", () => {
    if (!state.autoTimerAnchor) {
      return;
    }
    if (isPointerUnsafe()) {
      setAutoTimerStatusText(
        t("autoTimerStatusUnsafe", "Pointer just re-entered the window. Wait 1.5s and try again.")
      );
      return;
    }
    setAutoTimerStatusText(getAutoTimerStatusReadyText());
    setAutoTimerResetConfirmVisible(true);
  });
}

if (ui.autoTimerResetConfirmButton) {
  ui.autoTimerResetConfirmButton.addEventListener("click", () => {
    if (isPointerUnsafe()) {
      setAutoTimerStatusText(
        t("autoTimerStatusUnsafe", "Pointer just re-entered the window. Wait 1.5s and try again.")
      );
      return;
    }
    clearAutoTimerAnchor();
  });
}

if (ui.autoTimerResetCancelButton) {
  ui.autoTimerResetCancelButton.addEventListener("click", () => {
    setAutoTimerResetConfirmVisible(false);
    setAutoTimerStatusText(
      state.autoTimerAnchor ? getAutoTimerStatusReadyText() : getAutoTimerStatusIdleText()
    );
  });
}

ui.themeSelect.addEventListener("change", (event) => {
  applyTheme(event.target.value);
});

ui.langSelect.addEventListener("change", (event) => {
  applyLanguage(event.target.value);
});

ui.preloadToggle.addEventListener("change", (event) => {
  state.preload = event.target.checked;
  localStorage.setItem("dq9Preload", state.preload ? "1" : "0");
  if (state.preload && state.active) {
    const moduleUrl = new URL(state.active.module, window.location.href).toString();
    preloadModule(moduleUrl);
  }
});

if (ui.memoCopyMarkdown) {
  ui.memoCopyMarkdown.addEventListener("click", (event) => {
    event.preventDefault();
    if (!state.memoList.length) {
      return;
    }
    copyText(memoToMarkdown(state.memoList));
    appendLog("memo markdown copied");
  });
}

if (ui.memoCopyCsv) {
  ui.memoCopyCsv.addEventListener("click", (event) => {
    event.preventDefault();
    if (!state.memoList.length) {
      return;
    }
    copyText(memoToCsv(state.memoList));
    appendLog("memo csv copied");
  });
}

if (ui.memoTableBody) {
  ui.memoTableBody.addEventListener("click", (event) => {
    const target = event.target.closest("a");
    if (!target) {
      return;
    }
    const inputId = target.dataset.copyInput;
    const urlId = target.dataset.copyUrl;
    if (!inputId && !urlId) {
      return;
    }
    event.preventDefault();
    const entry = state.memoList.find((item) => item.id === (inputId || urlId));
    if (!entry) {
      return;
    }
    if (inputId) {
      copyText(entry.input);
      appendLog("memo input copied");
    } else if (urlId) {
      copyText(buildOverrideUrl(entry));
      appendLog("memo url copied");
    }
  });

  ui.memoTableBody.addEventListener("input", (event) => {
    const target = event.target;
    if (!target.classList.contains("memo-note-input")) {
      return;
    }
    const entryId = target.dataset.memoId;
    const entry = state.memoList.find((item) => item.id === entryId);
    if (!entry) {
      return;
    }
    entry.memo = target.value;
    saveSeedMemos(state.memoList);
  });
}

if (ui.offsetSeconds) {
  ui.offsetSeconds.addEventListener("change", (event) => {
    setOffsetSeconds(event.target.value);
  });
}

if (ui.searchRangeSeconds) {
  ui.searchRangeSeconds.addEventListener("change", (event) => {
    setSearchRangeSeconds(event.target.value);
  });
}

loadManifest();
initSettings();
initMemoLedger();
initPointerSafety();


  let composing = false;

  const vowelMap = {
  "あ": "a",
  "い": "i",
  "う": "u",
  "え": "e",
  "お": "o",
  "ア": "a",
  "イ": "i",
  "ウ": "u",
  "エ": "e",
  "オ": "o"
};

  function normalizeActionInput(value) {
  // 1. 全角英数字などを半角へ
  let v = value.normalize("NFKC");

  // 2. 母音だけ名指し変換
  v = v.replace(/[あいうえおアイウエオ]/g, ch => vowelMap[ch]);

  return v;
}

  ui.actionInput.addEventListener("compositionstart", () => {
  composing = true;
});

  ui.actionInput.addEventListener("compositionend", () => {
  composing = false;
  ui.actionInput.value = normalizeActionInput(ui.actionInput.value);
});

  ui.actionInput.addEventListener("input", () => {
  if (composing) return;
  ui.actionInput.value = normalizeActionInput(ui.actionInput.value);
});

// テキスト選択は許可したまま、テキストのD&D（ドラッグで文字が移動/外部へD&D/ドロップで置換）だけ無効化する
function disableTextDnDForElement(el) {
  if (!el) return;

  // 既存機能を壊さないため、対象要素にだけ局所的に適用する
  el.addEventListener("dragstart", (event) => {
    // selection drag による「文字が移動する」挙動を止める
    event.preventDefault();
    event.stopPropagation();
  });

  // ドロップでtextareaの内容が置き換わる/挿入されるのを止める
  el.addEventListener("drop", (event) => {
    event.preventDefault();
    event.stopPropagation();
  });

  // drop を許可しない場合、dragover 側でも preventDefault しておくと安定する
  el.addEventListener("dragover", (event) => {
    event.preventDefault();
    event.stopPropagation();
  });
}

function initDisableTextDnD() {
  // 主要な textarea 類
  disableTextDnDForElement(ui.actionInput);
  disableTextDnDForElement(ui.dumpOutput);
  disableTextDnDForElement(ui.logOutput);

  // Seed Memo のメモ入力欄（動的生成）も対象にする（既存の input イベントとは競合しない）
  if (ui.memoTableBody) {
    ui.memoTableBody.addEventListener("dragstart", (event) => {
      const target = event.target;
      if (target && target.classList && target.classList.contains("memo-note-input")) {
        event.preventDefault();
        event.stopPropagation();
      }
    });
    ui.memoTableBody.addEventListener("drop", (event) => {
      const target = event.target;
      if (target && target.classList && target.classList.contains("memo-note-input")) {
        event.preventDefault();
        event.stopPropagation();
      }
    });
    ui.memoTableBody.addEventListener("dragover", (event) => {
      const target = event.target;
      if (target && target.classList && target.classList.contains("memo-note-input")) {
        event.preventDefault();
        event.stopPropagation();
      }
    });
  }
}

initDisableTextDnD();
