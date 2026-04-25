(() => {
    const ui = {
        status: document.getElementById("visionStatus"),
        cameraSelect: document.getElementById("visionCameraSelect"),
        inspectRate: document.getElementById("visionInspectRate"),
        permissionButton: document.getElementById("visionPermissionButton"),
        connectButton: document.getElementById("visionConnectButton"),
        resetButton: document.getElementById("visionResetButton"),
        debugExportButton: document.getElementById("visionDebugExportButton"),
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
        gpuWarningDialog: document.getElementById("visionGpuWarningDialog"),
        gpuWarningCancel: document.getElementById("visionGpuWarningCancel"),
        gpuWarningStart: document.getElementById("visionGpuWarningStart"),
        matches: Array.from(document.querySelectorAll("#visionMatches .vision-match-card"))
    };

    if (!ui.status || !ui.video || !ui.overlay) {
        return;
    }

    const BASE_WIDTH = 958;
    const BASE_HEIGHT = 718;
    const SOURCE_1080P = {width: 1920, height: 1080};
    const SOURCE_720P = {width: 1280, height: 720};
    const VISION_ASSET_PACK_URL = "vision-assets.json";
    const TEMPLATE_THRESHOLD = 0.45;
    const RESET_LATCH_CLEAR_SCORE = 0.6;
    const WHITE_THRESHOLD = 0.72;
    const ACTION_THRESHOLD = 0.45;
    const NUMBER_THRESHOLD = 0.80;
    const MATCH_PENALTY_WEIGHT = 0.0;
    const MATCH_WHITE_WEIGHT = 1.0;
    const MATCH_CONTRAST = 1.28;
    const MATCH_BIAS = 0.03;
    const TEMPLATE_ALPHA_THRESHOLD = 0.05;
    const MATCH_SLOT_KEYS = ["main", "sub", "ally", "target"];
    const overlayContext = ui.overlay.getContext("2d");
    const processingCanvas = document.createElement("canvas");
    processingCanvas.width = BASE_WIDTH;
    processingCanvas.height = BASE_HEIGHT;
    const processingContext = processingCanvas.getContext("2d", {willReadFrequently: true});
    processingContext.imageSmoothingEnabled = false;

    const ROI_DEFS = {
        main: {x: 78, y: 645, width: 160, height: 70, label: "main"},
        ally: {x: 179, y: 645, width: 160, height: 60, label: "ally"},
        sub: {x: 518, y: 619, width: 100, height: 90, label: "sub"},
        target: {x: 78, y: 578, width: 140, height: 65, label: "target"}
    };

    const NUMBER_TEMPLATE_FILES = [
        "0.png",
        "0_2.png",
        "0_3.png",
        "0_4.png",
        "0_zep1.png",
        "0_zpe2.png",
        "1.png",
        "1_1.png",
        "1_10.png",
        "1_11.png",
        "1_12.png",
        "1_2.png",
        "1_3.png",
        "1_4.png",
        "1_5.png",
        "1_6.png",
        "1_7.png",
        "1_8.png",
        "1_9.png",
        "1_zep1.png",
        "1_zepp1.png",
        "1_zepp13.png",
        "1_zepp14.png",
        "1_zepp16.png",
        "1_zepp17.png",
        "2.png",
        "2_1.png",
        "2_2.png",
        "2_3.png",
        "2_4.png",
        "2_5.png",
        "2_zep1.png",
        "2_zep2.png",
        "2_zep4.png",
        "2_zepp2.png",
        "3.png",
        "3_1.png",
        "3_2.png",
        "3_3.png",
        "3_4.png",
        "3_5.png",
        "3_6.png",
        "3_7.png",
        "3_zep1.png",
        "3_zep3.png",
        "4.png",
        "4_1.png",
        "4_2.png",
        "4_3.png",
        "4_4.png",
        "4_5.png",
        "4_6.png",
        "4_zep1.png",
        "4_zep2.png",
        "4_zep4.png",
        "4_zep5.png",
        "4_zpp5.png",
        "5.png",
        "5_2.png",
        "5_zep1.png",
        "5_zep5.png",
        "5_zepp1.png",
        "6.png",
        "6_1.png",
        "6_12.png",
        "6_zep2.png",
        "6_zepp1.png",
        "6_zepppp.png",
        "7.png",
        "7_2.png",
        "7_3.png",
        "7_4.png",
        "7_zep1.png",
        "7_zeppp.png",
        "7_zpe2.png",
        "8.png",
        "8_1.png",
        "8_3.png",
        "8_4.png",
        "8_5.png",
        "8_6.png",
        "8_zep1.png",
        "8_zep7.png",
        "8_zepp.png",
        "9.png",
        "9_2.png",
        "9_zep1.png",
        "9_zep2.png",
        "9_zepp.png"
    ];

    const DAMAGE_ROIS = {
        damage1: {
            x: 78,
            y: 645,
            width: 160,
            height: 70,
            actionAreas: [
                {x: 0, y: 0, width: 60, height: 60},
                {x: 55, y: 0, width: 60, height: 60},
                {x: 105, y: 0, width: 60, height: 60}
            ]
        },
        damage2: {
            x: 179,
            y: 645,
            width: 160,
            height: 60,
            actionAreas: [
                {x: 0, y: 0, width: 60, height: 60},
                {x: 43, y: 0, width: 60, height: 60},
                {x: 87, y: 0, width: 60, height: 60}
            ]
        }
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
        1: {names: {ja: "攻撃(敵)", en: "Attack (enemy)"}, ally: false, damage: true},
        2: {names: {ja: "超高速連打", en: "Ultra High Speed Combo"}, ally: false, damage: true},
        5: {names: {ja: "ジゴスパ", en: "Lightning Storm"}, ally: false, damage: true},
        8: {names: {ja: "上空から攻撃", en: "Sky Attack"}, ally: false, damage: true},
        9: {names: {ja: "メラゾーマ", en: "Kafrizzle"}, ally: false, damage: true},
        10: {names: {ja: "凍える吹雪", en: "Freezing Blizzard"}, ally: false, damage: true},
        12: {names: {ja: "あやしいひとみ", en: "Lullab-Eye"}, ally: false, damage: false},
        15: {names: {ja: "笑い", en: "Laugh"}, ally: false, damage: false},
        16: {names: {ja: "凍てつく波動", en: "Disruptive Wave"}, ally: false, damage: false},
        17: {names: {ja: "やけつくいき", en: "Burning Breath"}, ally: false, damage: false},
        18: {names: {ja: "黒輝く息", en: "Dark Breath"}, ally: false, damage: true},
        24: {names: {ja: "麻痺で動けない", en: "Paralysis"}, ally: true, damage: false},
        25: {names: {ja: "攻撃(味方)", en: "Attack (ally)"}, ally: true, damage: true},
        28: {names: {ja: "麻痺回復", en: "Cure Paralysis"}, ally: true, damage: false},
        30: {names: {ja: "スカラ", en: "Buff"}, ally: true, damage: false},
        31: {names: {ja: "ミラーシールド", en: "Magic Mirror"}, ally: true, damage: false},
        32: {names: {ja: "ベホイム", en: "Moreheal"}, ally: true, damage: false},
        33: {names: {ja: "すてみ", en: "Double Up"}, ally: true, damage: false},
        34: {names: {ja: "さみだれ", en: "Multithrust"}, ally: true, damage: true},
        35: {names: {ja: "眠っている！", en: "Sleeping"}, ally: true, damage: false},
        37: {names: {ja: "ベホマ", en: "Fullheal"}, ally: true, damage: false},
        38: {names: {ja: "大防御", en: "Defending Champion"}, ally: true, damage: false},
        39: {names: {ja: "ためる(敵)", en: "Psyche Up (enemy)"}, ally: false, damage: false},
        40: {names: {ja: "起きた", en: "Cure Sleeping"}, ally: true, damage: false},
        41: {names: {ja: "瞑想", en: "Meditation"}, ally: false, damage: false},
        42: {names: {ja: "マダンテ", en: "Magic Burst"}, ally: false, damage: true},
        43: {names: {ja: "祈り", en: "Restore MP"}, ally: false, damage: false},
        44: {names: {ja: "しっぷう突き", en: "Mercurial Thrust"}, ally: true, damage: true},
        46: {names: {ja: "ターンスキップ", en: "Turn Skipped"}, ally: true, damage: false},
        47: {names: {ja: "賢者聖水", en: "Sage's Elixir"}, ally: true, damage: false},
        48: {names: {ja: "エルフののみぐすり", en: "Elfin Elixir"}, ally: true, damage: false},
        49: {names: {ja: "まほうのせいすい", en: "Magic Water"}, ally: true, damage: false},
        50: {names: {ja: "特薬草", en: "Special Medicine"}, ally: true, damage: false},
        51: {names: {ja: "しんでしまった！", en: "Dead"}, ally: true, damage: false},
        52: {names: {ja: "ゴスペルソング", en: "Gospel Song"}, ally: true, damage: false},
        53: {names: {ja: "逃げる", en: "Flee"}, ally: true, damage: false},
        62: {names: {ja: "ためる(味方)", en: "Psyche Up (ally)"}, ally: true, damage: false}
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
        numberTemplates: [],
        assetPack: null,
        lastMatches: Object.create(null),
        turnIndex: 1,
        actionIndex: 0,
        preAction: -1,
        lastDetectionAt: 0,
        resetLatched: false,
        becameActive: false,
        movementTimer: null,
        pendingDamage1: -1,
        pendingDamage1Enabled: false,
        pendingDamage2: -1,
        pendingDamage2Enabled: false,
        lastDamage1: -1,
        lastDamage2: -1,
        maybeCritical: -1,
        // 以下追加
        actionTaken: false,   // C#のActionTaken相当
        sleeping: false,      // C#のSleeping相当
        slept: false,         // C#のslept相当
        daibougilyo: false,   // C#のdaibougilyo相当
        matcherKind: "",
        gpuRecoveryInProgress: false,
        gpuWarningResolver: null,
        captureRect: {
            sourceWidth: BASE_WIDTH,
            sourceHeight: BASE_HEIGHT,
            sourceX: 0,
            sourceY: 0,
            sourceCropWidth: BASE_WIDTH,
            sourceCropHeight: BASE_HEIGHT
        }
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
        constructor(options = {}) {
            this.device = null;
            this.queue = null;
            this.pipeline = null;
            this.sampler = null;
            this.frameTexture = null;
            this.frameSize = {width: BASE_WIDTH, height: BASE_HEIGHT};
            this.onLost = options.onLost || null;
            this.deviceLostPromise = null;
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
            this.deviceLostPromise = device.lost
                .then((info) => {
                    if (this.onLost) {
                        Promise.resolve(this.onLost(info)).catch(() => {
                        });
                    }
                    return info;
                })
                .catch(() => {
                });
            this.sampler = device.createSampler({
                magFilter: "nearest",
                minFilter: "nearest"
            });
            this.frameTexture = device.createTexture({
                size: [BASE_WIDTH, BASE_HEIGHT, 1],
                format: "rgba8unorm",
                usage: GPUTextureUsage.COPY_DST | GPUTextureUsage.TEXTURE_BINDING | GPUTextureUsage.RENDER_ATTACHMENT
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
  penaltyWeight: f32,
  whiteWeight: f32,
  contrast: f32,
  bias: f32,
  alphaThreshold: f32,
};

@group(0) @binding(0) var frameTex: texture_2d<f32>;
@group(0) @binding(1) var templateMaskTex: texture_2d<f32>;
@group(0) @binding(2) var<storage, read_write> scores: array<f32>;
@group(0) @binding(3) var<uniform> params: Params;

fn luminance(rgb: vec3<f32>) -> f32 {
  return dot(rgb, vec3<f32>(0.2126, 0.7152, 0.0722));
}

fn preprocess(sample: vec4<f32>) -> f32 {
  let boosted = clamp((luminance(sample.rgb) - 0.5) * params.contrast + 0.5 + params.bias, 0.0, 1.0);
  return select(0.0, boosted, sample.a >= params.alphaThreshold);
}

@compute @workgroup_size(8, 8, 1)
fn main(@builtin(global_invocation_id) gid: vec3<u32>) {
  if (gid.x >= params.scoreWidth || gid.y >= params.scoreHeight) {
    return;
  }

  var overlap = 0.0;
  var templateWhiteCount = 0.0;
  var frameWhiteCount = 0.0;

  for (var y: u32 = 0u; y < params.templateHeight; y = y + 1u) {
    for (var x: u32 = 0u; x < params.templateWidth; x = x + 1u) {
      let framePos = vec2<i32>(i32(params.roiX + gid.x + x), i32(params.roiY + gid.y + y));
      let templatePos = vec2<i32>(i32(x), i32(y));
      let frameL = preprocess(textureLoad(frameTex, framePos, 0));
      let frameWhitePixel = select(0.0, 1.0, frameL >= params.threshold);
      let templateWhitePixel = select(0.0, 1.0, textureLoad(templateMaskTex, templatePos, 0).r > 0.0);
      overlap = overlap + min(frameWhitePixel, templateWhitePixel);
      templateWhiteCount = templateWhiteCount + templateWhitePixel;
      frameWhiteCount = frameWhiteCount + frameWhitePixel;
    }
  }

  let unionWhite = max(templateWhiteCount + frameWhiteCount - overlap, 1.0);
  let penalty = max(frameWhiteCount - overlap, 0.0);
  let index = gid.y * params.scoreWidth + gid.x;
  scores[index] = max(0.0, (overlap * params.whiteWeight - penalty * params.penaltyWeight) / unionWhite);
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
                format: template.maskBytes ? "r8unorm" : "rgba8unorm",
                usage: GPUTextureUsage.COPY_DST | GPUTextureUsage.TEXTURE_BINDING
            });
            if (template.maskBytes) {
                const packed = packTextureBytes(template.maskBytes, template.width, template.height, 1);
                this.queue.writeTexture(
                    {texture},
                    packed.data,
                    {
                        offset: 0,
                        bytesPerRow: packed.bytesPerRow,
                        rowsPerImage: template.height
                    },
                    [template.width, template.height, 1]
                );
            } else {
                this.queue.copyExternalImageToTexture(
                    {source: template.bitmap},
                    {texture},
                    [template.width, template.height]
                );
            }
            return {
                ...template,
                texture
            };
        }

        uploadFrame(source) {
            this.queue.copyExternalImageToTexture(
                {source},
                {texture: this.frameTexture},
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
            const paramsBuffer = new ArrayBuffer(56);
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
            paramsView.setFloat32(36, MATCH_PENALTY_WEIGHT, true);
            paramsView.setFloat32(40, MATCH_WHITE_WEIGHT, true);
            paramsView.setFloat32(44, MATCH_CONTRAST, true);
            paramsView.setFloat32(48, MATCH_BIAS, true);
            paramsView.setFloat32(52, TEMPLATE_ALPHA_THRESHOLD, true);
            const uniformBuffer = this.device.createBuffer({
                size: paramsBuffer.byteLength,
                usage: GPUBufferUsage.UNIFORM | GPUBufferUsage.COPY_DST
            });
            this.queue.writeBuffer(uniformBuffer, 0, paramsBuffer);
            const bindGroup = this.device.createBindGroup({
                layout: this.pipeline.getBindGroupLayout(0),
                entries: [
                    {binding: 0, resource: this.frameTexture.createView()},
                    {binding: 1, resource: template.texture.createView()},
                    {binding: 2, resource: {buffer: scoreBuffer}},
                    {binding: 3, resource: {buffer: uniformBuffer}}
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
            this.frameContext = this.frameCanvas.getContext("2d", {willReadFrequently: true});
            this.frameContext.imageSmoothingEnabled = false;
        }

        async init() {
        }

        async createTemplate(template) {
            if (template.maskBytes) {
                return {
                    ...template,
                    mask: template.maskBytes
                };
            }
            const canvas = document.createElement("canvas");
            canvas.width = template.width;
            canvas.height = template.height;
            const context = canvas.getContext("2d", {willReadFrequently: true});
            context.imageSmoothingEnabled = false;
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
            mask[index] = isWhitePixel(
                data[offset],
                data[offset + 1],
                data[offset + 2],
                data[offset + 3],
                WHITE_THRESHOLD,
                TEMPLATE_ALPHA_THRESHOLD
            ) ? 1 : 0;
        }
        return mask;
    }

    function preprocessLuminance(r, g, b) {
        const luminance = (r * 0.2126 + g * 0.7152 + b * 0.0722) / 255;
        return Math.max(0, Math.min(1, (luminance - 0.5) * MATCH_CONTRAST + 0.5 + MATCH_BIAS));
    }

    function isWhitePixel(r, g, b, a, threshold, alphaThreshold) {
        if (a / 255 < alphaThreshold) {
            return false;
        }
        return preprocessLuminance(r, g, b) >= threshold;
    }

    function compareMask(frame, templateMask, x, y, width, height) {
        const data = frame.data;
        let overlap = 0;
        let templateWhiteCount = 0;
        let frameWhiteCount = 0;
        for (let row = 0; row < height; row += 1) {
            for (let col = 0; col < width; col += 1) {
                const frameIndex = ((y + row) * frame.width + (x + col)) * 4;
                const frameWhite = isWhitePixel(
                    data[frameIndex],
                    data[frameIndex + 1],
                    data[frameIndex + 2],
                    data[frameIndex + 3],
                    WHITE_THRESHOLD,
                    0
                ) ? 1 : 0;
                const templateWhitePixel = templateMask[row * width + col];
                if (templateWhitePixel) {
                    templateWhiteCount += 1;
                }
                if (frameWhite) {
                    frameWhiteCount += 1;
                }
                if (frameWhite && templateWhitePixel) {
                    overlap += 1;
                }
            }
        }
        const union = templateWhiteCount + frameWhiteCount - overlap;
        if (!union) {
            return 0;
        }
        const penalty = Math.max(0, frameWhiteCount - overlap);
        return Math.max(0, (overlap * MATCH_WHITE_WEIGHT - penalty * MATCH_PENALTY_WEIGHT) / union);
    }

    function computeSourceRect(source) {
        const sourceWidth = source.videoWidth || source.naturalWidth || source.width || BASE_WIDTH;
        const sourceHeight = source.videoHeight || source.naturalHeight || source.height || BASE_HEIGHT;
        return {
            sourceWidth,
            sourceHeight,
            sourceX: 0,
            sourceY: 0,
            sourceCropWidth: BASE_WIDTH,
            sourceCropHeight: BASE_HEIGHT
        };
    }

    function packTextureBytes(bytes, width, height, bytesPerPixel) {
        const unalignedBytesPerRow = width * bytesPerPixel;
        const bytesPerRow = Math.ceil(unalignedBytesPerRow / 256) * 256;
        if (bytesPerRow === unalignedBytesPerRow) {
            return {data: bytes, bytesPerRow};
        }
        const packed = new Uint8Array(bytesPerRow * height);
        for (let row = 0; row < height; row += 1) {
            const srcOffset = row * unalignedBytesPerRow;
            const dstOffset = row * bytesPerRow;
            packed.set(bytes.subarray(srcOffset, srcOffset + unalignedBytesPerRow), dstOffset);
        }
        return {data: packed, bytesPerRow};
    }

    function drawProcessingFrame(source) {
        const rect = computeSourceRect(source);
        state.captureRect = rect;
        processingContext.clearRect(0, 0, BASE_WIDTH, BASE_HEIGHT);
        processingContext.drawImage(
            source,
            rect.sourceX,
            rect.sourceY,
            rect.sourceCropWidth,
            rect.sourceCropHeight,
            0,
            0,
            BASE_WIDTH,
            BASE_HEIGHT
        );
    }

    function buildWhiteMask(imageData) {
        const mask = new Uint8Array(imageData.width * imageData.height);
        const data = imageData.data;
        for (let index = 0; index < mask.length; index += 1) {
            const offset = index * 4;
            const luminance =
                (data[offset] * 0.2126 + data[offset + 1] * 0.7152 + data[offset + 2] * 0.0722) / 255;
            mask[index] = luminance >= 140 / 255 ? 1 : 0;
        }
        return {
            width: imageData.width,
            height: imageData.height,
            mask
        };
    }

    function cropMask(binary, area) {
        const width = Math.min(area.width, binary.width - area.x);
        const height = Math.min(area.height, binary.height - area.y);
        const mask = new Uint8Array(width * height);
        for (let row = 0; row < height; row += 1) {
            const srcOffset = (area.y + row) * binary.width + area.x;
            const dstOffset = row * width;
            mask.set(binary.mask.subarray(srcOffset, srcOffset + width), dstOffset);
        }
        return {width, height, mask};
    }

    function trimFirstPixel(binary, targetWidth, targetHeight) {
        let foundX = -1;
        let foundY = -1;

        // フェーズ1: 行方向に走査
        for (let row = 0; row < binary.height; row += 1) {
            for (let col = 0; col < binary.width - 1; col += 1) {
                const index = row * binary.width + col;
                if (binary.mask[index] && binary.mask[index + 1]) {
                    foundX = col;
                    foundY = row;
                    break;
                }
            }
            if (foundX !== -1) break;
        }

        if (foundX === -1) return binary;

        // フェーズ2: foundXだけ絞り込む（foundYは変更しない）
        for (let col = 0; col < foundX; col += 1) {
            for (let row = 0; row < binary.height; row += 1) {
                const index = row * binary.width + col;
                if (binary.mask[index] && binary.mask[index + 1]) {
                    foundX = Math.min(foundX, col);
                    // ← foundY の更新を削除
                    break;
                }
            }
        }

        const width = Math.min(targetWidth, binary.width - foundX);
        const height = Math.min(targetHeight, binary.height - foundY);
        const mask = new Uint8Array(targetWidth * targetHeight);
        for (let row = 0; row < height; row += 1) {
            const srcOffset = (foundY + row) * binary.width + foundX;
            const dstOffset = row * targetWidth;
            mask.set(binary.mask.subarray(srcOffset, srcOffset + width), dstOffset);
        }
        return {width: targetWidth, height: targetHeight, mask};
    }

    function compareBinaryImages(frameMask, templateMask) {
        if (frameMask.width !== templateMask.width || frameMask.height !== templateMask.height) {
            return 0;
        }
        let overlap = 0;
        let frameWhite = 0;
        let templateWhite = 0;
        for (let index = 0; index < frameMask.mask.length; index += 1) {
            const framePixel = frameMask.mask[index];
            const templatePixel = templateMask.mask[index];
            if (framePixel) {
                frameWhite += 1;
            }
            if (templatePixel) {
                templateWhite += 1;
            }
            if (framePixel && templatePixel) {
                overlap += 1;
            }
        }
        const union = frameWhite + templateWhite - overlap;
        if (!union) {
            return 0;
        }
        return overlap / union;
    }

    function normalizeDigitFileName(file) {
        return Number.parseInt(file.split("_")[0], 10);
    }

    function convertMatchResults(digits) {
        if (!digits.length || digits[0] === -1) {
            return -1;
        }
        if (digits[1] === -1 && digits[2] !== -1) {
            return -1;
        }
        let value = 0;
        let found = false;
        digits.forEach((digit) => {
            if (digit !== -1) {
                value = value * 10 + digit;
                found = true;
            }
        });
        return found ? value : -1;
    }

    function recognizeDamageValue(key) {
        const config = DAMAGE_ROIS[key];
        const cropped = processingContext.getImageData(config.x, config.y, config.width, config.height);
        const binary = buildWhiteMask(cropped);
        const digits = config.actionAreas.map((area) => {
            const trimmed = trimFirstPixel(cropMask(binary, area), 26, 40);
            // サイズチェックを撤廃（trimFirstPixelが常にtargetWidth×targetHeightを返すため）
            let bestDigit = -1;
            let bestScore = 0;
            state.numberTemplates.forEach((template) => {
                const score = compareBinaryImages(trimmed, template.mask);
                if (score >= NUMBER_THRESHOLD && score >= bestScore) {
                    bestDigit = template.digit;
                    bestScore = score;
                }
            });
            return {digit: bestDigit, score: bestScore};
        });
// recognizeDamageValue の return を変更
        return {
            digits: digits.map((item) => item.digit),
            scores: digits.map((item) => item.score),   // ← 追加
            score: digits.reduce((max, item) => Math.max(max, item.score), 0),
            value: convertMatchResults(digits.map((item) => item.digit))
        };
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

    function drawOverlay(matches, damageReadings) {
        overlayContext.clearRect(0, 0, BASE_WIDTH, BASE_HEIGHT);
        overlayContext.drawImage(processingCanvas, 0, 0, BASE_WIDTH, BASE_HEIGHT);
        overlayContext.lineWidth = 3;
        overlayContext.font = "18px Bahnschrift, sans-serif";



        // --- 追加: 数字認識枠 ---
        if (damageReadings) {
            overlayContext.font = "13px Bahnschrift, sans-serif";
            for (const [key, reading] of Object.entries(damageReadings)) {
                const config = DAMAGE_ROIS[key];
                if (!config) continue;

                // ROI全体枠（薄い水色）
                overlayContext.strokeStyle = "rgba(100, 210, 255, 0.55)";
                overlayContext.lineWidth = 2;
                overlayContext.strokeRect(config.x, config.y, config.width, config.height);

                // 認識値ラベル（ROI上部）
                const valueLabel = reading.value !== -1 ? `${key}: ${reading.value}` : `${key}: --`;
                const valueLabelWidth = overlayContext.measureText(valueLabel).width;
                const valueLabelY = config.y >= 20 ? config.y - 4 : config.y + config.height + 16;
                overlayContext.fillStyle = "rgba(10, 30, 50, 0.75)";
                overlayContext.fillRect(config.x, valueLabelY - 14, valueLabelWidth + 12, 18);
                overlayContext.fillStyle = reading.value !== -1
                    ? "rgba(100, 230, 255, 0.96)"
                    : "rgba(180, 180, 180, 0.7)";
                overlayContext.fillText(valueLabel, config.x + 6, valueLabelY);

                // 桁ごとのactionArea枠
                // 桁ごとのactionArea枠
                config.actionAreas.forEach((area, index) => {
                    const digit = reading.digits[index];
                    const score = reading.scores ? reading.scores[index] : null;
                    const absX = config.x + area.x;
                    const absY = config.y + area.y;

                    if (digit !== -1) {
                        overlayContext.strokeStyle = "rgba(80, 255, 120, 0.85)";
                        overlayContext.lineWidth = 1.5;
                        overlayContext.strokeRect(absX, absY, area.width, area.height);

                        const digitLabel = score !== null
                            ? `${digit} (${(score * 100).toFixed(0)}%)`
                            : `${digit}`;

// 偶数=枠の下側内側、奇数=枠の上側内側に交互配置
                        const labelAbove = index % 2 === 1;
                        const labelY = labelAbove
                            ? absY + 11                  // 枠の上側内側（上から11px）
                            : absY + area.height - 3;   // 枠の下側内側（下端から3px）

                        const labelWidth = overlayContext.measureText(digitLabel).width;
                        overlayContext.fillStyle = "rgba(10, 30, 10, 0.72)";
                        overlayContext.fillRect(
                            absX + 1,
                            labelAbove ? absY + 1 : absY + area.height - 14,
                            labelWidth + 6,
                            13
                        );

                        overlayContext.fillStyle = "rgba(80, 255, 120, 0.95)";
                        overlayContext.fillText(digitLabel, absX + 3, labelY);
                    }
                });
            }
        }

        // --- 既存: テンプレートマッチング枠 ---
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
                const labelY = match.y >= 26 ? match.y - 10 : match.y + match.height + 20;
                overlayContext.fillRect(match.x, labelY - 18, textWidth + 16, 24);
                overlayContext.fillStyle = "rgba(255, 248, 236, 0.96)";
                overlayContext.fillText(label, match.x + 8, labelY);
            }
        }
    }

    function createMonochromeCropCanvas(match) {
        const canvas = document.createElement("canvas");
        canvas.width = match.width;
        canvas.height = match.height;
        const context = canvas.getContext("2d", {willReadFrequently: true});
        context.imageSmoothingEnabled = false;
        context.drawImage(
            processingCanvas,
            match.x,
            match.y,
            match.width,
            match.height,
            0,
            0,
            match.width,
            match.height
        );
        const imageData = context.getImageData(0, 0, match.width, match.height);
        const data = imageData.data;
        for (let index = 0; index < data.length; index += 4) {
            const monochrome = Math.round(
                data[index] * 0.299 + data[index + 1] * 0.587 + data[index + 2] * 0.114
            );
            data[index] = monochrome;
            data[index + 1] = monochrome;
            data[index + 2] = monochrome;
        }
        context.putImageData(imageData, 0, 0);
        return canvas;
    }

    function downloadCanvas(canvas, fileName) {
        const link = document.createElement("a");
        link.download = fileName;
        link.href = canvas.toDataURL("image/png");
        link.rel = "noopener";
        document.body.appendChild(link);
        link.click();
        link.remove();
    }

    function downloadRecognizedMatchCrops() {
        const matches = state.lastMatches || {};
        let downloaded = 0;
        for (const slot of MATCH_SLOT_KEYS) {
            const match = matches[slot];
            if (!match || !match.file || match.score < TEMPLATE_THRESHOLD) {
                continue;
            }
            const canvas = createMonochromeCropCanvas(match);
            const safeFile = match.file.replace(/[^a-zA-Z0-9._-]/g, "_");
            downloadCanvas(canvas, `${slot}-${safeFile}-mono.png`);
            downloaded += 1;
        }
        return downloaded;
    }

    function emptyTurnRow(turn) {
        return {
            turn,
            slots: [
                {actionId: null, detail: "", score: null, damage: null},
                {actionId: null, detail: "", score: null, damage: null},
                {actionId: null, detail: "", score: null, damage: null}
            ]
        };
    }

    function getTurnRows() {
        const rows = [];
        state.history.forEach((entry) => {
            const index = Math.max(0, entry.turn - 1);
            if (!rows[index]) {
                rows[index] = emptyTurnRow(entry.turn);
            }
            rows[index].slots[entry.slot - 1] = {
                actionId: entry.actionId,
                detail: entry.detail,
                score: entry.score,
                damage: entry.damage
            };
        });
        return rows.filter(Boolean);
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

        // reset
        if (erugioMain && sub.file === "reset.png" && main.score >= 0.6 && sub.score >= 0.6) {
            return {kind: "reset", score: Math.min(main.score, sub.score), detail: `${main.file} + reset.png`};
        }

        // 攻撃(敵) = erugio + attack
        if (erugioMain && sub.file === "attack.png" && main.score >= TEMPLATE_THRESHOLD && sub.score >= TEMPLATE_THRESHOLD) {
            return {
                kind: "action",
                actionId: 1,
                detail: `${main.file} + attack.png`,
                score: Math.min(main.score, sub.score)
            };
        }

        // 超高速連打 = erugio + uhsc
        if (erugioMain && sub.file === "uhsc.png" && main.score >= TEMPLATE_THRESHOLD && sub.score >= TEMPLATE_THRESHOLD) {
            return {
                kind: "action",
                actionId: 2,
                detail: `${main.file} + uhsc.png`,
                score: Math.min(main.score, sub.score)
            };
        }

        // 攻撃(味方) = a_attack.png、ActionTaken未設定かつguardなし
        if (
            !state.actionTaken &&
            ally.file === "a_attack.png" &&
            ally.score >= TEMPLATE_THRESHOLD &&
            sub.file !== "uhsc.png" &&
            main.file !== "guard.png"
        ) {
            return {kind: "action", actionId: 25, detail: `a_attack.png (${main.file || "-"})`, score: ally.score};
        }

        // 大防御 combo
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

        // daibougilyo: マダンテ後のsleeping2は大防御
        if (state.daibougilyo && main.file === "sleeping2.png" && main.score >= TEMPLATE_THRESHOLD) {
            return {kind: "action", actionId: 38, detail: "madannte -> sleeping2 (daibougilyo)", score: main.score};
        }

        // 麻痺回復 = Paralysis + CareParalysis
        if (
            !state.slept &&
            main.file === "Paralysis.png" &&
            ally.file === "CareParalysis.png" &&
            ally.score >= TEMPLATE_THRESHOLD
        ) {
            return {
                kind: "action",
                actionId: 28,
                detail: "Paralysis + CareParalysis",
                score: Math.min(main.score, ally.score)
            };
        }

        // 麻痺で動けない = Paralysis単体（Paralysis2なし）
        if (
            !state.slept &&
            main.file === "Paralysis.png" &&
            main.score >= TEMPLATE_THRESHOLD &&
            (!sub.file || sub.file !== "Paralysis2.png" || sub.score < TEMPLATE_THRESHOLD)
        ) {
            return {kind: "action", actionId: 24, detail: "Paralysis", score: main.score};
        }

        // しんでしまった = sleeping2 + dead/dead2
        if (
            !state.actionTaken &&
            main.file === "sleeping2.png" &&
            (ally.file === "dead.png" || ally.file === "dead2.png") &&
            ally.score >= TEMPLATE_THRESHOLD
        ) {
            return {
                kind: "action",
                actionId: 51,
                detail: `sleeping2 + ${ally.file}`,
                score: Math.min(main.score, ally.score)
            };
        }

        // 眠っている！ = sleeping2単体（slept未設定）
        if (
            !state.slept &&
            main.file === "sleeping2.png" &&
            main.score >= TEMPLATE_THRESHOLD &&
            (!ally.file || (ally.file !== "dead.png" && ally.file !== "dead2.png") || ally.score < TEMPLATE_THRESHOLD)
        ) {
            return {kind: "action", actionId: 35, detail: "sleeping2", score: main.score};
        }

        // WakeUp系: Sleeping中かつslept未設定
        if (
            state.sleeping &&
            !state.slept &&
            (main.file === "WakeUp.png" || main.file === "WakeUp2.png" || main.file === "WakeUp3.png") &&
            sub.file !== "inori.png" &&
            main.score >= TEMPLATE_THRESHOLD
        ) {
            // ActionIndex != 0 かつ ActionTaken未設定 → ターンスキップ
            // ActionIndex == 0 かつ ActionTaken未設定 → 眠り回復
            if (state.actionIndex !== 0 && !state.actionTaken) {
                return {kind: "action", actionId: 46, detail: main.file, score: main.score};
            }
            if (state.actionIndex === 0 && !state.actionTaken) {
                return {kind: "action", actionId: 40, detail: main.file, score: main.score};
            }
        }

        // ためる
        if (main.file === "tameru.png" && main.score >= TEMPLATE_THRESHOLD) {
            if (target.file === "aha.png" && target.score >= TEMPLATE_THRESHOLD && !state.actionTaken) {
                return {
                    kind: "action",
                    actionId: 62,
                    detail: "tameru + aha",
                    score: Math.min(main.score, target.score)
                };
            }
            return {kind: "action", actionId: 39, detail: "tameru", score: main.score};
        }

        if (main.file === "sukara.png" && main.score >= TEMPLATE_THRESHOLD) {
            return {kind: "action", actionId: 30, detail: "tameru", score: main.score};
        }

        // ano.png: action記録なし、状態リセットのみ
        if (main.file === "ano.png" && main.score >= TEMPLATE_THRESHOLD) {
            return {kind: "ano"};
        }

        // DIRECT_MAIN_RULES
        const directAction = DIRECT_MAIN_RULES.get(main.file);
        if (directAction && main.score >= TEMPLATE_THRESHOLD) {
            return {kind: "action", actionId: directAction, detail: main.file, score: main.score};
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
        state.pendingDamage1 = -1;
        state.pendingDamage1Enabled = false;
        state.pendingDamage2 = -1;
        state.pendingDamage2Enabled = false;
        state.lastDamage1 = -1;
        state.lastDamage2 = -1;
        state.maybeCritical = -1;
        // 追加
        state.actionTaken = false;
        state.sleeping = false;
        state.slept = false;
        state.daibougilyo = false;
        renderHistory();
        updateTurnChip();
        setBridgeStatus("visionBridgeReady", "");
    }

    function renderHistory() {
        ui.historyBody.innerHTML = "";
        const rows = getTurnRows();
        if (!rows.length) {
            ui.historyEmpty.hidden = false;
            ui.historyScroll.hidden = true;
            return;
        }
        ui.historyEmpty.hidden = true;
        ui.historyScroll.hidden = false;
        rows.forEach((rowData) => {
            const row = document.createElement("tr");
            const turnCell = document.createElement("td");
            turnCell.textContent = `T${rowData.turn}`;
            row.appendChild(turnCell);
            rowData.slots.forEach((slot) => {
                const actionCell = document.createElement("td");
                actionCell.textContent = slot.actionId ? getActionLabel(slot.actionId) : "-";
                actionCell.title = slot.detail || "";
                const damageCell = document.createElement("td");
                damageCell.textContent =
                    typeof slot.damage === "number" && slot.damage >= 0 ? String(slot.damage) : slot.actionId ? "..." : "-";
                row.append(actionCell, damageCell);
            });
            ui.historyBody.appendChild(row);
        });
    }

    function buildConsoleCommand() {
        const enemyActions = [];
        const allyActions = [];
        const damages = [];
        state.history.forEach((entry) => {
            const action = ACTIONS[entry.actionId];
            if (!action || entry.damage === -1) {
                return;
            }
            if (action.ally) {
                allyActions.push(entry.actionId);
            } else {
                enemyActions.push(entry.actionId);
            }
            if (action.damage) {
                damages.push(entry.damage);
            }
        });
        return `b 0 0 0 ${Math.max(0, state.turnIndex - 1)} ${enemyActions.join(" ")}-${allyActions.join(" ")}-${damages.join(" ")}-`;
    }

    function getDamageChannel(actionId) {
        if (actionId === 2 || actionId === 34) {
            return 2;
        }
        if ([1, 5, 8, 9, 10, 18, 25, 42, 44].includes(actionId)) {
            return 1;
        }
        return 0;
    }

    function updateHistoryDamage(turn, slotIndex, damage) {
        const entry = state.history.find((item) => item.turn === turn && item.slot === slotIndex + 1);
        if (!entry) {
            return;
        }
        entry.damage = damage;
        renderHistory();
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
                detail: item.detail,
                damage: item.damage
            }))
        });
        setBridgeStatus(sync.key, sync.encoded);
    }

    function resolvePendingDamage(encodedIndex, damage) {
        if (encodedIndex < 0) {
            return;
        }
        const turn = encodedIndex & 0xfff;
        const slotIndex = (encodedIndex >> 12) & 0xf;
        updateHistoryDamage(turn + 1, slotIndex, damage);
    }

    function handlePendingDamages(matches, damageReadings) {
        const main = matches.main || emptyMatch("main");
        const damage1 = damageReadings.damage1.value;
        const damage2 = damageReadings.damage2.value;
        const candidateDamage1 = Math.max(damage1, damage2);
        const candidateDamage2 = Math.max(damage2, damage1);

        // maybeCritical: critical.png検出時に攻撃(敵)→痛恨(6)に上書き
        if (state.maybeCritical !== -1) {
            if (main.file === "critical.png" && main.score >= TEMPLATE_THRESHOLD) {
                const turn = (state.maybeCritical & 0xfff) + 1;
                const slotIndex = (state.maybeCritical >> 12) & 0xf;
                updateHistoryDamage(turn, slotIndex, -2); // -2は「action上書き」シグナル用
                // 実際はダメージではなくactionIdを上書きする必要があるため別途処理
                const entry = state.history.find((item) => item.turn === turn && item.slot === slotIndex + 1);
                if (entry) {
                    entry.actionId = 6; // CRITICAL_ATTACK
                    renderHistory();
                }
                state.maybeCritical = -1;
            }
        }

        if ((state.pendingDamage1 !== -1 && state.pendingDamage1Enabled) || state.lastDamage1 < candidateDamage1) {
            if (["guard.png", "miss.png", "miss2.png", "mikawasi.png"].includes(main.file)) {
                state.pendingDamage1Enabled = false;
                state.maybeCritical = -1;
                resolvePendingDamage(state.pendingDamage1, 0);
                state.preAction = -1;
                return true;
            }
            if (candidateDamage1 !== -1) {
                state.lastDamage1 = candidateDamage1;
                state.pendingDamage1Enabled = false;
                resolvePendingDamage(state.pendingDamage1, candidateDamage1);
                state.preAction = -1;
                return true;
            }
        } else if ((state.pendingDamage2 !== -1 && state.pendingDamage2Enabled) || state.lastDamage2 < candidateDamage2) {
            if (["guard.png", "miss.png", "miss2.png", "mikawasi.png"].includes(main.file)) {
                state.pendingDamage2Enabled = false;
                state.maybeCritical = -1;
                state.lastDamage2 = -1;
                resolvePendingDamage(state.pendingDamage2, 0);
                state.preAction = -1;
                return true;
            }
            if (candidateDamage2 !== -1) {
                state.pendingDamage2Enabled = false;
                state.lastDamage2 = candidateDamage2;
                resolvePendingDamage(state.pendingDamage2, candidateDamage2);
                state.preAction = -1;
                return true;
            }
        }
        return false;
    }

    function acceptCandidate(candidate) {
        // ano.png: preActionリセット、actionTaken=true、slept=false のみ
        if (candidate && candidate.kind === "ano") {
            state.preAction = -1;
            state.actionTaken = true;
            state.slept = false;
            return;
        }

        if (!candidate || candidate.kind !== "action") {
            if (Date.now() - state.lastDetectionAt > 5000) {
                state.preAction = -1;
            }
            // C#: else節でActionIndex==0ならActionTakenリセット
            if (state.actionIndex === 0) {
                state.actionTaken = false;
            }
            return;
        }

        if (candidate.actionId === state.preAction) {
            state.lastDetectionAt = Date.now();
            // C#: action==preActionのとき ActionIndex==0ならActionTakenリセット
            if (state.actionIndex === 0) {
                state.actionTaken = false;
            }
            return;
        }

        state.preAction = candidate.actionId;
        state.lastDetectionAt = Date.now();

        const pendingSlotRef = (state.actionIndex << 12) | (state.turnIndex - 1);
        const damageChannel = getDamageChannel(candidate.actionId);
        state.pendingDamage1 = damageChannel === 1 ? pendingSlotRef : -1;
        state.pendingDamage2 = damageChannel === 2 ? pendingSlotRef : -1;
        state.pendingDamage1Enabled = damageChannel === 1;
        state.pendingDamage2Enabled = damageChannel === 2;
        state.lastDamage1 = -1;
        state.lastDamage2 = -1;

        // maybeCritical: 攻撃(敵)のとき記録
        if (candidate.actionId === 1) {
            state.maybeCritical = pendingSlotRef;
        } else if (!candidate.detail?.includes("attack.png")) {
            state.maybeCritical = -1;
        }

        // daibougilyo: マダンテかつActionTaken未設定のとき設定
        if (candidate.actionId === 42 && !state.actionTaken) {
            state.daibougilyo = true;
        }
        // 大防御確定でdaibougilyoリセット
        if (candidate.actionId === 38) {
            state.daibougilyo = false;
        }

        // Sleeping状態の設定
        if (candidate.actionId === 12) { // あやしいひとみ
            state.sleeping = true;
        }
        if (candidate.actionId === 35) { // 眠っている！
            state.sleeping = true;
            state.slept = true;
        }
        if (candidate.actionId === 40 || candidate.actionId === 46) { // 眠り回復/ターンスキップ
            state.sleeping = false;
            state.slept = true;
        }

        // 麻痺系slept
        if (candidate.actionId === 24 || candidate.actionId === 28) {
            state.slept = true;
        }

        // ActionTaken設定が必要なaction
        const setsActionTaken = [
            30, 31, 33, 34, 37, 40, 46, 47, 48, 49, 50, 52, 53, // buff/mirror/doubleup/multithrust/fullheal/cure_sleeping/turn_skipped/elixirs/song/flee
            24, 28, 25, 35, 51                                    // paralysis/cure_paralysis/attack_ally/sleeping/dead
        ];
        if (setsActionTaken.includes(candidate.actionId)) {
            state.actionTaken = true;
        }

        const entry = {
            turn: state.turnIndex,
            slot: state.actionIndex + 1,
            actionId: candidate.actionId,
            detail: candidate.detail,
            score: candidate.score,
            damage: damageChannel ? -1 : 0
        };
        state.history.push(entry);

        if (state.actionIndex === 2) {
            state.actionIndex = 0;
            state.turnIndex += 1;
            state.actionTaken = false;
            state.daibougilyo = false;
            state.slept = false;
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
                detail: item.detail,
                damage: item.damage
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

    function decodeBase64Bytes(value) {
        // URL-safe Base64 を標準形式へ変換
        let normalized = value
            .replace(/-/g, "+")
            .replace(/_/g, "/")
            .replace(/\s+/g, "");

        // パディング補完
        while (normalized.length % 4 !== 0) {
            normalized += "=";
        }

        const binary = atob(normalized);
        const bytes = new Uint8Array(binary.length);

        for (let index = 0; index < binary.length; index++) {
            bytes[index] = binary.charCodeAt(index);
        }

        return bytes;
    }

    async function loadPackedVisionAssets() {
        const response = await fetch(VISION_ASSET_PACK_URL, {cache: "no-store"});
        if (!response.ok) {
            throw new Error(`asset pack missing: ${VISION_ASSET_PACK_URL}`);
        }
        return response.json();
    }

    function normalizePackedTemplate(entry) {
        return {
            slot: entry.slot,
            file: entry.file,
            width: entry.width,
            height: entry.height,
            maskBytes: decodeBase64Bytes(entry.mask)
        };
    }

    async function loadTemplates(matcher, assetPack) {
        const templatesBySlot = new Map();
        for (const entry of assetPack.templates || []) {
            const template = await matcher.createTemplate(normalizePackedTemplate(entry));
            if (!templatesBySlot.has(template.slot)) {
                templatesBySlot.set(template.slot, []);
            }
            templatesBySlot.get(template.slot).push(template);
        }
        return templatesBySlot;
    }

    function loadNumberTemplates(assetPack) {
        return (assetPack.numberTemplates || []).map((entry) => ({
            file: entry.file,
            digit: typeof entry.digit === "number" ? entry.digit : normalizeDigitFileName(entry.file),
            mask: {
                width: entry.width,
                height: entry.height,
                mask: decodeBase64Bytes(entry.mask)
            }
        }));
    }

    function openGpuWarningDialog() {
        return new Promise((resolve) => {
            if (!ui.gpuWarningDialog) {
                resolve(false);
                return;
            }
            state.gpuWarningResolver = resolve;
            ui.gpuWarningDialog.hidden = false;
        });
    }

    function closeGpuWarningDialog(accepted) {
        if (ui.gpuWarningDialog) {
            ui.gpuWarningDialog.hidden = true;
        }
        const resolver = state.gpuWarningResolver;
        state.gpuWarningResolver = null;
        if (resolver) {
            resolver(accepted);
        }
    }

    async function createMatcher(options = {}) {
        const {
            warnOnCpuFallback = true,
            onWebGpuLost = null
        } = options;
        if (navigator.gpu) {
            try {
                const matcher = new WebGpuTemplateMatcher({onLost: onWebGpuLost});
                await matcher.init();
                ui.engine.textContent = "WebGPU";
                state.matcherKind = "webgpu";
                setStatus("visionStatusReady");
                return matcher;
            } catch (error) {
                console.warn("WebGPU matcher unavailable, falling back to CPU:", error);
                if (warnOnCpuFallback) {
                    const proceed = await openGpuWarningDialog();
                    if (!proceed) {
                        const abortError = new Error("cpu fallback declined");
                        abortError.name = "AbortError";
                        throw abortError;
                    }
                }
            }
        }
        const matcher = new CpuTemplateMatcher();
        await matcher.init();
        ui.engine.textContent = "CPU";
        state.matcherKind = "cpu";
        setStatus("visionStatusFallback");
        return matcher;
    }

    async function recoverWebGpuMatcher() {
        if (state.gpuRecoveryInProgress || !state.stream || state.matcherKind !== "webgpu") {
            return;
        }
        state.gpuRecoveryInProgress = true;
        state.loopToken += 1;
        try {
            if (!state.assetPack) {
                state.assetPack = await loadPackedVisionAssets();
            }
            const matcher = await createMatcher({
                warnOnCpuFallback: false,
                onWebGpuLost: recoverWebGpuMatcher
            });
            state.matcher = matcher;
            state.templatesBySlot = await loadTemplates(matcher, state.assetPack);
            state.lastFrameAt = 0;
            state.lastFpsAt = 0;
            state.processedFrames = 0;
            if (state.stream) {
                setStatus(state.matcherKind === "webgpu" ? "visionStatusWatching" : "visionStatusFallback");
                startLoop();
            }
        } catch (error) {
            console.warn("WebGPU matcher recovery failed:", error);
            state.matcher = null;
            state.matcherKind = "";
            state.templatesBySlot = new Map();
            setStatus("visionStatusError");
        } finally {
            state.gpuRecoveryInProgress = false;
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
                video: {width: {ideal: BASE_WIDTH}, height: {ideal: BASE_HEIGHT}}
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
                    ? {deviceId: {exact: deviceId}, width: {ideal: 1920}, height: {ideal: 1080}}
                    : {width: {ideal: 1920}, height: {ideal: 1080}}
            };
            if (state.stream) {
                state.stream.getTracks().forEach((track) => track.stop());
            }
            const stream = await navigator.mediaDevices.getUserMedia(constraints);
            state.stream = stream;
            ui.video.srcObject = stream;
            await ui.video.play();
            await populateCameras();
            if (!state.assetPack) {
                state.assetPack = await loadPackedVisionAssets();
            }
            if (!state.matcher) {
                state.matcher = await createMatcher({
                    warnOnCpuFallback: true,
                    onWebGpuLost: recoverWebGpuMatcher
                });
                state.templatesBySlot = await loadTemplates(state.matcher, state.assetPack);
                state.numberTemplates = loadNumberTemplates(state.assetPack);
            }
            state.lastFrameAt = 0;
            state.lastFpsAt = 0;
            state.processedFrames = 0;
            setBridgeStatus("visionBridgeReady", ui.encodedPayload.value);
            setStatus("visionStatusWatching");
            startLoop();
        } catch (error) {
            if (state.stream) {
                state.stream.getTracks().forEach((track) => track.stop());
                state.stream = null;
            }
            ui.video.srcObject = null;
            if (error && error.name === "AbortError") {
                setStatus("visionStatusIdle");
                setBridgeStatus("visionBridgeIdle", "");
                return;
            }
            state.matcher = null;
            state.matcherKind = "";
            state.templatesBySlot = new Map();
            ui.engine.textContent = "";
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
                try {
                    drawProcessingFrame(ui.video);
                    const matches = await state.matcher.match(processingCanvas, state.templatesBySlot);
                    state.lastMatches = matches;
                    const damageReadings = {
                        damage1: recognizeDamageValue("damage1"),
                        damage2: recognizeDamageValue("damage2")
                    };
                    updateMatchCards(matches);
                    drawOverlay(matches, damageReadings);
                    if (handlePendingDamages(matches, damageReadings)) {
                        updateFps(now);
                        queueLoop(runFrame);
                        return;
                    }
                    const candidate = pickCandidate(matches);
                    if (candidate && candidate.score < ACTION_THRESHOLD) {
                        updateFps(now);
                        queueLoop(runFrame);
                        return;
                    }
                    if (!maybeResetFromCombo(candidate)) {
                        acceptCandidate(candidate);
                    }
                } catch (error) {
                    if (state.matcherKind === "webgpu") {
                        await recoverWebGpuMatcher();
                        return;
                    } else {
                        setStatus("visionStatusError");
                        console.error("vision matcher error:", error);
                        return;
                    }
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
        ui.debugExportButton.addEventListener("click", () => {
            downloadRecognizedMatchCrops();
        });
        ui.resetCancel.addEventListener("click", () => {
            closeResetDialog();
        });
        ui.resetConfirm.addEventListener("click", () => {
            closeResetDialog();
            resetConsoleState();
        });
        ui.gpuWarningCancel?.addEventListener("click", () => {
            closeGpuWarningDialog(false);
        });
        ui.gpuWarningStart?.addEventListener("click", () => {
            closeGpuWarningDialog(true);
        });
        ui.gpuWarningDialog?.addEventListener("click", (event) => {
            if (event.target === ui.gpuWarningDialog) {
                closeGpuWarningDialog(false);
            }
        });
        navigator.mediaDevices?.addEventListener?.("devicechange", () => {
            populateCameras().catch(() => {
            });
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
