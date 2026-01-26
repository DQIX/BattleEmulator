let moduleUrl = "";
let wasmUrlOverride = "";
let moduleReady = null;
let moduleReadyReject = null;
let moduleReadyResolve = null;

function resolveWasmPath(path) {
  const base = moduleUrl.replace(/[^/]+$/, "");
  return new URL(path, base).toString();
}

function initModuleFromText(jsText, wasmBuffer, wasmUrl) {
  if (moduleReady) {
    return moduleReady;
  }
  wasmUrlOverride = wasmUrl || "";
  moduleReady = new Promise((resolve, reject) => {
    moduleReadyResolve = resolve;
    moduleReadyReject = reject;
    self.Module = {
      wasmBinary: wasmBuffer,
      locateFile() {
        return wasmUrlOverride || "";
      },
      onRuntimeInitialized() {
        resolve(self.Module);
      }
    };
    try {
      const moduleFactory = new Function(jsText);
      moduleFactory();
    } catch (err) {
      reject(err);
    }
  });
  return moduleReady;
}

function loadModule(url) {
  if (moduleReady && url === moduleUrl) {
    return moduleReady;
  }
  moduleUrl = url;
  moduleReady = new Promise((resolve, reject) => {
    moduleReadyResolve = resolve;
    moduleReadyReject = reject;
    self.Module = {
      locateFile: resolveWasmPath,
      onRuntimeInitialized() {
        resolve(self.Module);
      }
    };
    try {
      importScripts(moduleUrl);
    } catch (err) {
      reject(err);
    }
  });
  return moduleReady;
}

self.onmessage = async (event) => {
  const { id, type, moduleUrl: url, input, resultIndex, startSeed, endSeed, seed, numThreads, dropbug } =
    event.data;
  try {
    if (type === "init") {
      const { jsText, wasm, wasmUrl } = event.data;
      await initModuleFromText(jsText, wasm, wasmUrl);
      self.postMessage({ id, type: "ready" });
      return;
    }
    if (type === "load") {
      await loadModule(url);
      self.postMessage({ id, type: "loaded" });
      return;
    }

    const Module = moduleReady ? await moduleReady : await loadModule(url);

    if (type === "prepare") {
      const count = Module.ccall("wasm_prepare_input", "number", ["string"], [input || ""]);
      let error = "";
      if (!count) {
        error = Module.ccall("wasm_get_last_error", "string", [], []);
      }
      self.postMessage({ id, type: "prepared", count, error });
      return;
    }

    if (type === "bruteforce") {
      const startTime = performance.now();
      const start = BigInt(startSeed);
      const end = BigInt(endSeed);
      const seedResult = Module._wasm_bruteforce_range(resultIndex || 0, start, end);
      const turns = Module._wasm_get_turn_processed();
      const elapsedMs = Math.max(1, Math.round(performance.now() - startTime));
      const seedText = seedResult === 0n ? "" : seedResult.toString();
      self.postMessage({
        id,
        type: "bruteforce-done",
        seed: seedText,
        turns: turns.toString(),
        elapsedMs
      });
      return;
    }

    if (type === "search") {
      const seedValue = BigInt(seed);
      const ptr = Module._wasm_search_dump(resultIndex || 0, seedValue, numThreads || 1, dropbug ? 1 : 0);
      const output = Module.UTF8ToString(ptr);
      self.postMessage({ id, type: "search-done", output });
    }
  } catch (err) {
    self.postMessage({ id, type: "error", message: err ? String(err) : "worker error" });
  }
};
