const ui = {
  emulatorSelect: document.getElementById("emulatorSelect"),
  emulatorMeta: document.getElementById("emulatorMeta"),
  emulatorStatus: document.getElementById("emulatorStatus"),
  offsetSeconds: document.getElementById("offsetSeconds"),
  searchRangeSeconds: document.getElementById("searchRangeSeconds"),
  threads: document.getElementById("threads"),
  actionInput: document.getElementById("actionInput"),
  runButton: document.getElementById("runButton"),
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
  urlOverridesAppliedEmu: false
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

function hashString(text) {
  let hash = 5381;
  for (let i = 0; i < text.length; i += 1) {
    hash = (hash * 33) ^ text.charCodeAt(i);
  }
  return `m${(hash >>> 0).toString(36)}`;
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

function buildMemoFingerprint(entry) {
  const payload = {
    emulatorLabel: entry.emulatorLabel,
    input: entry.input,
    timeText: entry.timeText,
    driftText: entry.driftText || "",
    offsetSeconds: entry.offsetSeconds,
    searchRangeSeconds: entry.searchRangeSeconds
  };
  return hashString(JSON.stringify(payload));
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

function recordSeedMemo(parsed, inputText, driftText) {
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
  entry.id = buildMemoFingerprint(entry);
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
  if (params.has("emu")) {
    const emu = params.get("emu").trim();
    if (emu) {
      overrides.emu = emu;
    }
  }
  if (params.has("offset")) {
    overrides.offsetSeconds = normalizeOffsetSeconds(params.get("offset"));
  }
  if (params.has("range")) {
    overrides.searchRangeSeconds = normalizeSearchRangeSeconds(params.get("range"));
  }
  if (params.has("input")) {
    const input = params.get("input").trim();
    if (input) {
      overrides.actionInput = input;
    }
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

function computeRealSecondsScaled(parsed) {
  const totalSeconds = parsed.hours * 3600 + parsed.minutes * 60 + parsed.seconds;
  return BigInt(totalSeconds) * SEED_TIME_SCALE;
}

function computeSeedDriftText(seed, parsed) {
  const seedSecondsScaled = computeSeedSecondsScaled(seed);
  const realSecondsScaled = computeRealSecondsScaled(parsed);
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

function computeSeedRange(hours, minutes, seconds, offsetSeconds) {
  const seedShift = 65536n;
  const totalSeconds = BigInt(hours * 3600 + minutes * 60 + seconds);
  const offset = BigInt(normalizeOffsetSeconds(offsetSeconds));
  const range = BigInt(normalizeSearchRangeSeconds(state.searchRangeSeconds));
  const numerator1 = 2n * (totalSeconds - offset) - range;
  const time1 = (numerator1 * 100000n) / (2n * 12515n);
  const numerator2 = 2n * (totalSeconds - offset) + range;
  const time2 = (numerator2 * 1000000n) / (2n * 125155n);
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
}

async function runSearch() {
  if (state.running || !state.active) {
    return;
  }

  const threads = Math.max(1, Math.min(32, parseIntValue(ui.threads) || 4));
  const input = ui.actionInput.value.trim();

  if (!input) {
    appendLog("input is empty");
    return;
  }

  const parsed = parseInput(input);
  if (parsed.error) {
    appendLog(parsed.error);
    return;
  }

  const { start, end } = computeSeedRange(
    parsed.hours,
    parsed.minutes,
    parsed.seconds,
    state.offsetSeconds
  );
  const ranges = splitRange(start, end, threads);
  if (!ranges.length) {
    appendLog("invalid time range");
    return;
  }

  clearOutputs();
  state.running = true;
  ui.runButton.disabled = true;
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
    if (totalFound === 1) {
      const driftText = computeSeedDriftText(BigInt(foundSeed), parsed);
      recordSeedMemo(parsed, input, driftText);
    }
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

    appendLog("dump table ready");
    searchClient.terminate();
  } catch (err) {
    appendLog(err ? String(err) : "run failed");
    setSeedState("error");
  } finally {
    clients.forEach((client) => client.terminate());
    state.running = false;
    ui.runButton.disabled = false;
  }
}

ui.emulatorSelect.addEventListener("change", (event) => {
  setActiveEmulator(Number(event.target.value));
});

ui.runButton.addEventListener("click", () => {
  runSearch();
});

ui.actionInput.addEventListener("keydown", (event) => {
  if (event.key === "Enter" && !event.shiftKey) {
    event.preventDefault();
    runSearch();
  }
});

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
