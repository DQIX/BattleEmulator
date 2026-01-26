let moduleUrl = "";
let moduleReady = null;

function loadModule(url) {
  if (moduleReady && url === moduleUrl) {
    return moduleReady;
  }
  moduleUrl = url;
  moduleReady = new Promise((resolve, reject) => {
    self.Module = {
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
      const startTime = performance.now();
      const seedResult = Module._wasm_bruteforce_range(
          resultIndex || 0,
          BigInt(startSeed),
          BigInt(endSeed)
      );
      const turns = Module._wasm_get_turn_processed();
      const elapsedMs = Math.max(1, Math.round(performance.now() - startTime));
      self.postMessage({
        id,
        type: "bruteforce-done",
        seed: seedResult ? seedResult.toString() : "",
        turns: turns.toString(),
        elapsedMs
      });
      return;
    }

    if (type === "search") {
      const ptr = Module._wasm_search_dump(
          resultIndex || 0,
          BigInt(seed),
          numThreads || 1,
          dropbug ? 1 : 0
      );
      const output = Module.UTF8ToString(ptr);
      self.postMessage({ id, type: "search-done", output });
    }
  } catch (err) {
    self.postMessage({ id, type: "error", message: String(err) });
  }
};
