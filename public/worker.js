let moduleUrl = "";
let moduleReady = null;

function resolveWasmPath(path) {
  const base = moduleUrl.replace(/[^/]+$/, "");
  return new URL(path, base).toString();
}

function loadModule(url) {
  if (moduleReady && url === moduleUrl) {
    return moduleReady;
  }
  moduleUrl = url;
  moduleReady = new Promise((resolve, reject) => {
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
    if (type === "load") {
      await loadModule(url);
      self.postMessage({ id, type: "loaded" });
      return;
    }

    const Module = await loadModule(url);

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
      const start = BigInt(startSeed);
      const end = BigInt(endSeed);
      const seedResult = Module._wasm_bruteforce_range(resultIndex || 0, start, end);
      const seedText = seedResult === 0n ? "" : seedResult.toString();
      self.postMessage({ id, type: "bruteforce-done", seed: seedText });
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
