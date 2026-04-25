(() => {
  const ui = {
    status: document.getElementById("visionStatus"),
    cameraSelect: document.getElementById("visionCameraSelect"),
    inspectRate: document.getElementById("visionInspectRate"),
    permissionButton: document.getElementById("visionPermissionButton"),
    connectButton: document.getElementById("visionConnectButton"),
    resetButton: document.getElementById("visionResetButton"),
    video: document.getElementById("visionVideo"),
    overlay: document.getElementById("visionOverlay"),
    fpsValue: document.getElementById("visionFpsValue"),
    engine: document.getElementById("visionEngine"),
    bridgeStatus: document.getElementById("visionBridgeStatus"),
    encodedPayload: document.getElementById("visionEncodedPayload"),
    turnChip: document.getElementById("visionTurnChip"),
    historyBody: document.getElementById("visionHistoryBody"),
    historyEmpty: document.getElementById("visionHistoryEmpty"),
    historyScroll: document.getElementById("visionHistoryScroll"),
    resetDialog: document.getElementById("visionResetDialog"),
    resetCancel: document.getElementById("visionResetCancel"),
    resetConfirm: document.getElementById("visionResetConfirm"),
    matches: Array.from(document.querySelectorAll("#visionMatches .vision-match-card"))
  };

  if (!ui.status || !ui.video || !ui.overlay) {
    return;
  }

  const BASE_WIDTH = 958;
  const BASE_HEIGHT = 718;
  const RESOURCE_BASES = ["resource", "../erugiosu2/resource"];
  const TEMPLATE_THRESHOLD = 0.82;
  const RESET_LATCH_CLEAR_SCORE = 0.6;
  const WHITE_THRESHOLD = 0.72;
  const MATCH_SLOT_KEYS = ["main", "sub", "ally", "target"];
  const overlayContext = ui.overlay.getContext("2d");
  const processingCanvas = document.createElement("canvas");
  processingCanvas.width = BASE_WIDTH;
  processingCanvas.height = BASE_HEIGHT;
  const processingContext = processingCanvas.getContext("2d", { willReadFrequently: true });

  const ROI_DEFS = {
    main: { x: 78, y: 645, width: 160, height: 70, label: "main" },
    ally: { x: 179, y: 645, width: 160, height: 60, label: "ally" },
    sub: { x: 518, y: 619, width: 100, height: 90, label: "sub" },
    target: { x: 78, y: 578, width: 140, height: 65, label: "target" }
  };

  const TEMPLATE_GROUPS = [
    {
      slot: "main",
      directory: "message_v2",
      files: [
        "ano.png",
        "ayasii.png",
        "critical.png",
        "defense_champion.png",
        "elven.png",
        "erugio.png",
        "erugio2.png",
        "erugio4.png",
        "flee.png",
        "fullheal.png",
        "guard.png",
        "hadou.png",
        "ice.png",
        "kagayaku.png",
        "kuroi.png",
        "madannte.png",
        "meisou.png",
        "merazoma.png",
        "mikawasi.png",
        "mira-.png",
        "miss.png",
        "miss2.png",
        "more_heal.png",
        "mp2.png",
        "no_hadou.png",
        "Paralysis.png",
        "sage.png",
        "samidare.png",
        "samidare2.png",
        "seisui.png",
        "sippuu.png",
        "sleeping2.png",
        "song.png",
        "sukara.png",
        "sutemi.png",
        "tameru.png",
        "tokuyaku.png",
        "WakeUp.png",
        "WakeUp2.png",
        "WakeUp3.png",
        "yaketuku.png",
        "zigosupa.png",
        "zilyoukuu.png"
      ]
    },
    {
      slot: "sub",
      directory: "submessage_v2",
      files: [
        "attack.png",
        "defense_champion2.png",
        "inori.png",
        "Paralysis2.png",
        "reset.png",
        "uhsc.png"
      ]
    },
    {
      slot: "ally",
      directory: "sub2message_v2",
      files: ["a_attack.png", "CareParalysis.png", "dead.png", "dead2.png"]
    },
    {
      slot: "target",
      directory: "target",
      files: ["aha.png", "erugio.png", "erugio2.png", "erugio4.png"]
    }
  ];

  const ACTIONS = {
    1: { names: { ja: "攻撃(敵)", en: "Attack (enemy)" }, ally: false, damage: true },
    2: { names: { ja: "超高速連打", en: "Ultra High Speed Combo" }, ally: false, damage: true },
    5: { names: { ja: "ジゴスパ", en: "Lightning Storm" }, ally: false, damage: true },
    8: { names: { ja: "上空から攻撃", en: "Sky Attack" }, ally: false, damage: true },
    9: { names: { ja: "メラゾーマ", en: "Kafrizzle" }, ally: false, damage: true },
    10: { names: { ja: "凍える吹雪", en: "Freezing Blizzard" }, ally: false, damage: true },
    12: { names: { ja: "あやしいひとみ", en: "Lullab-Eye" }, ally: false, damage: false },
    15: { names: { ja: "笑い", en: "Laugh" }, ally: false, damage: false },
    16: { names: { ja: "凍てつく波動", en: "Disruptive Wave" }, ally: false, damage: false },
    17: { names: { ja: "やけつくいき", en: "Burning Breath" }, ally: false, damage: false },
    18: { names: { ja: "黒輝く息", en: "Dark Breath" }, ally: false, damage: true },
    24: { names: { ja: "麻痺で動けない", en: "Paralysis" }, ally: true, damage: false },
    25: { names: { ja: "攻撃(味方)", en: "Attack (ally)" }, ally: true, damage: true },
    28: { names: { ja: "麻痺回復", en: "Cure Paralysis" }, ally: true, damage: false },
    30: { names: { ja: "スカラ", en: "Buff" }, ally: true, damage: false },
    31: { names: { ja: "ミラーシールド", en: "Magic Mirror" }, ally: true, damage: false },
    32: { names: { ja: "ベホイム", en: "Moreheal" }, ally: true, damage: false },
    33: { names: { ja: "すてみ", en: "Double Up" }, ally: true, damage: false },
    34: { names: { ja: "さみだれ", en: "Multithrust" }, ally: true, damage: true },
    35: { names: { ja: "眠っている！", en: "Sleeping" }, ally: true, damage: false },
    37: { names: { ja: "ベホマ", en: "Fullheal" }, ally: true, damage: false },
    38: { names: { ja: "大防御", en: "Defending Champion" }, ally: true, damage: false },
    39: { names: { ja: "ためる(敵)", en: "Psyche Up (enemy)" }, ally: false, damage: false },
    41: { names: { ja: "瞑想", en: "Meditation" }, ally: false, damage: false },
    42: { names: { ja: "マダンテ", en: "Magic Burst" }, ally: false, damage: true },
    43: { names: { ja: "祈り", en: "Restore MP" }, ally: false, damage: false },
    44: { names: { ja: "しっぷう突き", en: "Mercurial Thrust" }, ally: true, damage: true },
    47: { names: { ja: "賢者聖水", en: "Sage's Elixir" }, ally: true, damage: false },
    48: { names: { ja: "エルフののみぐすり", en: "Elfin Elixir" }, ally: true, damage: false },
    49: { names: { ja: "まほうのせいすい", en: "Magic Water" }, ally: true, damage: false },
    50: { names: { ja: "特薬草", en: "Special Medicine" }, ally: true, damage: false },
    51: { names: { ja: "しんでしまった！", en: "Dead" }, ally: true, damage: false },
    52: { names: { ja: "ゴスペルソング", en: "Gospel Song" }, ally: true, damage: false },
    53: { names: { ja: "逃げる", en: "Flee" }, ally: true, damage: false },
    62: { names: { ja: "ためる(味方)", en: "Psyche Up (ally)" }, ally: true, damage: false }
  };

  const DIRECT_MAIN_RULES = new Map([
    ["sukara.png", 30],
    ["hadou.png", 16],
    ["yaketuku.png", 17],
    ["zilyoukuu.png", 8],
    ["merazoma.png", 9],
    ["mira-.png", 31],
    ["samidare.png", 34],
    ["samidare2.png", 34],
    ["no_hadou.png", 15],
    ["zigosupa.png", 5],
    ["kuroi.png", 18],
    ["sutemi.png", 33],
    ["seisui.png", 49],
    ["meisou.png", 41],
    ["madannte.png", 42],
    ["ice.png", 10],
    ["fullheal.png", 37],
    ["more_heal.png", 32],
    ["ayasii.png", 12],
    ["mp2.png", 43],
    ["song.png", 52],
    ["sippuu.png", 44],
    ["sage.png", 47],
    ["elven.png", 48],
    ["flee.png", 53],
    ["tokuyaku.png", 50]
  ]);

  const state = {
    lang: document.documentElement.dataset.lang || "ja",
    statusKey: "visionStatusIdle",
    bridgeStatusKey: "visionBridgeIdle",
    stream: null,
    loopToken: 0,
    matcher: null,
    templatesBySlot: new Map(),
    lastFrameAt: 0,
    lastFpsAt: 0,
    processedFrames: 0,
    history: [],
    turnIndex: 1,
    actionIndex: 0,
    preAction: -1,
    lastDetectionAt: 0,
    resetLatched: false,
    becameActive: false,
    movementTimer: null
  };

  class BattleEmulatorBridge {
    send(snapshot) {
      const payload = {
        emulator: "erugios-web-placeholder",
        sentAt: new Date().toISOString(),
        currentTurn: snapshot.currentTurn,
        currentSlot: snapshot.currentSlot,
        command: snapshot.command,
        history: snapshot.history
      };
      const encoded = encodeBridgePayload(payload);
      window.postMessage(
        {
          type: "battle-emulator-vision-sync",
          payload,
          encoded
        },
        "*"
      );
      return {
        encoded,
        key: "visionBridgeTurn"
      };
    }
  }

  class WebGpuTemplateMatcher {
    constructor() {
      this.device = null;
      this.queue = null;
      this.pipeline = null;
      this.sampler = null;
      this.frameTexture = null;
      this.frameSize = { width: BASE_WIDTH, height: BASE_HEIGHT };
    }

    async init() {
      if (!navigator.gpu) {
        throw new Error("WebGPU unavailable");
      }
      const adapter = await navigator.gpu.requestAdapter();
      if (!adapter) {
        throw new Error("WebGPU adapter unavailable");
      }
      const device = await adapter.requestDevice();
      this.device = device;
      this.queue = device.queue;
      this.sampler = device.createSampler({
        magFilter: "nearest",
        minFilter: "nearest"
      });
      this.frameTexture = device.createTexture({
        size: [BASE_WIDTH, BASE_HEIGHT, 1],
        format: "rgba8unorm",
        usage: GPUTextureUsage.COPY_DST | GPUTextureUsage.TEXTURE_BINDING
      });
      const shader = device.createShaderModule({
        code: `
struct Params {
  roiX: u32,
  roiY: u32,
  roiWidth: u32,
  roiHeight: u32,
  templateWidth: u32,
  templateHeight: u32,
  scoreWidth: u32,
  scoreHeight: u32,
  threshold: f32,
  whiteWeight: f32,
  darkWeight: f32,
  pad: f32,
};

@group(0) @binding(0) var frameTex: texture_2d<f32>;
@group(0) @binding(1) var templateTex: texture_2d<f32>;
@group(0) @binding(2) var<storage, read_write> scores: array<f32>;
@group(0) @binding(3) var<uniform> params: Params;

fn luminance(rgb: vec3<f32>) -> f32 {
  return dot(rgb, vec3<f32>(0.2126, 0.7152, 0.0722));
}

@compute @workgroup_size(8, 8, 1)
fn main(@builtin(global_invocation_id) gid: vec3<u32>) {
  if (gid.x >= params.scoreWidth || gid.y >= params.scoreHeight) {
    return;
  }

  var score = 0.0;
  var total = 0.0;

  for (var y: u32 = 0u; y < params.templateHeight; y = y + 1u) {
    for (var x: u32 = 0u; x < params.templateWidth; x = x + 1u) {
      let framePos = vec2<i32>(i32(params.roiX + gid.x + x), i32(params.roiY + gid.y + y));
      let templatePos = vec2<i32>(i32(x), i32(y));
      let frameL = luminance(textureLoad(frameTex, framePos, 0).rgb);
      let templateL = luminance(textureLoad(templateTex, templatePos, 0).rgb);
      let frameWhite = select(0.0, 1.0, frameL >= params.threshold);
      let templateWhite = select(0.0, 1.0, templateL >= params.threshold);
      let weight = select(params.darkWeight, params.whiteWeight, templateWhite > 0.5);
      total = total + weight;
      if (abs(frameWhite - templateWhite) < 0.1) {
        score = score + weight;
      }
    }
  }

  let index = gid.y * params.scoreWidth + gid.x;
  scores[index] = select(0.0, score / total, total > 0.0);
}
`
      });
      this.pipeline = device.createComputePipeline({
        layout: "auto",
        compute: {
          module: shader,
          entryPoint: "main"
        }
      });
    }

    async createTemplate(template) {
      const texture = this.device.createTexture({
        size: [template.width, template.height, 1],
        format: "rgba8unorm",
        usage: GPUTextureUsage.COPY_DST | GPUTextureUsage.TEXTURE_BINDING
      });
      this.queue.copyExternalImageToTexture(
        { source: template.bitmap },
        { texture },
        [template.width, template.height]
      );
      return {
        ...template,
        texture
      };
    }

    uploadFrame(source) {
      this.queue.copyExternalImageToTexture(
        { source },
        { texture: this.frameTexture },
        [BASE_WIDTH, BASE_HEIGHT]
      );
    }

    async matchSlot(slot, templates) {
      const roi = ROI_DEFS[slot];
      let best = emptyMatch(slot);
      for (const template of templates) {
        const scoreWidth = roi.width - template.width + 1;
        const scoreHeight = roi.height - template.height + 1;
        if (scoreWidth < 1 || scoreHeight < 1) {
          continue;
        }
        const result = await this.matchTemplate(roi, template, scoreWidth, scoreHeight);
        if (result.score > best.score) {
          best = {
            slot,
            file: template.file,
            score: result.score,
            x: roi.x + result.x,
            y: roi.y + result.y,
            width: template.width,
            height: template.height
          };
        }
      }
      return best;
    }

    async matchTemplate(roi, template, scoreWidth, scoreHeight) {
      const scoreCount = scoreWidth * scoreHeight;
      const scoreBuffer = this.device.createBuffer({
        size: scoreCount * 4,
        usage: GPUBufferUsage.STORAGE | GPUBufferUsage.COPY_SRC
      });
      const readBuffer = this.device.createBuffer({
        size: scoreCount * 4,
        usage: GPUBufferUsage.COPY_DST | GPUBufferUsage.MAP_READ
      });
      const paramsBuffer = new ArrayBuffer(48);
      const paramsView = new DataView(paramsBuffer);
      paramsView.setUint32(0, roi.x, true);
      paramsView.setUint32(4, roi.y, true);
      paramsView.setUint32(8, roi.width, true);
      paramsView.setUint32(12, roi.height, true);
      paramsView.setUint32(16, template.width, true);
      paramsView.setUint32(20, template.height, true);
      paramsView.setUint32(24, scoreWidth, true);
      paramsView.setUint32(28, scoreHeight, true);
      paramsView.setFloat32(32, WHITE_THRESHOLD, true);
      paramsView.setFloat32(36, 2.2, true);
      paramsView.setFloat32(40, 1.0, true);
      paramsView.setFloat32(44, 0, true);
      const uniformBuffer = this.device.createBuffer({
        size: paramsBuffer.byteLength,
        usage: GPUBufferUsage.UNIFORM | GPUBufferUsage.COPY_DST
      });
      this.queue.writeBuffer(uniformBuffer, 0, paramsBuffer);
      const bindGroup = this.device.createBindGroup({
        layout: this.pipeline.getBindGroupLayout(0),
        entries: [
          { binding: 0, resource: this.frameTexture.createView() },
          { binding: 1, resource: template.texture.createView() },
          { binding: 2, resource: { buffer: scoreBuffer } },
          { binding: 3, resource: { buffer: uniformBuffer } }
        ]
      });
      const encoder = this.device.createCommandEncoder();
      const pass = encoder.beginComputePass();
      pass.setPipeline(this.pipeline);
      pass.setBindGroup(0, bindGroup);
      pass.dispatchWorkgroups(Math.ceil(scoreWidth / 8), Math.ceil(scoreHeight / 8));
      pass.end();
      encoder.copyBufferToBuffer(scoreBuffer, 0, readBuffer, 0, scoreCount * 4);
      this.queue.submit([encoder.finish()]);
      await readBuffer.mapAsync(GPUMapMode.READ);
      const scores = new Float32Array(readBuffer.getMappedRange()).slice();
      readBuffer.unmap();
      scoreBuffer.destroy();
      readBuffer.destroy();
      uniformBuffer.destroy();
      let bestScore = 0;
      let bestIndex = 0;
      for (let index = 0; index < scores.length; index += 1) {
        if (scores[index] > bestScore) {
          bestScore = scores[index];
          bestIndex = index;
        }
      }
      return {
        score: bestScore,
        x: bestIndex % scoreWidth,
        y: Math.floor(bestIndex / scoreWidth)
      };
    }

    async match(source, templatesBySlot) {
      this.uploadFrame(source);
      const entries = await Promise.all(
        MATCH_SLOT_KEYS.map(async (slot) => [slot, await this.matchSlot(slot, templatesBySlot.get(slot) || [])])
      );
      return Object.fromEntries(entries);
    }
  }

  class CpuTemplateMatcher {
    constructor() {
      this.frameCanvas = document.createElement("canvas");
      this.frameCanvas.width = BASE_WIDTH;
      this.frameCanvas.height = BASE_HEIGHT;
      this.frameContext = this.frameCanvas.getContext("2d", { willReadFrequently: true });
    }

    async init() {}

    async createTemplate(template) {
      const canvas = document.createElement("canvas");
      canvas.width = template.width;
      canvas.height = template.height;
      const context = canvas.getContext("2d", { willReadFrequently: true });
      context.drawImage(template.bitmap, 0, 0);
      return {
        ...template,
        mask: buildBinaryMask(context.getImageData(0, 0, template.width, template.height))
      };
    }

    async match(source, templatesBySlot) {
      this.frameContext.drawImage(source, 0, 0, BASE_WIDTH, BASE_HEIGHT);
      const frame = this.frameContext.getImageData(0, 0, BASE_WIDTH, BASE_HEIGHT);
      const matches = {};
      for (const slot of MATCH_SLOT_KEYS) {
        matches[slot] = this.matchSlot(frame, ROI_DEFS[slot], templatesBySlot.get(slot) || [], slot);
      }
      return matches;
    }

    matchSlot(frame, roi, templates, slot) {
      let best = emptyMatch(slot);
      for (const template of templates) {
        const maxX = roi.width - template.width;
        const maxY = roi.height - template.height;
        for (let offsetY = 0; offsetY <= maxY; offsetY += 1) {
          for (let offsetX = 0; offsetX <= maxX; offsetX += 1) {
            const score = compareMask(frame, template.mask, roi.x + offsetX, roi.y + offsetY, template.width, template.height);
            if (score > best.score) {
              best = {
                slot,
                file: template.file,
                score,
                x: roi.x + offsetX,
                y: roi.y + offsetY,
                width: template.width,
                height: template.height
              };
            }
          }
        }
      }
      return best;
    }
  }

  function buildBinaryMask(imageData) {
    const mask = new Uint8Array(imageData.width * imageData.height);
    const data = imageData.data;
    for (let index = 0; index < mask.length; index += 1) {
      const offset = index * 4;
      const luminance =
        (data[offset] * 0.2126 + data[offset + 1] * 0.7152 + data[offset + 2] * 0.0722) / 255;
      mask[index] = luminance >= WHITE_THRESHOLD ? 1 : 0;
    }
    return mask;
  }

  function compareMask(frame, templateMask, x, y, width, height) {
    const data = frame.data;
    let score = 0;
    let total = 0;
    for (let row = 0; row < height; row += 1) {
      for (let col = 0; col < width; col += 1) {
        const frameIndex = ((y + row) * frame.width + (x + col)) * 4;
        const pixel =
          (data[frameIndex] * 0.2126 + data[frameIndex + 1] * 0.7152 + data[frameIndex + 2] * 0.0722) / 255;
        const frameWhite = pixel >= WHITE_THRESHOLD ? 1 : 0;
        const templateWhite = templateMask[row * width + col];
        const weight = templateWhite ? 2.2 : 1;
        total += weight;
        if (frameWhite === templateWhite) {
          score += weight;
        }
      }
    }
    return total ? score / total : 0;
  }

  function emptyMatch(slot) {
    const roi = ROI_DEFS[slot];
    return {
      slot,
      file: "",
      score: 0,
      x: roi.x,
      y: roi.y,
      width: roi.width,
      height: roi.height
    };
  }

  function getDictionary() {
    const config = window.APP_CONFIG || {};
    return config.i18n ? config.i18n[state.lang] : {};
  }

  function t(key, fallback) {
    const dictionary = getDictionary();
    return dictionary[key] || fallback;
  }

  function getActionLabel(actionId) {
    const action = ACTIONS[actionId];
    if (!action) {
      return `#${actionId}`;
    }
    return action.names[state.lang] || action.names.ja || action.names.en;
  }

  function setStatus(key) {
    state.statusKey = key;
    ui.status.textContent = t(key, key);
  }

  function setBridgeStatus(key, encoded) {
    state.bridgeStatusKey = key;
    ui.bridgeStatus.textContent = t(key, key);
    ui.encodedPayload.value = encoded || "";
  }

  function updateTurnChip() {
    ui.turnChip.textContent = `T${state.turnIndex} / A${state.actionIndex + 1}`;
  }

  function scheduleMovementSafety() {
    if (state.movementTimer !== null) {
      return;
    }
    state.movementTimer = window.setTimeout(() => {
      state.becameActive = false;
      state.movementTimer = null;
    }, 1500);
  }

  function initMovementSafety() {
    const throttled = throttle(() => {
      if (state.becameActive) {
        scheduleMovementSafety();
      }
    }, 200);
    window.addEventListener("mousemove", throttled);
    document.addEventListener("mouseenter", () => {
      state.becameActive = true;
      if (state.movementTimer !== null) {
        clearTimeout(state.movementTimer);
        state.movementTimer = null;
      }
      scheduleMovementSafety();
    });
  }

  function throttle(fn, wait) {
    let lastTime = 0;
    return (...args) => {
      const now = Date.now();
      if (now - lastTime >= wait) {
        lastTime = now;
        fn(...args);
      }
    };
  }

  function updateMatchCards(matches) {
    ui.matches.forEach((card) => {
      const slot = card.dataset.slot;
      const match = matches[slot] || emptyMatch(slot);
      const nameNode = card.querySelector(".vision-match-name");
      const scoreNode = card.querySelector(".vision-match-score");
      nameNode.textContent = match.file || "-";
      scoreNode.textContent = `${(match.score * 100).toFixed(1)}%`;
    });
  }

  function drawOverlay(matches) {
    overlayContext.clearRect(0, 0, BASE_WIDTH, BASE_HEIGHT);
    overlayContext.lineWidth = 3;
    overlayContext.font = "18px Bahnschrift, sans-serif";
    for (const slot of MATCH_SLOT_KEYS) {
      const roi = ROI_DEFS[slot];
      const match = matches[slot] || emptyMatch(slot);
      overlayContext.strokeStyle = "rgba(230, 230, 230, 0.72)";
      overlayContext.strokeRect(roi.x, roi.y, roi.width, roi.height);
      if (match.file && match.score >= TEMPLATE_THRESHOLD) {
        overlayContext.strokeStyle = "rgba(255, 187, 92, 0.92)";
        overlayContext.strokeRect(match.x, match.y, match.width, match.height);
        overlayContext.fillStyle = "rgba(17, 22, 27, 0.78)";
        const label = `${slot}: ${match.file} ${(match.score * 100).toFixed(1)}%`;
        const textWidth = overlayContext.measureText(label).width;
        const labelY = Math.max(26, match.y - 10);
        overlayContext.fillRect(match.x, labelY - 18, textWidth + 16, 24);
        overlayContext.fillStyle = "rgba(255, 248, 236, 0.96)";
        overlayContext.fillText(label, match.x + 8, labelY);
      }
    }
  }

  function getScaledFpsTarget() {
    const parsed = Number.parseInt(ui.inspectRate.value, 10);
    if (!Number.isFinite(parsed)) {
      return 3;
    }
    return Math.min(12, Math.max(1, parsed));
  }

  function updateFps(now) {
    state.processedFrames += 1;
    if (!state.lastFpsAt) {
      state.lastFpsAt = now;
      return;
    }
    const elapsed = now - state.lastFpsAt;
    if (elapsed >= 1000) {
      const fps = (state.processedFrames * 1000) / elapsed;
      ui.fpsValue.textContent = `${fps.toFixed(1)} fps`;
      state.lastFpsAt = now;
      state.processedFrames = 0;
    }
  }

  function pickCandidate(matches) {
    const main = matches.main || emptyMatch("main");
    const sub = matches.sub || emptyMatch("sub");
    const ally = matches.ally || emptyMatch("ally");
    const target = matches.target || emptyMatch("target");
    const erugioMain = isErugio(main.file);

    if (erugioMain && sub.file === "reset.png" && main.score >= TEMPLATE_THRESHOLD && sub.score >= TEMPLATE_THRESHOLD) {
      return { kind: "reset", score: Math.min(main.score, sub.score), detail: `${main.file} + reset.png` };
    }
    if (erugioMain && sub.file === "attack.png" && main.score >= TEMPLATE_THRESHOLD && sub.score >= TEMPLATE_THRESHOLD) {
      return {
        kind: "action",
        actionId: 1,
        detail: `${main.file} + attack.png`,
        score: Math.min(main.score, sub.score)
      };
    }
    if (erugioMain && sub.file === "uhsc.png" && main.score >= TEMPLATE_THRESHOLD && sub.score >= TEMPLATE_THRESHOLD) {
      return {
        kind: "action",
        actionId: 2,
        detail: `${main.file} + uhsc.png`,
        score: Math.min(main.score, sub.score)
      };
    }
    if (
      ally.file === "a_attack.png" &&
      ally.score >= TEMPLATE_THRESHOLD &&
      sub.file !== "uhsc.png" &&
      main.file !== "guard.png"
    ) {
      return {
        kind: "action",
        actionId: 25,
        detail: `a_attack.png (${main.file || "-"})`,
        score: ally.score
      };
    }
    if (
      main.file === "defense_champion.png" &&
      sub.file === "defense_champion2.png" &&
      main.score >= TEMPLATE_THRESHOLD &&
      sub.score >= TEMPLATE_THRESHOLD
    ) {
      return {
        kind: "action",
        actionId: 38,
        detail: "defense_champion combo",
        score: Math.min(main.score, sub.score)
      };
    }
    if (main.file === "Paralysis.png" && ally.file === "CareParalysis.png" && ally.score >= TEMPLATE_THRESHOLD) {
      return {
        kind: "action",
        actionId: 28,
        detail: "Paralysis + CareParalysis",
        score: Math.min(main.score, ally.score)
      };
    }
    if (
      main.file === "Paralysis.png" &&
      main.score >= TEMPLATE_THRESHOLD &&
      (!sub.file || sub.file !== "Paralysis2.png" || sub.score < TEMPLATE_THRESHOLD)
    ) {
      return {
        kind: "action",
        actionId: 24,
        detail: "Paralysis",
        score: main.score
      };
    }
    if (main.file === "sleeping2.png" && (ally.file === "dead.png" || ally.file === "dead2.png") && ally.score >= TEMPLATE_THRESHOLD) {
      return {
        kind: "action",
        actionId: 51,
        detail: `sleeping2 + ${ally.file}`,
        score: Math.min(main.score, ally.score)
      };
    }
    if (main.file === "sleeping2.png" && main.score >= TEMPLATE_THRESHOLD) {
      return {
        kind: "action",
        actionId: 35,
        detail: "sleeping2",
        score: main.score
      };
    }
    if (main.file === "tameru.png" && main.score >= TEMPLATE_THRESHOLD) {
      return {
        kind: "action",
        actionId: target.file === "aha.png" && target.score >= TEMPLATE_THRESHOLD ? 62 : 39,
        detail: target.file === "aha.png" ? "tameru + aha" : "tameru",
        score: Math.min(main.score, target.score || 1)
      };
    }
    const directAction = DIRECT_MAIN_RULES.get(main.file);
    if (directAction && main.score >= TEMPLATE_THRESHOLD) {
      return {
        kind: "action",
        actionId: directAction,
        detail: main.file,
        score: main.score
      };
    }
    return null;
  }

  function isErugio(file) {
    return file === "erugio.png" || file === "erugio2.png" || file === "erugio4.png";
  }

  function maybeResetFromCombo(candidate) {
    if (candidate && candidate.kind === "reset") {
      if (!state.resetLatched) {
        state.resetLatched = true;
        resetConsoleState();
      }
      return true;
    }
    if (!candidate || candidate.score < RESET_LATCH_CLEAR_SCORE) {
      state.resetLatched = false;
    }
    return false;
  }

  function resetConsoleState() {
    state.history = [];
    state.turnIndex = 1;
    state.actionIndex = 0;
    state.preAction = -1;
    state.lastDetectionAt = 0;
    renderHistory();
    updateTurnChip();
    setBridgeStatus("visionBridgeReady", "");
  }

  function renderHistory() {
    ui.historyBody.innerHTML = "";
    if (!state.history.length) {
      ui.historyEmpty.hidden = false;
      ui.historyScroll.hidden = true;
      return;
    }
    ui.historyEmpty.hidden = true;
    ui.historyScroll.hidden = false;
    state.history.forEach((entry) => {
      const row = document.createElement("tr");
      const turnCell = document.createElement("td");
      turnCell.textContent = `T${entry.turn}`;
      const slotCell = document.createElement("td");
      slotCell.textContent = `A${entry.slot}`;
      const actionCell = document.createElement("td");
      actionCell.textContent = getActionLabel(entry.actionId);
      const detailCell = document.createElement("td");
      detailCell.textContent = entry.detail;
      const scoreCell = document.createElement("td");
      scoreCell.textContent = `${(entry.score * 100).toFixed(1)}%`;
      row.append(turnCell, slotCell, actionCell, detailCell, scoreCell);
      ui.historyBody.appendChild(row);
    });
  }

  function buildConsoleCommand() {
    const enemyActions = [];
    const allyActions = [];
    const damages = [];
    state.history.forEach((entry) => {
      const action = ACTIONS[entry.actionId];
      if (!action) {
        return;
      }
      if (action.ally) {
        allyActions.push(entry.actionId);
      } else {
        enemyActions.push(entry.actionId);
      }
      if (action.damage) {
        damages.push(0);
      }
    });
    return `b 0 0 0 ${Math.max(0, state.turnIndex - 1)} ${enemyActions.join(" ")}-${allyActions.join(" ")}-${damages.join(" ")}-`;
  }

  function acceptCandidate(candidate) {
    if (!candidate || candidate.kind !== "action") {
      if (Date.now() - state.lastDetectionAt > 5000) {
        state.preAction = -1;
      }
      return;
    }
    if (candidate.actionId === state.preAction) {
      state.lastDetectionAt = Date.now();
      return;
    }
    state.preAction = candidate.actionId;
    state.lastDetectionAt = Date.now();
    const entry = {
      turn: state.turnIndex,
      slot: state.actionIndex + 1,
      actionId: candidate.actionId,
      detail: candidate.detail,
      score: candidate.score
    };
    state.history.push(entry);
    if (state.actionIndex === 2) {
      state.actionIndex = 0;
      state.turnIndex += 1;
    } else {
      state.actionIndex += 1;
    }
    renderHistory();
    updateTurnChip();
    const bridge = new BattleEmulatorBridge();
    const sync = bridge.send({
      currentTurn: state.turnIndex,
      currentSlot: state.actionIndex + 1,
      command: buildConsoleCommand(),
      history: state.history.map((item) => ({
        turn: item.turn,
        slot: item.slot,
        actionId: item.actionId,
        actionLabel: getActionLabel(item.actionId),
        detail: item.detail
      }))
    });
    setBridgeStatus(sync.key, sync.encoded);
  }

  function encodeBridgePayload(payload) {
    const json = JSON.stringify(payload);
    const bytes = utf8Encode(json);
    return base64UrlEncode(bytes);
  }

  function utf8Encode(text) {
    const bytes = [];
    for (let index = 0; index < text.length; index += 1) {
      let codePoint = text.codePointAt(index);
      if (codePoint > 0xffff) {
        index += 1;
      }
      if (codePoint <= 0x7f) {
        bytes.push(codePoint);
      } else if (codePoint <= 0x7ff) {
        bytes.push(0xc0 | (codePoint >> 6), 0x80 | (codePoint & 0x3f));
      } else if (codePoint <= 0xffff) {
        bytes.push(
          0xe0 | (codePoint >> 12),
          0x80 | ((codePoint >> 6) & 0x3f),
          0x80 | (codePoint & 0x3f)
        );
      } else {
        bytes.push(
          0xf0 | (codePoint >> 18),
          0x80 | ((codePoint >> 12) & 0x3f),
          0x80 | ((codePoint >> 6) & 0x3f),
          0x80 | (codePoint & 0x3f)
        );
      }
    }
    return bytes;
  }

  function base64UrlEncode(bytes) {
    const alphabet = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789-_";
    let output = "";
    for (let index = 0; index < bytes.length; index += 3) {
      const a = bytes[index] ?? 0;
      const b = bytes[index + 1] ?? 0;
      const c = bytes[index + 2] ?? 0;
      const triple = (a << 16) | (b << 8) | c;
      output += alphabet[(triple >> 18) & 0x3f];
      output += alphabet[(triple >> 12) & 0x3f];
      output += index + 1 < bytes.length ? alphabet[(triple >> 6) & 0x3f] : "";
      output += index + 2 < bytes.length ? alphabet[triple & 0x3f] : "";
    }
    return output;
  }

  async function loadBitmap(path) {
    const response = await fetch(path, { cache: "force-cache" });
    if (!response.ok) {
      throw new Error(path);
    }
    const blob = await response.blob();
    return createImageBitmap(blob);
  }

  async function loadTemplateFile(group, file) {
    for (const base of RESOURCE_BASES) {
      const url = `${base}/${group.directory}/${file}`;
      try {
        const bitmap = await loadBitmap(url);
        return {
          slot: group.slot,
          file,
          path: url,
          bitmap,
          width: bitmap.width,
          height: bitmap.height
        };
      } catch (error) {}
    }
    throw new Error(`template missing: ${group.directory}/${file}`);
  }

  async function loadTemplates(matcher) {
    const templatesBySlot = new Map();
    for (const group of TEMPLATE_GROUPS) {
      const templates = [];
      for (const file of group.files) {
        const template = await loadTemplateFile(group, file);
        templates.push(await matcher.createTemplate(template));
      }
      templatesBySlot.set(group.slot, templates);
    }
    return templatesBySlot;
  }

  async function createMatcher() {
    try {
      const matcher = new WebGpuTemplateMatcher();
      await matcher.init();
      ui.engine.textContent = "WebGPU";
      setStatus("visionStatusReady");
      return matcher;
    } catch (error) {
      const matcher = new CpuTemplateMatcher();
      await matcher.init();
      ui.engine.textContent = "CPU fallback";
      setStatus("visionStatusFallback");
      return matcher;
    }
  }

  async function populateCameras() {
    if (!navigator.mediaDevices || !navigator.mediaDevices.enumerateDevices) {
      throw new Error("media devices unavailable");
    }
    const devices = await navigator.mediaDevices.enumerateDevices();
    const cameras = devices.filter((device) => device.kind === "videoinput");
    ui.cameraSelect.innerHTML = "";
    cameras.forEach((camera, index) => {
      const option = document.createElement("option");
      option.value = camera.deviceId;
      option.textContent = camera.label || `camera-${index + 1}`;
      ui.cameraSelect.appendChild(option);
    });

    // ここを追加
    const obsCamera = cameras.find((camera) =>
        camera.label.toLowerCase().includes("obs virtual camera")
    );
    if (obsCamera) {
      ui.cameraSelect.value = obsCamera.deviceId;
    }

    if (!cameras.length) {
      const option = document.createElement("option");
      option.value = "";
      option.textContent = "camera unavailable";
      ui.cameraSelect.appendChild(option);
    }
  }

  async function requestCameraPermission() {
    ui.permissionButton.disabled = true;
    try {
      const stream = await navigator.mediaDevices.getUserMedia({
        audio: false,
        video: { width: { ideal: BASE_WIDTH }, height: { ideal: BASE_HEIGHT } }
      });
      stream.getTracks().forEach((track) => track.stop());
      await populateCameras();
      setStatus("visionStatusReady");
    } catch (error) {
      setStatus("visionStatusError");
      console.error("camera permission error:", error);
    } finally {
      ui.permissionButton.disabled = false;
    }
  }

  async function connectCamera() {
    ui.connectButton.disabled = true;
    try {
      const deviceId = ui.cameraSelect.value;
      const constraints = {
        audio: false,
        video: deviceId
          ? { deviceId: { exact: deviceId }, width: { ideal: BASE_WIDTH }, height: { ideal: BASE_HEIGHT } }
          : { width: { ideal: BASE_WIDTH }, height: { ideal: BASE_HEIGHT } }
      };
      if (state.stream) {
        state.stream.getTracks().forEach((track) => track.stop());
      }
      const stream = await navigator.mediaDevices.getUserMedia(constraints);
      state.stream = stream;
      ui.video.srcObject = stream;
      await ui.video.play();
      await populateCameras();
      if (!state.matcher) {
        state.matcher = await createMatcher();
        state.templatesBySlot = await loadTemplates(state.matcher);
      }
      state.lastFrameAt = 0;
      state.lastFpsAt = 0;
      state.processedFrames = 0;
      setBridgeStatus("visionBridgeReady", ui.encodedPayload.value);
      setStatus("visionStatusWatching");
      startLoop();
    } catch (error) {
      setStatus("visionStatusError");
      setBridgeStatus("visionBridgeIdle", "");
    } finally {
      ui.connectButton.disabled = false;
    }
  }

  function startLoop() {
    state.loopToken += 1;
    const token = state.loopToken;
    const runFrame = async (now) => {
      if (token !== state.loopToken || !state.stream || !state.matcher) {
        return;
      }
      const targetInterval = 1000 / getScaledFpsTarget();
      if (!state.lastFrameAt || now - state.lastFrameAt >= targetInterval) {
        state.lastFrameAt = now;
        processingContext.drawImage(ui.video, 0, 0, BASE_WIDTH, BASE_HEIGHT);
        const matches = await state.matcher.match(processingCanvas, state.templatesBySlot);
        updateMatchCards(matches);
        drawOverlay(matches);
        const candidate = pickCandidate(matches);
        if (!maybeResetFromCombo(candidate)) {
          acceptCandidate(candidate);
        }
        updateFps(now);
      }
      queueLoop(runFrame);
    };
    queueLoop(runFrame);
  }

  function queueLoop(callback) {
    if ("requestVideoFrameCallback" in HTMLVideoElement.prototype) {
      ui.video.requestVideoFrameCallback((_, meta) => callback(meta.expectedDisplayTime || performance.now()));
    } else {
      window.requestAnimationFrame(callback);
    }
  }

  function syncLanguage() {
    state.lang = document.documentElement.dataset.lang || "ja";
    setStatus(state.statusKey);
    setBridgeStatus(state.bridgeStatusKey, ui.encodedPayload.value);
    renderHistory();
  }

  function openResetDialog() {
    if (state.becameActive) {
      scheduleMovementSafety();
    }
    ui.resetDialog.hidden = false;
  }

  function closeResetDialog() {
    ui.resetDialog.hidden = true;
  }

  function initEvents() {
    ui.permissionButton.addEventListener("click", () => {
      requestCameraPermission();
    });
    ui.connectButton.addEventListener("click", () => {
      connectCamera();
    });
    ui.resetButton.addEventListener("click", () => {
      openResetDialog();
    });
    ui.resetCancel.addEventListener("click", () => {
      closeResetDialog();
    });
    ui.resetConfirm.addEventListener("click", () => {
      closeResetDialog();
      resetConsoleState();
    });
    navigator.mediaDevices?.addEventListener?.("devicechange", () => {
      populateCameras().catch(() => {});
    });
    const observer = new MutationObserver(() => {
      syncLanguage();
    });
    observer.observe(document.documentElement, {
      attributes: true,
      attributeFilter: ["data-lang"]
    });
  }

  async function init() {
    updateTurnChip();
    renderHistory();
    initMovementSafety();
    initEvents();
    try {
      if (navigator.mediaDevices?.getUserMedia) {
        setStatus("visionStatusIdle");
      }
      await populateCameras();
    } catch (error) {
      setStatus("visionStatusIdle");
    }
  }

  init();
})();
