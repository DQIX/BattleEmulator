const ui = {
  emulatorSelect: document.getElementById("emulatorSelect"),
  emulatorMeta: document.getElementById("emulatorMeta"),
  emulatorStatus: document.getElementById("emulatorStatus"),
  hours: document.getElementById("hours"),
  minutes: document.getElementById("minutes"),
  seconds: document.getElementById("seconds"),
  threads: document.getElementById("threads"),
  actionInput: document.getElementById("actionInput"),
  runButton: document.getElementById("runButton"),
  seedHex: document.getElementById("seedHex"),
  seedDec: document.getElementById("seedDec"),
  seedState: document.getElementById("seedState"),
  dumpOutput: document.getElementById("dumpOutput"),
  logOutput: document.getElementById("logOutput")
};

const state = {
  emulators: [],
  active: null,
  running: false
};

const logLines = [];

function appendLog(line) {
  const timestamp = new Date().toLocaleTimeString();
  logLines.push(`[${timestamp}] ${line}`);
  ui.logOutput.value = logLines.join("\n");
  ui.logOutput.scrollTop = ui.logOutput.scrollHeight;
}

function setSeedState(text) {
  ui.seedState.textContent = text;
}

function setSeedValues(seedText) {
  if (!seedText) {
    ui.seedHex.textContent = "-";
    ui.seedDec.textContent = "-";
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
  const seedShift = 65536;
  let totalSeconds = hours * 3600 + minutes * 60 + seconds;
  totalSeconds -= 15;
  const time1 = Math.floor((totalSeconds - 4.5) * (1 / 0.12515)) * seedShift;
  const time2 = Math.floor((totalSeconds + 4.5) * (1 / 0.125155)) * seedShift;
  return { start: time1, end: time2 };
}

function splitRange(start, end, threads) {
  const ranges = [];
  if (end <= start) {
    return ranges;
  }
  const length = end - start;
  const chunk = Math.ceil(length / threads);
  for (let i = 0; i < threads; i += 1) {
    const rangeStart = start + chunk * i;
    const rangeEnd = Math.min(rangeStart + chunk, end);
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
    ui.emulatorStatus.textContent = "ready";
  } catch (err) {
    ui.emulatorStatus.textContent = "missing";
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

async function runSearch() {
  if (state.running || !state.active) {
    return;
  }

  const hours = parseIntValue(ui.hours);
  const minutes = parseIntValue(ui.minutes);
  const seconds = parseIntValue(ui.seconds);
  const threads = Math.max(1, Math.min(32, parseIntValue(ui.threads) || 4));
  const input = ui.actionInput.value.trim();

  if (!input) {
    appendLog("input is empty");
    return;
  }

  const { start, end } = computeSeedRange(hours, minutes, seconds);
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

  try {
    await Promise.all(clients.map((client) => client.call("load", { moduleUrl })));

    const prepResults = await Promise.all(
      clients.map((client) => client.call("prepare", { moduleUrl, input }))
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
        startSeed: String(ranges[index].start),
        endSeed: String(ranges[index].end)
      })
    );

    const tracked = brutePromises.map((promise, index) =>
      promise.then((result) => ({ index, result }))
    );

    const pending = new Set(tracked);
    let foundSeed = "";
    let foundIndex = -1;
    while (pending.size) {
      const next = await Promise.race(Array.from(pending));
      pending.delete(tracked[next.index]);
      if (next.result.seed) {
        foundSeed = next.result.seed;
        foundIndex = next.index;
        break;
      }
    }

    if (!foundSeed) {
      setSeedState("not found");
      appendLog("seed not found");
      return;
    }

    setSeedValues(foundSeed);
    setSeedState("found");
    appendLog(`seed found ${foundSeed}`);

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

loadManifest();
