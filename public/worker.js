/*
  worker.js
  - Emscripten の main JS は文字列で受け取る
  - pthread 等で外部 runtime asset が生成された場合は manifest の URL mapping を使う
  - module.js は Document 側で fetch し、文字列として渡す
*/

let moduleReady = null;
let moduleKey = "";
let runtimeAssetBlobUrls = {};

/**
 * Emscripten module をコード文字列から初期化する
 */
function loadModuleFromSource(jsText, key, runtimeAssets = {}) {
  if (moduleReady && key === moduleKey) {
    return moduleReady;
  }

  moduleKey = key;
  moduleReady = new Promise((resolve, reject) => {
    const mainModuleBlob = new Blob([jsText], {type: "application/javascript"});
    self.Module = {
      // Emscripten posts this Blob to each pthread worker. Its generated
      // helper creates the object URL inside that child worker, avoiding any
      // dependency on Blob URL visibility between Worker globals.
      mainScriptUrlOrBlob: mainModuleBlob,
      locateFile(path, prefix) {
        if (Object.prototype.hasOwnProperty.call(runtimeAssets, path)) {
          if (!runtimeAssetBlobUrls[path]) {
            runtimeAssetBlobUrls[path] = URL.createObjectURL(
              new Blob([runtimeAssets[path]], {type: "application/javascript"})
            );
          }
          return runtimeAssetBlobUrls[path];
        }
        return `${prefix || ""}${path}`;
      },
      onRuntimeInitialized() {
        resolve(self.Module);
      }
    };

    try {
      // SINGLE_FILE 出力をそのまま評価
      // worker 内限定用途なので問題なし
      (0, eval)(jsText);
    } catch (err) {
      console.error("worker module eval failed:", err);
      reject(err);
    }
  });

  return moduleReady;
}

self.onmessage = async (event) => {
  const {
    id,
    type,
    moduleKey: key,
    moduleSource,
    runtimeAssets,
    input,
    resultIndex,
    startSeed,
    endSeed,
    seed,
    numThreads,
    dropbug
  } = event.data;

  try {
    // load / prepare のどちらでも Module を初期化
    if (type === "load") {
      await loadModuleFromSource(moduleSource, key, runtimeAssets);
      self.postMessage({ id, type: "loaded" });
      return;
    }

    const Module = await loadModuleFromSource(moduleSource, key, runtimeAssets);

    if (type === "prepare") {
      const count = Module.ccall(
          "wasm_prepare_input",
          "number",
          ["string"],
          [input || ""]
      );

      let error = "";
      if (!count) {
        error = Module.ccall("wasm_get_last_error", "string", [], []);
      }

      self.postMessage({
        id,
        type: "prepared",
        count,
        error
      });
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
      const found = Module._wasm_get_found_seeds
          ? Module._wasm_get_found_seeds()
          : 0;

      const elapsedMs = Math.max(
          1,
          Math.round(performance.now() - startTime)
      );

      self.postMessage({
        id,
        type: "bruteforce-done",
        seed: seedResult ? seedResult.toString() : "",
        found,
        turns: turns.toString(),
        elapsedMs
      });
      return;
    }

    if (type === "search") {
      const startTime = performance.now();
      const ptr = Module._wasm_search_dump(
          resultIndex || 0,
          BigInt(seed),
          numThreads || 1,
          dropbug ? 1 : 0
      );

      const output = Module.UTF8ToString(ptr);
      const turns = Module._wasm_get_turn_processed();

      const elapsedMs = Math.max(
          1,
          Math.round(performance.now() - startTime)
      );

      self.postMessage({
        id,
        type: "search-done",
        output,
        turns: turns.toString(),
        elapsedMs
      });
      return;
    }

  } catch (err) {
    console.error("worker task failed:", err);
    self.postMessage({
      id,
      type: "error",
      message: String(err)
    });
  }
};
