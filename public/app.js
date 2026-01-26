const ui = {
  emulatorSelect: document.getElementById("emulatorSelect"),
  emulatorMeta: document.getElementById("emulatorMeta"),
  emulatorStatus: document.getElementById("emulatorStatus"),
  threads: document.getElementById("threads"),
  actionInput: document.getElementById("actionInput"),
  runButton: document.getElementById("runButton"),
  seedHex: document.getElementById("seedHex"),
  seedDec: document.getElementById("seedDec"),
  seedSpeed: document.getElementById("seedSpeed"),
  seedElapsed: document.getElementById("seedElapsed"),
  seedState: document.getElementById("seedState"),
  dumpOutput: document.getElementById("dumpOutput"),
  logOutput: document.getElementById("logOutput"),
  themeSelect: document.getElementById("themeSelect"),
  langSelect: document.getElementById("langSelect")
};

const state = {
  emulators: [],
  active: null,
  running: false,
  lang: document.documentElement.dataset.lang || "ja",
  theme: document.documentElement.dataset.theme || "lightSepia",
  emulatorStatusKey: "idle"
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

function setSeedState(text) {
  ui.seedState.textContent = t(text, text);
}

function setSeedValues(seedText) {
  if (!seedText) {
    ui.seedHex.textContent = "-";
    ui.seedDec.textContent = "-";
    ui.seedSpeed.textContent = "-";
    ui.seedElapsed.textContent = "-";
    return;
  }
  const seed = BigInt(seedText);
  ui.seedHex.textContent = `0x${seed.toString(16)}`;
  ui.seedDec.textContent = seed.toString(10);
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

function computeSeedRange(hours, minutes, seconds) {
  const seedShift = 65536n;
  const totalSeconds = BigInt(hours * 3600 + minutes * 60 + seconds);
  const numerator1 = 2n * (totalSeconds - 15n) - 9n;
  const time1 = (numerator1 * 100000n) / (2n * 12515n);
  const numerator2 = 2n * (totalSeconds - 15n) + 9n;
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

function createWorkerClient() {
  const worker = new Worker("worker.js");
  let counter = 0;
  const pending = new Map();

  worker.onmessage = (event) => {
    const { id, type, ...payload } = event.data;
    const entry = pending.get(id);
    if (!entry) {
      return;
    }
    pending.delete(id);
    entry.resolve({ type, ...payload });
  };

  function call(type, payload) {
    return new Promise((resolve, reject) => {
      const id = counter += 1;
      pending.set(id, { resolve, reject });
      worker.postMessage({ id, type, ...payload });
    });
  }

  return {
    worker,
    call,
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
  ui.threads.value = emulator.defaultThreads || 4;
  appendLog(`selected emulator ${emulator.label}`);
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
  applyLanguage(state.lang);
  applyTheme(state.theme);
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

  const { start, end } = computeSeedRange(parsed.hours, parsed.minutes, parsed.seconds);
  const ranges = splitRange(start, end, threads);
  if (!ranges.length) {
    appendLog("invalid time range");
    return;
  }

  clearOutputs();
  state.running = true;
  ui.runButton.disabled = true;
  setSeedState("running");
  appendLog(`range ${start} -> ${end} using ${ranges.length} workers`);

  const moduleUrl = new URL(state.active.module, window.location.href).toString();
  const clients = ranges.map(() => createWorkerClient());
  const inputActions = parsed.actions.join(" ");

  try {
    await Promise.all(clients.map((client) => client.call("load", { moduleUrl })));

    const prepResults = await Promise.all(
      clients.map((client) => client.call("prepare", { moduleUrl, input: inputActions }))
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
        endSeed: ranges[index].end.toString()
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
    while (pending.size) {
      const next = await Promise.race(Array.from(pending));
      pending.delete(tracked[next.index]);
      if (next.result.turns) {
        const turns = BigInt(next.result.turns);
        const elapsedMs = BigInt(next.result.elapsedMs || 1);
        const speed = (turns * 10000n * 1000n) / elapsedMs;
        appendLog(
          `worker ${next.index + 1} turns=${turns} elapsed=${elapsedMs}ms speed=${speed} (x1/10k turns/s)`
        );
        if (!bestSpeed || speed > bestSpeed) {
          bestSpeed = speed;
          bestElapsed = elapsedMs;
          bestTurns = turns;
        }
      }
      if (next.result.seed) {
        foundSeed = next.result.seed;
        foundIndex = next.index;
        break;
      }
    }

    if (!foundSeed) {
      setSeedState("notFound");
      appendLog("seed not found");
      return;
    }

    setSeedValues(foundSeed);
    setSeedState("found");
    appendLog(`seed found ${foundSeed}`);
    if (bestSpeed && bestElapsed && bestTurns) {
      ui.seedSpeed.textContent = `${bestSpeed} (x1/10k turns/s)`;
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
      dropbug: true
    });

    if (searchResult.output.startsWith("SearchRequest failed")) {
      searchResult = await searchClient.call("search", {
        moduleUrl,
        resultIndex: 0,
        seed: foundSeed,
        numThreads: threads,
        dropbug: false
      });
    }

    ui.dumpOutput.value = searchResult.output;
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

loadManifest();
initSettings();
