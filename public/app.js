const ui = {
  emulatorSelect: document.getElementById("emulatorSelect"),
  emulatorMeta: document.getElementById("emulatorMeta"),
  emulatorStatus: document.getElementById("emulatorStatus"),
  offsetSeconds: document.getElementById("offsetSeconds"),
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
  preloadToggle: document.getElementById("preloadToggle")
};

const DEFAULT_OFFSET_SECONDS = 15;
const OFFSET_STORAGE_KEY = "dq9OffsetSeconds";
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
  preloadQueue: Promise.resolve(),
  moduleCache: new Map(),
  workerScriptText: "",
  workerBlobUrl: ""
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

function computeSeedRange(hours, minutes, seconds, offsetSeconds) {
  const seedShift = 65536n;
  const totalSeconds = BigInt(hours * 3600 + minutes * 60 + seconds);
  const offset = BigInt(normalizeOffsetSeconds(offsetSeconds));
  const numerator1 = 2n * (totalSeconds - offset) - 6n;
  const time1 = (numerator1 * 100000n) / (2n * 12515n);
  const numerator2 = 2n * (totalSeconds - offset) + 6n;
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

if (ui.offsetSeconds) {
  ui.offsetSeconds.addEventListener("change", (event) => {
    setOffsetSeconds(event.target.value);
  });
}

loadManifest();
initSettings();
