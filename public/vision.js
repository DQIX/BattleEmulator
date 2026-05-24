(() => {
    const ui = {
        status: document.getElementById("visionStatus"),
        cameraSelect: document.getElementById("visionCameraSelect"),
        modeSelect: document.getElementById("visionModeSelect"),
        inspectRate: document.getElementById("visionInspectRate"),
        permissionButton: document.getElementById("visionPermissionButton"),
        connectButton: document.getElementById("visionConnectButton"),
        resetButton: document.getElementById("visionResetButton"),
        debugExportButton: document.getElementById("visionDebugExportButton"),
        applyFormatButton: document.getElementById("visionApplyFormatButton"),
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
        matches: Array.from(document.querySelectorAll("#visionMatches .vision-match-card")),
        copyMarkdown: document.getElementById("visionCopyMarkdown"),
        copyCsv: document.getElementById("visionCopyCsv")
    };

    if (!ui.status || !ui.video || !ui.overlay) {
        return;
    }

    const BASE_WIDTH = 958;
    const BASE_HEIGHT = 718;
    const SOURCE_1080P = {width: 1920, height: 1080};
    const SOURCE_720P = {width: 1280, height: 720};
    const VISION_ASSET_PACK_URL = "vision-assets.json";
    const VISION_ASSET_EMBED_KEY = "__VISION_ASSET_PACK__";
    const DEFAULT_VISION_THRESHOLDS = Object.freeze({
        templateThreshold: 0.45,
        resetLatchClearScore: 0.6,
        whiteThreshold: 0.72,
        whiteSaturationMaxDark: 0.20,//こっちのほうを小さくないといけない
        whiteSaturationMaxBright: 0.26,//こっちが大きい
        whiteSaturationDarkValue: 0.10,
        numberWhiteSaturationMaxDark: 0.20,
        numberWhiteSaturationMaxBright: 0.28,
        numberWhiteThresholdBright: 0.70,
        whiteSaturationBrightValue: 0.9,
        numberWhiteThresholdDark: 0.59,
        actionThreshold: 0.45,
        numberThreshold: 0.65,
        matchPenaltyWeight: 0.0,
        matchWhiteWeight: 1.0,
        templateAlphaThreshold: 0.05
    });
    const VISION_MODE_THRESHOLDS = Object.freeze({
        identify: normalizeVisionThresholds({
            // 待機(認識) モード
            whiteSaturationMaxDark: 0.20,//こっちのほうを小さくないといけない
            whiteSaturationMaxBright: 0.20,//こっちが大きい
        }),
        erugiosu: normalizeVisionThresholds({
            // エルギオスモード
        }),
    });
    const DAMAGE_CONFIRMATION_MS = 1500;
    const MATCH_SLOT_KEYS = ["main", "sub", "ally", "target"];
    const overlayContext = ui.overlay.getContext("2d");
    overlayContext.imageSmoothingEnabled = false; // ★ 追加
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
    const RECOGNIZED_CROP_DEFS = {
        main: {width: 130, height: 45},
        number: {width: 26, height: 40},
        ally: {width: 100, height: 45},
        sub: {width: 40, height: 60},
        target: {width: 130, height: 45}
    };

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
            width: 180,
            height: 60,
            actionAreas: [
                {x: 0, y: 0, width: 60, height: 60},
                {x: 43, y: 0, width: 60, height: 60},
                {x: 87, y: 0, width: 60, height: 60},
                {x: 87+44, y: 0, width: 60, height: 60}
            ]
        }
    };

    const ACTIONS = {
        1: {names: {ja: "攻撃(敵)", en: "Attack (enemy)"}, ally: false, damage: true},
        2: {names: {ja: "超高速連打", en: "Ultra High Speed Combo"}, ally: false, damage: true},
        5: {names: {ja: "ジゴスパ", en: "Lightning Storm"}, ally: false, damage: true},
        6: {names: {ja: "痛恨", en: "Critical Attack"}, ally: false, damage: true},
        8: {names: {ja: "上空から攻撃", en: "Sky Attack"}, ally: false, damage: true},
        9: {names: {ja: "メラゾーマ", en: "Kafrizzle"}, ally: false, damage: true},
        10: {names: {ja: "凍える吹雪", en: "Freezing Blizzard"}, ally: false, damage: true},
        12: {names: {ja: "あやしいひとみ", en: "Lullab-Eye"}, ally: false, damage: false},
        15: {names: {ja: "笑い", en: "Laugh"}, ally: false, damage: false},
        16: {names: {ja: "凍てつく波動", en: "Disruptive Wave"}, ally: false, damage: false},
        17: {names: {ja: "やけつくいき", en: "Burning Breath"}, ally: false, damage: false},
        18: {names: {ja: "黒輝く息", en: "Dark Breath"}, ally: false, damage: true},
        22: {names: {ja: "やすみ", en: "inactive"}, ally: true, damage: false},
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
        62: {names: {ja: "ためる(味方)", en: "Psyche Up (ally)"}, ally: true, damage: false},
        64: {names: {ja: "火炎斬り", en: "FLAME_SLASH"}, ally: false, damage: true},
        65: {names: {ja: "マヒャド斬り", en: "KACRACKLE_SLASH"}, ally: false, damage: true},
        66: {names: {ja: "魔人切り", en: "HATCHET_MAN"}, ally: false, damage: true},
        67: {names: {ja: "斬り上げた", en: "UPWARD_SLICE"}, ally: false, damage: true},
        68: {names: {ja: "さみだれ斬り", en: "MULTISLASH"}, ally: false, damage: true}
    };
    const ACTION_IDS = Object.freeze({
        ATTACK_ENEMY: 1,
        ULTRA_HIGH_SPEED_COMBO: 2,
        SWITCH_2B: 3,
        SWITCH_2C: 4,
        LIGHTNING_STORM: 5,
        CRITICAL_ATTACK: 6,
        SKY_ATTACK: 8,
        MERA_ZOMA: 9,
        FREEZING_BLIZZARD: 10,
        SWITCH_2A: 11,
        LULLAB_EYE: 12,
        SWITCH_2E: 13,
        LAUGH: 15,
        DISRUPTIVE_WAVE: 16,
        BURNING_BREATH: 17,
        DARK_BREATH: 18,
        SWITCH_2D: 19,
        INACTIVE_ENEMY: 21,
        INACTIVE_ALLY: 22,
        MEDICINAL_HERBS: 23,
        PARALYSIS: 24,
        ATTACK_ALLY: 25,
        HEAL: 26,
        DEFENCE: 27,
        CURE_PARALYSIS: 28,
        BUFF: 30,
        MAGIC_MIRROR: 31,
        MORE_HEAL: 32,
        DOUBLE_UP: 33,
        MULTITHRUST: 34,
        SLEEPING: 35,
        MIDHEAL: 36,
        FULLHEAL: 37,
        DEFENDING_CHAMPION: 38,
        PSYCHE_UP: 39,
        CURE_SLEEPING: 40,
        MEDITATION: 41,
        MAGIC_BURST: 42,
        RESTORE_MP: 43,
        MERCURIAL_THRUST: 44,
        THUNDER_THRUST: 45,
        TURN_SKIPPED: 46,
        SAGE_ELIXIR: 47,
        ELFIN_ELIXIR: 48,
        MAGIC_WATER: 49,
        SPECIAL_MEDICINE: 50,
        DEAD: 51,
        SONG: 52,
        FLEE: 53,
        PSYCHE_UP_ALLY: 62,
        FLAME_SLASH: 64,
        KACRACKLE_SLASH: 65,
        HATCHET_MAN: 66,
        UPWARD_SLICE: 67,
        MULTISLASH: 68
    });
    const ACTIONS_BY_ID = ACTIONS;

    function normalizeVisionThresholds(...sources) {
        const thresholds = {...DEFAULT_VISION_THRESHOLDS};
        for (const source of sources) {
            if (!source || typeof source !== "object" || Array.isArray(source)) {
                continue;
            }
            for (const [key, value] of Object.entries(source)) {
                if (Object.prototype.hasOwnProperty.call(DEFAULT_VISION_THRESHOLDS, key) && Number.isFinite(value)) {
                    thresholds[key] = value;
                }
            }
        }
        return thresholds;
    }

    const LEGACY_VISION_MODE_DEFINITIONS = Object.freeze({
        erugiosu: {
            id: "erugiosu",
            names: {
                ja: "エルギオスモード",
                en: "Erugiosu Mode"
            },
            picker: "erugiosu",
            timeoutMs: 4 * 60 * 1000,
            battleEmulator: {
                branch: "erugiosu_new_arugo"
            },
            thresholds: VISION_MODE_THRESHOLDS.erugiosu,
            identify: {
                templates: [
                    {slot: "main", directory: "message_v2", file: "erugio.png"},
                    {slot: "main", directory: "message_v2", file: "erugio2.png"},
                    {slot: "main", directory: "message_v2", file: "erugio4.png"}
                ]
            },
            rules: {
                erugioMain: ["erugio.png", "erugio2.png", "erugio4.png"],
                resetSub: ["reset.png"],
                enemyAttackSub: ["attack.png"],
                uhscSub: ["uhsc.png"],
                allyAttack: ["a_attack.png"],
                dead: ["dead.png", "dead2.png"],
                wakeUp: ["WakeUp.png", "WakeUp2.png", "WakeUp3.png"],
                psycheUpTarget: ["aha.png"],
                directMainActions: {
                    "sukara.png": 30,
                    "hadou.png": 16,
                    "yaketuku.png": 17,
                    "zilyoukuu.png": 8,
                    "merazoma.png": 9,
                    "mira-.png": 31,
                    "samidare.png": 34,
                    "samidare2.png": 34,
                    "no_hadou.png": 15,
                    "zigosupa.png": 5,
                    "kuroi.png": 18,
                    "sutemi.png": 33,
                    "seisui.png": 49,
                    "meisou.png": 41,
                    "madannte.png": 42,
                    "ice.png": 10,
                    "fullheal.png": 37,
                    "more_heal.png": 32,
                    "ayasii.png": 12,
                    "mp2.png": 43,
                    "song.png": 52,
                    "sippuu.png": 44,
                    "sage.png": 47,
                    "elven.png": 48,
                    "flee.png": 53,
                    "tokuyaku.png": 50
                }
            }
        },
        gilyumei1: {
            id: "gilyumei1",
            names: {
                ja: "ギュメイ1モード",
                en: "Gilyumei 1 Mode"
            },
            picker: "gilyumei1",
            timeoutMs: 4 * 60 * 1000,
            battleEmulator: {
                branch: "gilyumei1"
            },
            thresholds: VISION_MODE_THRESHOLDS.gilyumei1,
            identify: {
                templates: []
            },
            rules: {}
        }
    });

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
        assetPackPromise: null,
        modes: [],
        modeId: "identify",
        activeMode: null,
        activeThresholds: DEFAULT_VISION_THRESHOLDS,
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
        pendingDamage1ConfirmUntil: 0,
        pendingDamage2ConfirmUntil: 0,
        maybeCritical: -1,
        // 以下追加
        actionTaken: false,   // C#のActionTaken相当
        sleeping: false,      // C#のSleeping相当
        slept: false,         // C#のslept相当
        daibougilyo: false,   // C#のdaibougilyo相当
        matcherKind: "",
        gpuRecoveryInProgress: false,
        gpuWarningResolver: null,
        lastModeHitAt: 0,
        captureRect: {
            sourceWidth: BASE_WIDTH,
            sourceHeight: BASE_HEIGHT,
            sourceX: 0,
            sourceY: 0,
            sourceCropWidth: BASE_WIDTH,
            sourceCropHeight: BASE_HEIGHT
        }
    };

    function getEmbeddedVisionAssetPack() {
        const embedded = window[VISION_ASSET_EMBED_KEY];
        return embedded && typeof embedded === "object" ? embedded : null;
    }

    function getLegacyModeDefinition(modeId) {
        return LEGACY_VISION_MODE_DEFINITIONS[modeId] || null;
    }

    function resolveModeThresholds(mode) {
        return normalizeVisionThresholds(mode?.thresholds);
    }

    function getActiveThresholds() {
        return state.activeThresholds || DEFAULT_VISION_THRESHOLDS;
    }

    function buildLegacyVisionAssetPack(rawPack) {
        const erugiosu = getLegacyModeDefinition("erugiosu");
        const identifyDetections = [{
            modeId: "erugiosu",
            templates: erugiosu.identify.templates
        }];
        return {
            version: rawPack.version || 1,
            generatedAt: rawPack.generatedAt || null,
            modes: [
                {
                    id: "identify",
                    names: {ja: "識別モード", en: "Identify Mode"},
                    picker: "identify",
                    timeoutMs: 0,
                    battleEmulator: null,
                    thresholds: VISION_MODE_THRESHOLDS.identify,
                    rules: {detections: identifyDetections},
                    identify: {templates: []},
                    templates: (rawPack.templates || []).filter((entry) =>
                        identifyDetections.some((detection) =>
                            detection.templates.some((template) =>
                                template.slot === entry.slot && template.file === entry.file
                            )
                        )
                    )
                },
                {
                    ...erugiosu,
                    templates: rawPack.templates || []
                },
                {
                    ...getLegacyModeDefinition("gilyumei1"),
                    templates: []
                }
            ],
            numberTemplates: rawPack.numberTemplates || [],
            assets: {}
        };
    }

    function normalizeVisionAssetMap(rawAssets) {
        return rawAssets && typeof rawAssets === "object" && !Array.isArray(rawAssets) ? rawAssets : {};
    }

    function normalizeVisionAssetPack(rawPack) {
        if (!rawPack || typeof rawPack !== "object") {
            return buildLegacyVisionAssetPack({templates: [], numberTemplates: []});
        }

        if (!Array.isArray(rawPack.modes)) {
            return buildLegacyVisionAssetPack(rawPack);
        }

        const normalizedModes = rawPack.modes.map((mode) => {
            const legacy = getLegacyModeDefinition(mode.id);
            return {
                ...legacy,
                ...mode,
                names: {
                    ...(legacy?.names || {}),
                    ...(mode.names || {})
                },
                thresholds: normalizeVisionThresholds(legacy?.thresholds, mode?.thresholds),
                rules: {
                    ...(legacy?.rules || {}),
                    ...(mode.rules || {})
                },
                identify: {
                    templates: [],
                    ...(legacy?.identify || {}),
                    ...(mode.identify || {})
                },
                templates: Array.isArray(mode.templates) ? mode.templates : []
            };
        });

        const modeIds = new Set(normalizedModes.map((mode) => mode.id));
        for (const legacyMode of Object.values(LEGACY_VISION_MODE_DEFINITIONS)) {
            if (!modeIds.has(legacyMode.id)) {
                normalizedModes.push({...legacyMode, templates: []});
            }
        }

        if (!modeIds.has("identify")) {
            normalizedModes.unshift({
                id: "identify",
                names: {ja: "識別モード", en: "Identify Mode"},
                picker: "identify",
                timeoutMs: 0,
                battleEmulator: null,
                thresholds: VISION_MODE_THRESHOLDS.identify,
                rules: {detections: []},
                identify: {templates: []},
                templates: []
            });
        }

        return {
            version: rawPack.version || 2,
            generatedAt: rawPack.generatedAt || null,
            modes: normalizedModes,
            numberTemplates: rawPack.numberTemplates || [],
            assets: normalizeVisionAssetMap(rawPack.assets)
        };
    }

    function getActiveMode() {
        return state.activeMode;
    }

    function getModeRuleList(mode, key) {
        const list = mode?.rules?.[key];
        return Array.isArray(list) ? list : [];
    }

    function getModeRuleMap(mode, key) {
        const value = mode?.rules?.[key];
        return value && typeof value === "object" && !Array.isArray(value) ? value : {};
    }

    function modeRuleHasFile(mode, key, file) {
        if (!file) {
            return false;
        }
        return getModeRuleList(mode, key).includes(file);
    }

    function getModeDetectionEntries(mode) {
        const detections = mode?.rules?.detections;
        return Array.isArray(detections) ? detections : [];
    }

    class BattleEmulatorBridge {
        send(snapshot) {
            const activeMode = getActiveMode();
            const payload = {
                emulator: activeMode?.battleEmulator || null,
                visionMode: activeMode?.id || "identify",
                sentAt: new Date().toISOString(),
                currentTurn: snapshot.currentTurn,
                currentSlot: snapshot.currentSlot,
                command: snapshot.command,
                history: snapshot.history
            };
            const encoded = encodeBridgePayload(payload);
            const formatted = buildBattleActionFormat(snapshot.history);
            window.postMessage(
                {
                    type: "battle-emulator-vision-sync",
                    payload,
                    encoded,
                    formatted
                },
                "*"
            );
            return {
                encoded: formatted,
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
  penaltyWeight: f32,
  whiteWeight: f32,
  valueThreshold: f32,
  saturationMax: f32,
};

@group(0) @binding(0) var frameTex: texture_2d<f32>;
@group(0) @binding(1) var templateMaskTex: texture_2d<f32>;
@group(0) @binding(2) var<storage, read_write> scores: array<f32>;
@group(0) @binding(3) var<uniform> params: Params;

fn hsvSaturation(rgb: vec3<f32>) -> f32 {
  let maxChannel = max(max(rgb.r, rgb.g), rgb.b);
  let minChannel = min(min(rgb.r, rgb.g), rgb.b);
  return select(0.0, (maxChannel - minChannel) / maxChannel, maxChannel > 0.0);
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
      let sample = textureLoad(frameTex, framePos, 0);
      let value = max(max(sample.r, sample.g), sample.b);
      let saturation = hsvSaturation(sample.rgb);
      let frameWhitePixel = select(0.0, 1.0, value >= params.valueThreshold && saturation <= params.saturationMax);
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

        // 全スロット・全テンプレートを1回のsubmit + 1回のmapAsyncで処理する
        async match(source, templatesBySlot) {
            this.uploadFrame(source);
            const device = this.device;
            const frameView = this.frameTexture.createView();
            const frame = processingContext.getImageData(0, 0, BASE_WIDTH, BASE_HEIGHT);
            const thresholds = getActiveThresholds();
            const whiteParamsBySlot = buildWhiteParamsBySlot(frame);

            // --- パス1: 全テンプレートのGPUジョブを1つのencoderに積む ---
            // 各テンプレートのメタ情報（オフセット・サイズ・バッファ参照）を収集
            const jobs = []; // {slot, template, scoreWidth, scoreHeight, scoreCount, scoreOffset, whiteParams}
            let totalScoreCount = 0;

            for (const slot of MATCH_SLOT_KEYS) {
                const roi = ROI_DEFS[slot];
                for (const template of (templatesBySlot.get(slot) || [])) {
                    const scoreWidth = roi.width - template.width + 1;
                    const scoreHeight = roi.height - template.height + 1;
                    if (scoreWidth < 1 || scoreHeight < 1) continue;
                    const scoreCount = scoreWidth * scoreHeight;
                    jobs.push({
                        slot,
                        roi,
                        template,
                        scoreWidth,
                        scoreHeight,
                        scoreCount,
                        scoreOffset: totalScoreCount,
                        whiteParams: whiteParamsBySlot[slot]
                    });
                    totalScoreCount += scoreCount;
                }
            }

            if (totalScoreCount === 0) {
                return Object.fromEntries(MATCH_SLOT_KEYS.map(slot => [slot, emptyMatch(slot)]));
            }

            // 全スコアをまとめるreadBuffer（1回のmapAsyncのため）
            // copyBufferToBuffer の destinationOffset は 256バイトアライメントが必要
            const ALIGN = 256;
            const align256 = (n) => Math.ceil(n / ALIGN) * ALIGN;

            // 各ジョブのcopyOffsetを事前に256アライメントで計算し、readBufferサイズを確定
            let totalAlignedSize = 0;
            for (const job of jobs) {
                job.alignedOffset = totalAlignedSize;
                totalAlignedSize += align256(job.scoreCount * 4);
            }

            const readBuffer = device.createBuffer({
                size: Math.max(totalAlignedSize, ALIGN),
                usage: GPUBufferUsage.COPY_DST | GPUBufferUsage.MAP_READ
            });

            const encoder = device.createCommandEncoder();
            const pass = encoder.beginComputePass();
            pass.setPipeline(this.pipeline);

            const scoreBuffers = [];
            const uniformBuffers = [];

            for (const job of jobs) {
                const {roi, template, scoreWidth, scoreHeight, scoreCount, whiteParams} = job;

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
                paramsView.setFloat32(32, thresholds.matchPenaltyWeight, true);
                paramsView.setFloat32(36, thresholds.matchWhiteWeight, true);
                paramsView.setFloat32(40, thresholds.whiteThreshold, true);
                paramsView.setFloat32(44, whiteParams.saturationMax, true);

                const uniformBuffer = device.createBuffer({
                    size: 64,
                    usage: GPUBufferUsage.UNIFORM | GPUBufferUsage.COPY_DST
                });
                this.queue.writeBuffer(uniformBuffer, 0, paramsBuffer);

                const scoreBuffer = device.createBuffer({
                    size: scoreCount * 4,
                    usage: GPUBufferUsage.STORAGE | GPUBufferUsage.COPY_SRC
                });

                const bindGroup = device.createBindGroup({
                    layout: this.pipeline.getBindGroupLayout(0),
                    entries: [
                        {binding: 0, resource: frameView},
                        {binding: 1, resource: template.texture.createView()},
                        {binding: 2, resource: {buffer: scoreBuffer}},
                        {binding: 3, resource: {buffer: uniformBuffer}}
                    ]
                });

                pass.setBindGroup(0, bindGroup);
                pass.dispatchWorkgroups(Math.ceil(scoreWidth / 8), Math.ceil(scoreHeight / 8));

                job.scoreBufferRef = scoreBuffer;
                scoreBuffers.push(scoreBuffer);
                uniformBuffers.push(uniformBuffer);
            }

            pass.end();

            // 全スコアバッファをreadBufferへコピー
            for (const job of jobs) {
                encoder.copyBufferToBuffer(job.scoreBufferRef, 0, readBuffer, job.alignedOffset, job.scoreCount * 4);
            }

            // 1回のsubmit
            this.queue.submit([encoder.finish()]);

            // 1回のmapAsync
            await readBuffer.mapAsync(GPUMapMode.READ);
            const allScores = new Float32Array(readBuffer.getMappedRange());

            // --- パス2: CPUで各スロットのベストを選ぶ ---
            const bestBySlot = Object.fromEntries(MATCH_SLOT_KEYS.map(slot => [slot, emptyMatch(slot)]));

            for (const job of jobs) {
                const {slot, roi, template, scoreWidth, scoreHeight, scoreCount, alignedOffset} = job;
                const byteOffset = alignedOffset / 4; // Float32Arrayインデックス（alignedOffsetはバイト単位）
                let bestScore = 0;
                let bestIndex = 0;
                const end = byteOffset + scoreCount;
                for (let i = byteOffset; i < end; i++) {
                    if (allScores[i] > bestScore) {
                        bestScore = allScores[i];
                        bestIndex = i - byteOffset;
                    }
                }
                if (bestScore > bestBySlot[slot].score) {
                    bestBySlot[slot] = {
                        slot,
                        file: template.file,
                        score: bestScore,
                        x: roi.x + (bestIndex % scoreWidth),
                        y: roi.y + Math.floor(bestIndex / scoreWidth),
                        width: template.width,
                        height: template.height
                    };
                }
            }

            readBuffer.unmap();

            // バッファ解放
            readBuffer.destroy();
            for (const buf of scoreBuffers) buf.destroy();
            for (const buf of uniformBuffers) buf.destroy();

            return bestBySlot;
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
            const whiteParamsBySlot = buildWhiteParamsBySlot(frame);
            const matches = {};
            for (const slot of MATCH_SLOT_KEYS) {
                matches[slot] = this.matchSlot(frame, ROI_DEFS[slot], templatesBySlot.get(slot) || [], slot, whiteParamsBySlot[slot]);
            }
            return matches;
        }

        matchSlot(frame, roi, templates, slot, whiteParams) {
            let best = emptyMatch(slot);
            for (const template of templates) {
                const maxX = roi.width - template.width;
                const maxY = roi.height - template.height;
                for (let offsetY = 0; offsetY <= maxY; offsetY += 1) {
                    for (let offsetX = 0; offsetX <= maxX; offsetX += 1) {
                        const score = compareMask(frame, template.mask, roi.x + offsetX, roi.y + offsetY, template.width, template.height, whiteParams);
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
        const thresholds = getActiveThresholds();
        const mask = new Uint8Array(imageData.width * imageData.height);
        const data = imageData.data;
        const whiteParams = buildWhiteParamsForImageData(imageData);
        for (let index = 0; index < mask.length; index += 1) {
            const offset = index * 4;
            mask[index] = isWhitePixel(
                data[offset],
                data[offset + 1],
                data[offset + 2],
                data[offset + 3],
                thresholds.whiteThreshold,
                thresholds.templateAlphaThreshold,
                whiteParams
            ) ? 1 : 0;
        }
        return mask;
    }

    function getHsvValue(r, g, b) {
        return Math.max(r, g, b) / 255;
    }

    function getHsvSaturation(r, g, b) {
        const maxChannel = Math.max(r, g, b);
        if (maxChannel <= 0) {
            return 0;
        }
        const minChannel = Math.min(r, g, b);
        return (maxChannel - minChannel) / maxChannel;
    }

    function interpolateWhiteParamByBackground(value, darkParam, brightParam) {
        const thresholds = getActiveThresholds();
        const range = Math.max(0.001, thresholds.whiteSaturationBrightValue - thresholds.whiteSaturationDarkValue);
        const ratio = Math.max(0, Math.min(1, (value - thresholds.whiteSaturationDarkValue) / range));
        return darkParam + (brightParam - darkParam) * ratio;
    }

    function getWhiteSaturationMaxForBackground(value) {
        const thresholds = getActiveThresholds();
        return interpolateWhiteParamByBackground(
            value,
            thresholds.whiteSaturationMaxDark,
            thresholds.whiteSaturationMaxBright
        );
    }

    function getNumberWhiteThresholdForBackground(value) {
        const thresholds = getActiveThresholds();
        return interpolateWhiteParamByBackground(
            value,
            thresholds.numberWhiteThresholdDark,
            thresholds.numberWhiteThresholdBright
        );
    }

    function getNumberWhiteSaturationMaxForBackground(value) {
        const thresholds = getActiveThresholds();
        return interpolateWhiteParamByBackground(
            value,
            thresholds.numberWhiteSaturationMaxDark,
            thresholds.numberWhiteSaturationMaxBright
        );
    }

    function estimateBackgroundValue(imageData, area = null) {
        const x = Math.max(0, area?.x ?? 0);
        const y = Math.max(0, area?.y ?? 0);
        const width = Math.max(0, Math.min(area?.width ?? imageData.width, imageData.width - x));
        const height = Math.max(0, Math.min(area?.height ?? imageData.height, imageData.height - y));
        const sampleStep = Math.max(1, Math.floor(Math.sqrt((width * height) / 2048)));
        const values = [];

        for (let row = y; row < y + height; row += sampleStep) {
            for (let col = x; col < x + width; col += sampleStep) {
                const offset = (row * imageData.width + col) * 4;
                if (imageData.data[offset + 3] <= 0) {
                    continue;
                }
                values.push(getHsvValue(
                    imageData.data[offset],
                    imageData.data[offset + 1],
                    imageData.data[offset + 2]
                ));
            }
        }

        if (!values.length) {
            return getActiveThresholds().whiteThreshold;
        }

        values.sort((left, right) => left - right);
        return values[Math.floor(values.length / 2)];
    }

    function buildWhiteParamsForImageData(imageData, area = null) {
        // return {
        //     saturationMax: 0.25
        // };
        // console.log( getWhiteSaturationMaxForBackground(estimateBackgroundValue(imageData, area)));
        return {
            saturationMax: getWhiteSaturationMaxForBackground(estimateBackgroundValue(imageData, area))
        };
    }

    function buildNumberWhiteParamsForImageData(imageData, area = null) {
        const backgroundValue = estimateBackgroundValue(imageData, area);
        return {
            threshold: getNumberWhiteThresholdForBackground(backgroundValue),
            saturationMax: getNumberWhiteSaturationMaxForBackground(backgroundValue)
        };
    }

    function buildWhiteParamsBySlot(frame) {
        const params = {};
        for (const slot of MATCH_SLOT_KEYS) {
            params[slot] = buildWhiteParamsForImageData(frame, ROI_DEFS[slot]);
        }
        return params;
    }

    function isWhitePixel(r, g, b, a, threshold, alphaThreshold, whiteParams) {
        if (a / 255 < alphaThreshold) {
            return false;
        }
        const value = getHsvValue(r, g, b);
        const saturationMax = whiteParams?.saturationMax ?? getActiveThresholds().whiteSaturationMaxDark;
        return value >= threshold && getHsvSaturation(r, g, b) <= saturationMax;
    }

    function isNumberWhitePixel(r, g, b, a, whiteParams) {
        if (a <= 0) {
            return false;
        }
        const thresholds = getActiveThresholds();
        const threshold = whiteParams?.threshold ?? thresholds.numberWhiteThresholdDark;
        const saturationMax = whiteParams?.saturationMax ?? thresholds.numberWhiteSaturationMaxDark;
        return getHsvValue(r, g, b) >= threshold
            && getHsvSaturation(r, g, b) <= saturationMax;
    }

    function shiftWhitePixelsToTopLeft(imageData) {
        const {width, height, data} = imageData;
        let minX = width;
        let minY = height;
        const mask = new Uint8Array(width * height);
        for (let row = 0; row < height; row += 1) {
            for (let col = 0; col < width; col += 1) {
                const index = row * width + col;
                if (data[index * 4] !== 255) {
                    continue;
                }
                mask[index] = 1;
                if (col < minX) {
                    minX = col;
                }
                if (row < minY) {
                    minY = row;
                }
            }
        }
        data.fill(0);
        for (let index = 3; index < data.length; index += 4) {
            data[index] = 255;
        }
        if (minX === width || minY === height) {
            return;
        }
        for (let row = minY; row < height; row += 1) {
            for (let col = minX; col < width; col += 1) {
                const srcIndex = row * width + col;
                if (!mask[srcIndex]) {
                    continue;
                }
                const dstX = col - minX;
                const dstY = row - minY;
                const dstOffset = (dstY * width + dstX) * 4;
                data[dstOffset] = 255;
                data[dstOffset + 1] = 255;
                data[dstOffset + 2] = 255;
            }
        }
    }

    function buildNormalizedMonochromeImageData(sourceImageData, targetWidth, targetHeight) {
        const thresholds = getActiveThresholds();
        const result = new ImageData(targetWidth, targetHeight);
        const resultData = result.data;
        for (let index = 3; index < resultData.length; index += 4) {
            resultData[index] = 255;
        }
        const sourceData = sourceImageData.data;
        const whiteParams = buildWhiteParamsForImageData(sourceImageData);
        let minX = sourceImageData.width;
        let minY = sourceImageData.height;
        const mask = new Uint8Array(sourceImageData.width * sourceImageData.height);
        for (let row = 0; row < sourceImageData.height; row += 1) {
            for (let col = 0; col < sourceImageData.width; col += 1) {
                const pixelIndex = row * sourceImageData.width + col;
                const offset = pixelIndex * 4;
                const white = isWhitePixel(
                    sourceData[offset],
                    sourceData[offset + 1],
                    sourceData[offset + 2],
                    sourceData[offset + 3],
                    thresholds.whiteThreshold,
                    0,
                    whiteParams
                );
                if (!white) {
                    continue;
                }
                mask[pixelIndex] = 1;
                if (col < minX) {
                    minX = col;
                }
                if (row < minY) {
                    minY = row;
                }
            }
        }
        if (minX === sourceImageData.width || minY === sourceImageData.height) {
            return result;
        }
        for (let row = minY; row < sourceImageData.height; row += 1) {
            for (let col = minX; col < sourceImageData.width; col += 1) {
                const sourceIndex = row * sourceImageData.width + col;
                if (!mask[sourceIndex]) {
                    continue;
                }
                const dstX = col - minX;
                const dstY = row - minY;
                if (dstX >= targetWidth || dstY >= targetHeight) {
                    continue;
                }
                const dstOffset = (dstY * targetWidth + dstX) * 4;
                resultData[dstOffset] = 255;
                resultData[dstOffset + 1] = 255;
                resultData[dstOffset + 2] = 255;
            }
        }
        return result;
    }

    function compareMask(frame, templateMask, x, y, width, height, whiteParams) {
        const thresholds = getActiveThresholds();
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
                    thresholds.whiteThreshold,
                    0,
                    whiteParams
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
        return Math.max(0, (overlap * thresholds.matchWhiteWeight - penalty * thresholds.matchPenaltyWeight) / union);
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
        processingContext.imageSmoothingEnabled = false; // ★ 追加
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

    function buildNumberWhiteMask(imageData) {
        const mask = new Uint8Array(imageData.width * imageData.height);
        const data = imageData.data;
        const whiteParams = buildNumberWhiteParamsForImageData(imageData);
        for (let index = 0; index < mask.length; index += 1) {
            const offset = index * 4;
            mask[index] = isNumberWhitePixel(
                data[offset],
                data[offset + 1],
                data[offset + 2],
                data[offset + 3],
                whiteParams
            ) ? 1 : 0;
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

        if (foundX === -1) return {...binary, sourceX: 0, sourceY: 0};

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
        return {width: targetWidth, height: targetHeight, mask, sourceX: foundX, sourceY: foundY};
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
        const thresholds = getActiveThresholds();
        const config = DAMAGE_ROIS[key];
        const cropped = processingContext.getImageData(config.x, config.y, config.width, config.height);
        const binary = buildNumberWhiteMask(cropped);
        const digits = config.actionAreas.map((area) => {
            const trimmed = trimFirstPixel(cropMask(binary, area), 26, 40);
            // サイズチェックを撤廃（trimFirstPixelが常にtargetWidth×targetHeightを返すため）
            let bestDigit = -1;
            let bestScore = 0;
            state.numberTemplates.forEach((template) => {
                const score = compareBinaryImages(trimmed, template.mask);
                if (score >= thresholds.numberThreshold && score >= bestScore) {
                    bestDigit = template.digit;
                    bestScore = score;
                }
            });
            // trimFirstPixel が返す sourceX/sourceY = actionArea内での検出起点
            return {digit: bestDigit, score: bestScore, localX: trimmed.sourceX ?? 0, localY: trimmed.sourceY ?? 0};
        });
        return {
            digits: digits.map((item) => item.digit),
            scores: digits.map((item) => item.score),
            score: digits.reduce((max, item) => Math.max(max, item.score), 0),
            value: convertMatchResults(digits.map((item) => item.digit)),
            // 各桁の実際の検出位置（絶対座標）。認識失敗時は null
            positions: digits.map((item, i) => {
                if (item.digit === -1) return null;
                const area = config.actionAreas[i];
                return {
                    x: config.x + area.x + item.localX,
                    y: config.y + area.y + item.localY
                };
            })
        };
    }

    function shouldRecognizeDamageValue(key) {
        if (state.modeId === "identify") {
            return false;
        }
        const now = Date.now();
        if (key === "damage1") {
            return state.pendingDamage1 !== -1 && (state.pendingDamage1Enabled || now < state.pendingDamage1ConfirmUntil);
        }
        if (key === "damage2") {
            return state.pendingDamage2 !== -1 && (state.pendingDamage2Enabled || now < state.pendingDamage2ConfirmUntil);
        }
        return false;
    }

    function recognizePendingDamageValues() {
        const damageReadings = {};
        if (shouldRecognizeDamageValue("damage1")) {
            damageReadings.damage1 = recognizeDamageValue("damage1");
        }
        if (shouldRecognizeDamageValue("damage2")) {
            damageReadings.damage2 = recognizeDamageValue("damage2");
        }
        return damageReadings;
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
        const action = ACTIONS_BY_ID[actionId];
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
        const {templateThreshold} = getActiveThresholds();
        overlayContext.clearRect(0, 0, BASE_WIDTH, BASE_HEIGHT);
        overlayContext.imageSmoothingEnabled = false;
        overlayContext.drawImage(processingCanvas, 0, 0, BASE_WIDTH, BASE_HEIGHT);

        // 数字テンプレートの固定サイズ（26x40）
        const NUM_W = 26;
        const NUM_H = 40;

        // --- 数字認識枠（実際の検出位置をオレンジで） ---
        if (damageReadings) {
            overlayContext.font = "13px Bahnschrift, sans-serif";
            for (const [key, reading] of Object.entries(damageReadings)) {
                const config = DAMAGE_ROIS[key];
                if (!config) continue;

                // ROI全体枠（薄い水色）
                overlayContext.strokeStyle = "rgba(100, 210, 255, 0.45)";
                overlayContext.lineWidth = 1;
                overlayContext.strokeRect(config.x, config.y, config.width, config.height);

                // 認識値ラベル（ROI枠の外上側）
                const valueLabel = reading.value !== -1 ? `${key}: ${reading.value}` : `${key}: --`;
                const valueLabelWidth = overlayContext.measureText(valueLabel).width;
                const valueLabelY = config.y >= 18 ? config.y - 5 : config.y + config.height + 15;
                overlayContext.fillStyle = "rgba(10, 30, 50, 0.78)";
                overlayContext.fillRect(config.x, valueLabelY - 13, valueLabelWidth + 10, 16);
                overlayContext.fillStyle = reading.value !== -1
                    ? "rgba(100, 230, 255, 0.96)"
                    : "rgba(160, 160, 160, 0.8)";
                overlayContext.fillText(valueLabel, config.x + 5, valueLabelY);

                // 桁ごと：実際の検出位置（positions[i]）をオレンジ枠で囲む
                reading.digits.forEach((digit, index) => {
                    if (digit === -1) return;
                    const pos = reading.positions ? reading.positions[index] : null;
                    if (!pos) return;

                    const score = reading.scores ? reading.scores[index] : null;
                    const bx = pos.x;
                    const by = pos.y;

                    // 実際のマッチ位置をオレンジで囲む
                    overlayContext.strokeStyle = "rgba(255, 140, 0, 0.92)";
                    overlayContext.lineWidth = 1.5;
                    overlayContext.strokeRect(bx, by, NUM_W, NUM_H);

                    // スコアラベルを枠の外側（上）に表示
                    const digitLabel = score !== null
                        ? `${digit} (${(score * 100).toFixed(0)}%)`
                        : `${digit}`;
                    const labelWidth = overlayContext.measureText(digitLabel).width;
                    const labelY = by + NUM_H + 13;
                    overlayContext.fillStyle = "rgba(20, 10, 0, 0.78)";
                    overlayContext.fillRect(bx, labelY - 13, labelWidth + 8, 15);
                    overlayContext.fillStyle = "rgba(255, 160, 40, 0.97)";
                    overlayContext.fillText(digitLabel, bx + 4, labelY);
                });
            }
        }

        // --- テンプレートマッチング枠（既存） ---
        overlayContext.font = "18px Bahnschrift, sans-serif";
        for (const slot of MATCH_SLOT_KEYS) {
            const roi = ROI_DEFS[slot];
            const match = matches[slot] || emptyMatch(slot);
            overlayContext.strokeStyle = "rgba(230, 230, 230, 0.72)";
            overlayContext.lineWidth = 1;
            overlayContext.strokeRect(roi.x, roi.y, roi.width, roi.height);
            if (match.file && match.score >= templateThreshold) {
                overlayContext.strokeStyle = "rgba(255, 187, 92, 0.92)";
                overlayContext.lineWidth = 2;
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

    function resolveExportRect(slot, match) {
        const roi = ROI_DEFS[slot];
        if (!match || !match.file) {
            return {...roi, exportName: `${slot}-roi`};
        }
        return {
            x: match.x,
            y: match.y,
            width: match.width,
            height: match.height,
            exportName: `${slot}-${match.file.replace(/[^a-zA-Z0-9._-]/g, "_")}`
        };
    }

    function createMonochromeCropCanvas(slot, match) {
        const roi = ROI_DEFS[slot];
        const cropDef = RECOGNIZED_CROP_DEFS[slot];
        const exportName = match && match.file
            ? `${slot}-${match.file.replace(/[^a-zA-Z0-9._-]/g, "_")}`
            : `${slot}-roi`;
        const canvas = document.createElement("canvas");
        canvas.width = cropDef.width;
        canvas.height = cropDef.height;
        const context = canvas.getContext("2d", {willReadFrequently: true});
        context.imageSmoothingEnabled = false;
        const roiCanvas = document.createElement("canvas");
        roiCanvas.width = roi.width;
        roiCanvas.height = roi.height;
        const roiContext = roiCanvas.getContext("2d", {willReadFrequently: true});
        roiContext.imageSmoothingEnabled = false;
        roiContext.drawImage(
            processingCanvas,
            roi.x,
            roi.y,
            roi.width,
            roi.height,
            0,
            0,
            roi.width,
            roi.height
        );
        const normalized = buildNormalizedMonochromeImageData(
            roiContext.getImageData(0, 0, roi.width, roi.height),
            cropDef.width,
            cropDef.height
        );
        context.putImageData(normalized, 0, 0);
        return {
            canvas,
            exportName
        };
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

    function createDamageActionAreaCanvas(damageKey, areaIndex) {
        const config = DAMAGE_ROIS[damageKey];
        if (!config) return null;
        const area = config.actionAreas[areaIndex];
        if (!area) return null;

        const roiCanvas = document.createElement("canvas");
        roiCanvas.width = config.width;
        roiCanvas.height = config.height;
        const roiCtx = roiCanvas.getContext("2d", {willReadFrequently: true});
        roiCtx.imageSmoothingEnabled = false;
        roiCtx.drawImage(
            processingCanvas,
            config.x, config.y, config.width, config.height,
            0, 0, config.width, config.height
        );

        const roiImageData = roiCtx.getImageData(0, 0, config.width, config.height);
        const binary = buildNumberWhiteMask(roiImageData);
        const trimmed = trimFirstPixel(cropMask(binary, area), 26, 40);

        // Uint8Array mask → RGBA ImageData に変換
        const canvas = document.createElement("canvas");
        canvas.width = trimmed.width;
        canvas.height = trimmed.height;
        const ctx = canvas.getContext("2d");
        const imageData = ctx.createImageData(trimmed.width, trimmed.height);
        for (let i = 0; i < trimmed.mask.length; i++) {
            const v = trimmed.mask[i] ? 255 : 0;
            imageData.data[i * 4 + 0] = v;
            imageData.data[i * 4 + 1] = v;
            imageData.data[i * 4 + 2] = v;
            imageData.data[i * 4 + 3] = 255;
        }
        ctx.putImageData(imageData, 0, 0);

        return {
            canvas,
            exportName: `${damageKey}-area${areaIndex}`
        };
    }

    function downloadRecognizedMatchCrops() {
        const matches = state.lastMatches || {};
        let downloaded = 0;

        // 既存: スロットの切り抜き
        for (const slot of MATCH_SLOT_KEYS) {
            const match = matches[slot];
            const crop = createMonochromeCropCanvas(slot, match);
            downloadCanvas(crop.canvas, `${crop.exportName}-mono.png`);
            downloaded += 1;
        }

        // 追加: DAMAGE_ROISのactionAreaごとの数字切り抜き
        for (const damageKey of Object.keys(DAMAGE_ROIS)) {
            const config = DAMAGE_ROIS[damageKey];
            for (let i = 0; i < config.actionAreas.length; i++) {
                const crop = createDamageActionAreaCanvas(damageKey, i);
                if (crop) {
                    downloadCanvas(crop.canvas, `${crop.exportName}-mono.png`);
                    downloaded += 1;
                }
            }
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

    function pickIdentifyCandidate(matches) {
        const mode = getActiveMode();
        let best = null;

        for (const detection of getModeDetectionEntries(mode)) {
            for (const template of detection.templates || []) {
                const match = matches[template.slot] || emptyMatch(template.slot);
                if (match.file !== template.file || match.score < 0.50) {
                    continue;
                }
                if (!best || match.score > best.score) {
                    best = {
                        kind: "mode",
                        modeId: detection.modeId,
                        detail: `${template.slot}:${template.file}`,
                        score: match.score
                    };
                }
            }
        }

        return best;
    }

    function pickErugiosuCandidate(matches) {
        const {templateThreshold} = getActiveThresholds();
        const main = matches.main || emptyMatch("main");
        const sub = matches.sub || emptyMatch("sub");
        const ally = matches.ally || emptyMatch("ally");
        const target = matches.target || emptyMatch("target");
        const mode = getActiveMode();
        const erugioMain = modeRuleHasFile(mode, "erugioMain", main.file);

        // reset
        if (erugioMain && modeRuleHasFile(mode, "resetSub", sub.file) && main.score >= 0.6 && sub.score >= 0.6) {
            return {kind: "reset", score: Math.min(main.score, sub.score), detail: `${main.file} + ${sub.file}`};
        }

        // 攻撃(敵) = erugio + attack
        if (erugioMain && modeRuleHasFile(mode, "enemyAttackSub", sub.file) && main.score >= templateThreshold && sub.score >= templateThreshold) {
            return {
                kind: "action",
                actionId: ACTION_IDS.ATTACK_ENEMY,
                detail: `${main.file} + ${sub.file}`,
                score: Math.min(main.score, sub.score)
            };
        }

        // 超高速連打 = erugio + uhsc
        if (erugioMain && modeRuleHasFile(mode, "uhscSub", sub.file) && main.score >= templateThreshold && sub.score >= templateThreshold) {
            return {
                kind: "action",
                actionId: ACTION_IDS.ULTRA_HIGH_SPEED_COMBO,
                detail: `${main.file} + ${sub.file}`,
                score: Math.min(main.score, sub.score)
            };
        }

        // 攻撃(味方) = a_attack.png、ActionTaken未設定かつguardなし
        if (
            !state.actionTaken &&
            modeRuleHasFile(mode, "allyAttack", ally.file) &&
            ally.score >= templateThreshold &&
            !modeRuleHasFile(mode, "uhscSub", sub.file) &&
            main.file !== "guard.png"
        ) {
            return {kind: "action", actionId: ACTION_IDS.ATTACK_ALLY, detail: `${ally.file} (${main.file || "-"})`, score: ally.score};
        }

        // 大防御 combo
        if (
            main.file === "defense_champion.png" &&
            sub.file === "defense_champion2.png" &&
            main.score >= templateThreshold &&
            sub.score >= templateThreshold
        ) {
            return {
                kind: "action",
                actionId: ACTION_IDS.DEFENDING_CHAMPION,
                detail: "defense_champion combo",
                score: Math.min(main.score, sub.score)
            };
        }

        // daibougilyo: マダンテ後のsleeping2は大防御
        if (state.daibougilyo && main.file === "sleeping2.png" && main.score >= templateThreshold) {
            return {kind: "action", actionId: ACTION_IDS.DEFENDING_CHAMPION, detail: "madannte -> sleeping2 (daibougilyo)", score: main.score};
        }

        // 麻痺回復 = Paralysis + CareParalysis
        if (
            !state.slept &&
            main.file === "Paralysis.png" &&
            ally.file === "CareParalysis.png" &&
            ally.score >= templateThreshold
        ) {
            return {
                kind: "action",
                actionId: ACTION_IDS.CURE_PARALYSIS,
                detail: "Paralysis + CareParalysis",
                score: Math.min(main.score, ally.score)
            };
        }

        // 麻痺で動けない = Paralysis単体（Paralysis2なし）
        if (
            !state.slept &&
            main.file === "Paralysis.png" &&
            main.score >= templateThreshold &&
            (!sub.file || sub.file !== "Paralysis2.png" || sub.score < templateThreshold)
        ) {
            return {kind: "action", actionId: ACTION_IDS.PARALYSIS, detail: "Paralysis", score: main.score};
        }

        // しんでしまった = sleeping2 + dead/dead2
        if (
            !state.actionTaken &&
            main.file === "sleeping2.png" &&
            modeRuleHasFile(mode, "dead", ally.file) &&
            ally.score >= templateThreshold
        ) {
            return {
                kind: "action",
                actionId: ACTION_IDS.DEAD,
                detail: `sleeping2 + ${ally.file}`,
                score: Math.min(main.score, ally.score)
            };
        }

        // 眠っている！ = sleeping2単体（slept未設定）
        if (
            !state.slept &&
            main.file === "sleeping2.png" &&
            main.score >= templateThreshold &&
            (!ally.file || (ally.file !== "dead.png" && ally.file !== "dead2.png") || ally.score < templateThreshold)
        ) {
            return {kind: "action", actionId: ACTION_IDS.SLEEPING, detail: "sleeping2", score: main.score};
        }

        // WakeUp系: Sleeping中かつ ActionIndex != 0 かつ slept未設定 → ターンスキップ
        if (
            state.sleeping &&
            !state.slept &&
            modeRuleHasFile(mode, "wakeUp", main.file) &&
            sub.file !== "inori.png" &&
            main.score >= templateThreshold &&
            state.actionIndex !== 0 &&
            !state.actionTaken
        ) {
            return {kind: "action", actionId: ACTION_IDS.TURN_SKIPPED, detail: main.file, score: main.score};
        }

        // WakeUp系: Sleeping中かつ ActionIndex == 0 → 眠り回復（sleptは問わない）
        if (
            state.sleeping &&
            modeRuleHasFile(mode, "wakeUp", main.file) &&
            sub.file !== "inori.png" &&
            main.score >= templateThreshold &&
            state.actionIndex === 0 &&
            !state.actionTaken
        ) {
            return {kind: "action", actionId: ACTION_IDS.CURE_SLEEPING, detail: main.file, score: main.score};
        }

        // ためる
        if (main.file === "tameru.png" && main.score >= templateThreshold) {
            if (modeRuleHasFile(mode, "psycheUpTarget", target.file) && target.score >= templateThreshold && !state.actionTaken) {
                return {
                    kind: "action",
                    actionId: ACTION_IDS.PSYCHE_UP_ALLY,
                    detail: `tameru + ${target.file}`,
                    score: Math.min(main.score, target.score)
                };
            }
            return {kind: "action", actionId: ACTION_IDS.PSYCHE_UP, detail: "tameru", score: main.score};
        }

        // ano.png: action記録なし、状態リセットのみ
        if (main.file === "ano.png" && main.score >= templateThreshold) {
            return {kind: "ano"};
        }

        // DIRECT_MAIN_RULES
        const directAction = getModeRuleMap(mode, "directMainActions")[main.file];
        if (directAction && main.score >= templateThreshold) {
            return {kind: "action", actionId: directAction, detail: main.file, score: main.score};
        }

        return null;
    }

    function pickGilyumei1Candidate(matches) {
        const {templateThreshold} = getActiveThresholds();
        const main = matches.main || emptyMatch("main");
        const sub = matches.sub || emptyMatch("sub");
        const ally = matches.ally || emptyMatch("ally");
        const target = matches.target || emptyMatch("target");
        const mode = getActiveMode();
        const erugioMain = modeRuleHasFile(mode, "gilyumeiMain", main.file);
        // reset
        if (erugioMain && modeRuleHasFile(mode, "resetSub", sub.file) && main.score >= 0.6 && sub.score >= 0.6) {
            return {kind: "reset", score: Math.min(main.score, sub.score), detail: `${main.file} + ${sub.file}`};
        }

        if(modeRuleHasFile(mode, "kiriage", ally.file) && ally.score >= templateThreshold) {
            return {kind: "action", actionId: ACTION_IDS.UPWARD_SLICE, detail: ally.file, score: ally.score};
        }
        if(modeRuleHasFile(mode, "inactive", main.file) && main.score >= templateThreshold && !state.actionTaken) {
            return {kind: "action", actionId: ACTION_IDS.INACTIVE_ALLY, detail: main.file, score: main.score};
        }
        if(modeRuleHasFile(mode, "samidare", main.file) && main.score >= templateThreshold) {
            if(target.file === "gilyumei_target.png" && target.score >= templateThreshold) {
                return {kind: "action", actionId: ACTION_IDS.MULTISLASH, detail: main.file, score: main.score};
            }else if(target.file === "aha.png" && target.score >= templateThreshold) {
                return {kind: "action", actionId: ACTION_IDS.MULTITHRUST, detail: main.file, score: main.score};
            }
        }

        const directAction = getModeRuleMap(mode, "directMainActions")[main.file];
        if (directAction && main.score >= templateThreshold) {
            return {kind: "action", actionId: directAction, detail: main.file, score: main.score};
        }
    }

    function pickhexagoonCandidate(matches){
        const {templateThreshold} = getActiveThresholds();
        const main = matches.main || emptyMatch("main");
        const sub = matches.sub || emptyMatch("sub");
        const ally = matches.ally || emptyMatch("ally");
        const target = matches.target || emptyMatch("target");
        const mode = getActiveMode();
        const erugioMain = modeRuleHasFile(mode, "Main", main.file);
        // reset
        if (erugioMain && modeRuleHasFile(mode, "resetSub", sub.file) && main.score >= 0.6 && sub.score >= 0.6) {
            return {kind: "reset", score: Math.min(main.score, sub.score), detail: `${main.file} + ${sub.file}`};
        }

    }

    function pickCandidate(matches) {
        const mode = getActiveMode();
        const picker = mode?.picker || mode?.id || "identify";
        if (picker === "identify") {
            return pickIdentifyCandidate(matches);
        }
        if (picker === "erugiosu") {
            return pickErugiosuCandidate(matches);
        }
        if (picker === "gilyumei1") {
            return pickGilyumei1Candidate(matches);
        }
        if (picker === "hexagoon") {
            return pickhexagoonCandidate(matches);
        }
        return null;
    }

    function maybeResetFromCombo(candidate) {
        const {resetLatchClearScore} = getActiveThresholds();
        if (candidate && candidate.kind === "reset") {
            if (!state.resetLatched) {
                state.resetLatched = true;
                resetConsoleState();
            }
            return true;
        }
        if (!candidate || candidate.score < resetLatchClearScore) {
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
        state.pendingDamage1ConfirmUntil = 0;
        state.pendingDamage2ConfirmUntil = 0;
        state.maybeCritical = -1;
        state.lastModeHitAt = Date.now();
        // 追加
        state.actionTaken = false;
        state.sleeping = false;
        state.slept = false;
        state.daibougilyo = false;
        renderHistory();
        updateTurnChip();
        setBridgeStatus("visionBridgeReady", "");
    }

    function getModeById(modeId) {
        return state.modes.find((mode) => mode.id === modeId) || null;
    }

    function getModeLabel(mode) {
        if (!mode) {
            return "identify";
        }
        return mode.names?.[state.lang] || mode.names?.ja || mode.names?.en || mode.id;
    }

    function syncModeSelect() {
        if (!ui.modeSelect) {
            return;
        }
        ui.modeSelect.value = state.modeId;
    }

    function populateModeOptions() {
        if (!ui.modeSelect) {
            return;
        }
        ui.modeSelect.innerHTML = "";
        for (const mode of state.modes) {
            const option = document.createElement("option");
            option.value = mode.id;
            option.textContent = getModeLabel(mode);
            ui.modeSelect.appendChild(option);
        }
        syncModeSelect();
    }

    function syncModeBattleEmulator(mode) {
        if (!mode?.battleEmulator) {
            return false;
        }
        if (typeof window.selectVisionBattleEmulator !== "function") {
            return false;
        }
        return window.selectVisionBattleEmulator(mode.battleEmulator);
    }

    async function refreshTemplatesForActiveMode() {
        if (!state.matcher || !state.assetPack) {
            return;
        }
        const mode = getActiveMode();
        state.templatesBySlot = await loadTemplates(state.matcher, mode, state.assetPack);
    }

    async function setVisionMode(modeId, options = {}) {
        const {
            reset = true,
            syncEmulator = false
        } = options;
        const nextMode = getModeById(modeId) || getModeById("identify");
        if (!nextMode) {
            return;
        }

        state.modeId = nextMode.id;
        state.activeMode = nextMode;
        state.activeThresholds = resolveModeThresholds(nextMode);
        state.lastModeHitAt = Date.now();
        state.lastMatches = Object.create(null);
        syncModeSelect();
        if (syncEmulator) {
            syncModeBattleEmulator(nextMode);
        }
        if (reset) {
            resetConsoleState();
        }
        await refreshTemplatesForActiveMode();
        updateMatchCards(state.lastMatches);
    }

    function modeHasActivityMatch(mode, matches) {
        const {templateThreshold} = getActiveThresholds();
        if (!mode || mode.id === "identify") {
            return false;
        }

        for (const template of mode.identify?.templates || []) {
            const match = matches[template.slot] || emptyMatch(template.slot);
            if (match.file === template.file && match.score >= templateThreshold) {
                return true;
            }
        }

        return false;
    }

    function maybeReturnToIdentifyMode(matches) {
        const mode = getActiveMode();
        if (!mode || mode.id === "identify") {
            return false;
        }

        if (modeHasActivityMatch(mode, matches)) {
            state.lastModeHitAt = Date.now();
            return false;
        }

        const timeoutMs = Number.isFinite(mode.timeoutMs) ? mode.timeoutMs : 4 * 60 * 1000;
        if (Date.now() - state.lastModeHitAt < timeoutMs) {
            return false;
        }

        setVisionMode("identify", {reset: true, syncEmulator: false}).catch(() => {
        });
        return true;
    }

    function historyToMarkdown() {
        const rows = getTurnRows();
        if (!rows.length) return "";
        const header = "| Turn | Act 1 | Damage 1 | Act 2 | Damage 2 | Act 3 | Damage 3 |";
        const sep    = "| --- | --- | --- | --- | --- | --- | --- |";
        const lines = rows.map((row) => {
            const cells = row.slots.map((slot) => {
                const act = slot.actionId ? getActionLabel(slot.actionId) : "-";
                const dmg = typeof slot.damage === "number" && slot.damage >= 0 ? String(slot.damage) : slot.actionId ? "..." : "-";
                return `${act} | ${dmg}`;
            });
            return `| T${row.turn} | ${cells.join(" | ")} |`;
        });
        return [header, sep, ...lines].join("\n");
    }

    function historyToCsv() {
        const rows = getTurnRows();
        if (!rows.length) return "";
        const header = "Turn,Act 1,Damage 1,Act 2,Damage 2,Act 3,Damage 3";
        const lines = rows.map((row) => {
            const cells = row.slots.flatMap((slot) => {
                const act = slot.actionId ? getActionLabel(slot.actionId) : "";
                const dmg = typeof slot.damage === "number" && slot.damage >= 0 ? String(slot.damage) : slot.actionId ? "..." : "";
                return [act, dmg];
            });
            return [`T${row.turn}`, ...cells].join(",");
        });
        return [header, ...lines].join("\n");
    }

    function renderHistory() {
        ui.historyBody.innerHTML = "";
        const rows = getTurnRows();
        if (!rows.length) {
            if (ui.applyFormatButton) {
                ui.applyFormatButton.disabled = true;
            }
            ui.historyEmpty.hidden = false;
            ui.historyScroll.hidden = true;
            return;
        }
        if (ui.applyFormatButton) {
            ui.applyFormatButton.disabled = false;
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
        const el = ui.historyScroll;
        const isAtBottom = el.scrollHeight - el.scrollTop - el.clientHeight < 30;
        if (isAtBottom) {
            el.scrollTop = el.scrollHeight;
        }
    }

    function buildBattleActionFormat(historyEntries = state.history) {
        const enemyActions = [];
        const allyActions = [];
        const damages = [];
        historyEntries.forEach((entry) => {
            const action = ACTIONS_BY_ID[entry.actionId];
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
        return `${enemyActions.join(" ")}-${allyActions.join(" ")}-${damages.join(" ")}-`;
    }

    function buildConsoleCommand() {
        return `b 0 0 0 ${Math.max(0, state.turnIndex - 1)} ${buildBattleActionFormat()}`;
    }

    function getDamageChannel(actionId) {
        if (actionId === ACTION_IDS.MULTISLASH || actionId === ACTION_IDS.ULTRA_HIGH_SPEED_COMBO || actionId === ACTION_IDS.MULTITHRUST) {
            return 2;
        }
        if ([
            ACTION_IDS.ATTACK_ENEMY,
            ACTION_IDS.LIGHTNING_STORM,
            ACTION_IDS.SKY_ATTACK,
            ACTION_IDS.MERA_ZOMA,
            ACTION_IDS.FREEZING_BLIZZARD,
            ACTION_IDS.DARK_BREATH,
            ACTION_IDS.ATTACK_ALLY,
            ACTION_IDS.MAGIC_BURST,
            ACTION_IDS.MERCURIAL_THRUST,
            ACTION_IDS.UPWARD_SLICE,
            ACTION_IDS.FLAME_SLASH,
            ACTION_IDS.KACRACKLE_SLASH,
            ACTION_IDS.HATCHET_MAN,
        ].includes(actionId)) {
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

        // C#のUpdateDamage内の処理を移植:
        // 攻撃系ダメージ確定時、Act3(slotIndex==2)またはActionTaken済みならSleepingを解除
        if (damage > 0 && state.sleeping) {
            const entry = state.history.find(
                (item) => item.turn === turn + 1 && item.slot === slotIndex + 1
            );
            if (entry) {
                const sleepBreakActions = [
                    ACTION_IDS.ATTACK_ENEMY,
                    ACTION_IDS.CRITICAL_ATTACK,
                    ACTION_IDS.LIGHTNING_STORM,
                    ACTION_IDS.ULTRA_HIGH_SPEED_COMBO,
                    ACTION_IDS.SKY_ATTACK
                ];
                if (sleepBreakActions.includes(entry.actionId)) {
                    if (state.actionTaken || slotIndex === 2) {
                        state.sleeping = false;
                    }
                }
            }
        }
    }

    function clearPendingDamageConfirmation(key) {
        if (key === "damage1") {
            state.pendingDamage1 = -1;
            state.pendingDamage1Enabled = false;
            state.pendingDamage1ConfirmUntil = 0;
        } else if (key === "damage2") {
            state.pendingDamage2 = -1;
            state.pendingDamage2Enabled = false;
            state.pendingDamage2ConfirmUntil = 0;
        }
    }

    function handlePendingDamages(matches, damageReadings, nextActionDetected) {
        const {templateThreshold} = getActiveThresholds();
        const now = Date.now();
        const main = matches.main || emptyMatch("main");
        const needsDamage1 = state.pendingDamage1 !== -1 && state.pendingDamage1Enabled;
        const needsDamage2 = state.pendingDamage2 !== -1 && state.pendingDamage2Enabled;
        const confirmingDamage1 = state.pendingDamage1 !== -1 && !state.pendingDamage1Enabled && now < state.pendingDamage1ConfirmUntil;
        const confirmingDamage2 = state.pendingDamage2 !== -1 && !state.pendingDamage2Enabled && now < state.pendingDamage2ConfirmUntil;
        const candidateDamage1 = (needsDamage1 || confirmingDamage1) ? (damageReadings.damage1?.value ?? -1) : -1;
        const candidateDamage2 = (needsDamage2 || confirmingDamage2) ? (damageReadings.damage2?.value ?? -1) : -1;

        // maybeCritical: critical.png検出時に攻撃(敵)→痛恨(6)に上書き
        if (state.maybeCritical !== -1) {
            if (main.file === "critical.png" && main.score >= templateThreshold) {
                const turn = (state.maybeCritical & 0xfff) + 1;
                const slotIndex = (state.maybeCritical >> 12) & 0xf;
                updateHistoryDamage(turn, slotIndex, -1); // -2は「action上書き」シグナル用
                // 実際はダメージではなくactionIdを上書きする必要があるため別途処理
                const entry = state.history.find((item) => item.turn === turn && item.slot === slotIndex + 1);
                if (entry) {
                    entry.actionId = ACTION_IDS.CRITICAL_ATTACK;
                    renderHistory();
                }
                state.maybeCritical = -1;
            }
        }

        if (needsDamage1) {
            if (["guard.png", "miss.png", "miss2.png", "mikawasi.png"].includes(main.file) && main.score >= templateThreshold) {
                state.pendingDamage1Enabled = false;
                state.maybeCritical = -1;
                resolvePendingDamage(state.pendingDamage1, 0);
                state.preAction = -1;
                return true;
            }
            if (candidateDamage1 !== -1) {
                state.lastDamage1 = candidateDamage1;
                state.pendingDamage1Enabled = false;
                state.pendingDamage1ConfirmUntil = now + DAMAGE_CONFIRMATION_MS;
                resolvePendingDamage(state.pendingDamage1, candidateDamage1);
                state.preAction = -1;
                return true;
            }
        } else if (confirmingDamage1) {
            if (candidateDamage1 !== -1 && state.lastDamage1 < candidateDamage1) {
                state.lastDamage1 = candidateDamage1;
                resolvePendingDamage(state.pendingDamage1, candidateDamage1);
            }
            if (nextActionDetected || now >= state.pendingDamage1ConfirmUntil) {
                clearPendingDamageConfirmation("damage1");
                return false;
            }
            return true;
        } else if (state.pendingDamage1 !== -1 && !state.pendingDamage1Enabled) {
            clearPendingDamageConfirmation("damage1");
        } else if (needsDamage2) {
            if (["guard.png", "miss.png", "miss2.png", "mikawasi.png"].includes(main.file) && main.score >= templateThreshold) {
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
                state.pendingDamage2ConfirmUntil = now + DAMAGE_CONFIRMATION_MS;
                resolvePendingDamage(state.pendingDamage2, candidateDamage2);
                state.preAction = -1;
                return true;
            }
        } else if (confirmingDamage2) {
            if (candidateDamage2 !== -1 && state.lastDamage2 < candidateDamage2) {
                state.lastDamage2 = candidateDamage2;
                resolvePendingDamage(state.pendingDamage2, candidateDamage2);
            }
            if (nextActionDetected || now >= state.pendingDamage2ConfirmUntil) {
                clearPendingDamageConfirmation("damage2");
                return false;
            }
            return true;
        } else if (state.pendingDamage2 !== -1 && !state.pendingDamage2Enabled) {
            clearPendingDamageConfirmation("damage2");
        }
        return false;
    }

    function acceptCandidate(candidate, matches) {
        const {templateThreshold} = getActiveThresholds();
        // ano.png: preActionリセット、actionTaken=true、slept=false のみ
        if (candidate && candidate.kind === "ano") {
            state.preAction = -1;
            state.actionTaken = true;
            state.slept = false;
            return;
        }

        if (!candidate || candidate.kind !== "action") {
            const now = Date.now();
            const elapsed = now - state.lastDetectionAt;
            // C#移植: (currentTime - LastDetection) > 閾値 && lastHit1 == ""
            // lastHit1 == "" 相当 = mainスロットでテンプレートが閾値未満（未検出）
            const mainMatch = matches && matches.main ? matches.main : null;
            const noTemplateDetected = !mainMatch || mainMatch.score < templateThreshold;
            if (elapsed > 3000 && noTemplateDetected) {
                state.preAction = -1;
            }
            // C#移植: 一定時間行動未検出でsleptをリセット（C#は4秒、ここでは3秒に統一）
            if (elapsed > 3000) {
                state.slept = false;
            }
            // C#: else節でActionIndex==0ならActionTakenリセット
            if (state.actionIndex === 0) {
                state.actionTaken = false;
            }
            return;
        }

        if (candidate.actionId === state.preAction) {
            // 同一アクション検知中はタイマーをリセットしない
            // → nullが3秒続いたときだけpreActionがクリアされる
            // C#: action==preActionのとき ActionIndex==0ならActionTakenリセット
            if (state.actionIndex === 0) {
                state.actionTaken = false;
            }
            return;
        }

        state.preAction = candidate.actionId;
        state.lastDetectionAt = Date.now();
        state.lastModeHitAt = state.lastDetectionAt;

        const pendingSlotRef = (state.actionIndex << 12) | (state.turnIndex - 1);
        const damageChannel = getDamageChannel(candidate.actionId);
        state.pendingDamage1 = damageChannel === 1 ? pendingSlotRef : -1;
        state.pendingDamage2 = damageChannel === 2 ? pendingSlotRef : -1;
        state.pendingDamage1Enabled = damageChannel === 1;
        state.pendingDamage2Enabled = damageChannel === 2;
        state.lastDamage1 = -1;
        state.lastDamage2 = -1;
        state.pendingDamage1ConfirmUntil = 0;
        state.pendingDamage2ConfirmUntil = 0;

        // maybeCritical: 攻撃(敵)のとき記録
        if (candidate.actionId === ACTION_IDS.ATTACK_ENEMY) {
            state.maybeCritical = pendingSlotRef;
        } else if (!candidate.detail?.includes("attack.png")) {
            state.maybeCritical = -1;
        }

        // daibougilyo: マダンテかつActionTaken未設定のとき設定
        if (candidate.actionId === ACTION_IDS.MAGIC_BURST && !state.actionTaken) {
            state.daibougilyo = true;
        }
        // 大防御確定でdaibougilyoリセット
        if (candidate.actionId === ACTION_IDS.DEFENDING_CHAMPION) {
            state.daibougilyo = false;
        }

        // Sleeping状態の設定
        if (candidate.actionId === ACTION_IDS.LULLAB_EYE) { // あやしいひとみ
            state.sleeping = true;
        }
        if (candidate.actionId === ACTION_IDS.SLEEPING) { // 眠っている！
            state.sleeping = true;
            state.slept = true;
        }
        if (
            candidate.actionId === ACTION_IDS.CURE_SLEEPING ||
            candidate.actionId === ACTION_IDS.TURN_SKIPPED
        ) { // 眠り回復/ターンスキップ
            state.sleeping = false;
            state.slept = true;
        }

        // 麻痺系slept
        if (
            candidate.actionId === ACTION_IDS.PARALYSIS ||
            candidate.actionId === ACTION_IDS.CURE_PARALYSIS
        ) {
            state.slept = true;
        }

        // ActionTaken設定が必要なaction
        const setsActionTaken = [
            ACTION_IDS.BUFF,
            ACTION_IDS.MAGIC_MIRROR,
            ACTION_IDS.DOUBLE_UP,
            ACTION_IDS.MULTITHRUST,
            ACTION_IDS.FULLHEAL,
            ACTION_IDS.CURE_SLEEPING,
            ACTION_IDS.TURN_SKIPPED,
            ACTION_IDS.SAGE_ELIXIR,
            ACTION_IDS.ELFIN_ELIXIR,
            ACTION_IDS.MAGIC_WATER,
            ACTION_IDS.SPECIAL_MEDICINE,
            ACTION_IDS.SONG,
            ACTION_IDS.FLEE,
            ACTION_IDS.PARALYSIS,
            ACTION_IDS.CURE_PARALYSIS,
            ACTION_IDS.ATTACK_ALLY,
            ACTION_IDS.SLEEPING,
            ACTION_IDS.DEAD
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
        const embeddedPack = getEmbeddedVisionAssetPack();
        if (embeddedPack) {
            return normalizeVisionAssetPack(embeddedPack);
        }
        const response = await fetch(VISION_ASSET_PACK_URL, {cache: "no-store"});
        if (!response.ok) {
            throw new Error(`asset pack missing: ${VISION_ASSET_PACK_URL}`);
        }
        return normalizeVisionAssetPack(await response.json());
    }

    function decodePackedMask(entry, assetPack) {
        const encoded = typeof entry.mask === "string" ? entry.mask : assetPack?.assets?.[entry.maskId];
        if (typeof encoded !== "string") {
            throw new Error(`asset mask missing: ${entry.file || entry.maskId || "unknown"}`);
        }
        return decodeBase64Bytes(encoded);
    }

    function normalizePackedTemplate(entry, assetPack) {
        return {
            slot: entry.slot,
            file: entry.file,
            width: entry.width,
            height: entry.height,
            maskBytes: decodePackedMask(entry, assetPack)
        };
    }

    async function loadTemplates(matcher, mode, assetPack) {
        const templatesBySlot = new Map();
        for (const entry of mode?.templates || []) {
            const template = await matcher.createTemplate(normalizePackedTemplate(entry, assetPack));
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
                mask: decodePackedMask(entry, assetPack)
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
            state.templatesBySlot = await loadTemplates(matcher, getActiveMode(), state.assetPack);
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
            if (!state.assetPackPromise) {
                state.assetPackPromise = loadPackedVisionAssets();
            }
            if (!state.assetPack) {
                state.assetPack = await state.assetPackPromise;
            }
            if (!state.matcher) {
                state.matcher = await createMatcher({
                    warnOnCpuFallback: true,
                    onWebGpuLost: recoverWebGpuMatcher
                });
            }
            state.templatesBySlot = await loadTemplates(state.matcher, getActiveMode(), state.assetPack);
            state.numberTemplates = loadNumberTemplates(state.assetPack);
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
            const {actionThreshold} = getActiveThresholds();
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
                    const damageReadings = recognizePendingDamageValues();
                    updateMatchCards(matches);
                    drawOverlay(matches, damageReadings);
                    if (maybeReturnToIdentifyMode(matches)) {
                        updateFps(now);
                        queueLoop(runFrame);
                        return;
                    }
                    const candidate = pickCandidate(matches);
                    const nextActionDetected = candidate && candidate.kind === "action" && candidate.score >= actionThreshold;
                    if (handlePendingDamages(matches, damageReadings, nextActionDetected)) {
                        updateFps(now);
                        queueLoop(runFrame);
                        return;
                    }
                    if (candidate && candidate.score < actionThreshold) {
                        updateFps(now);
                        queueLoop(runFrame);
                        return;
                    }
                    if (candidate && candidate.kind === "mode") {
                        await setVisionMode(candidate.modeId, {reset: true, syncEmulator: true});
                        updateFps(now);
                        queueLoop(runFrame);
                        return;
                    }
                    if (!maybeResetFromCombo(candidate)) {
                        acceptCandidate(candidate, matches);
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
        populateModeOptions();
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
        ui.modeSelect?.addEventListener("change", (event) => {
            setVisionMode(event.target.value, {reset: true, syncEmulator: true}).catch(() => {
            });
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
        ui.copyMarkdown?.addEventListener("click", (event) => {
            event.preventDefault();
            if (!state.history.length) return;
            const text = historyToMarkdown();
            if (typeof copyText === "function") copyText(text);
        });
        ui.copyCsv?.addEventListener("click", (event) => {
            event.preventDefault();
            if (!state.history.length) return;
            const text = historyToCsv();
            if (typeof copyText === "function") copyText(text);
        });
        ui.applyFormatButton?.addEventListener("click", () => {
            const formatText = buildBattleActionFormat();
            if (!formatText.trim()) {
                return;
            }
            if (typeof window.applyVisionBattleFormat === "function") {
                window.applyVisionBattleFormat(formatText, {
                    battleEmulator: getActiveMode()?.battleEmulator || null
                });
            }
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
        try {
            state.assetPackPromise = loadPackedVisionAssets();
            state.assetPack = await state.assetPackPromise;
            state.modes = Array.isArray(state.assetPack.modes) ? state.assetPack.modes : [];
        } catch (error) {
            console.warn("vision asset pack load failed, using legacy fallback:", error);
            state.assetPack = normalizeVisionAssetPack(null);
            state.modes = state.assetPack.modes;
        }
        state.activeMode = getModeById(state.modeId) || state.modes[0] || null;
        state.activeThresholds = resolveModeThresholds(state.activeMode);
        state.lastModeHitAt = Date.now();
        populateModeOptions();
        updateTurnChip();
        renderHistory();
        if (ui.applyFormatButton) {
            ui.applyFormatButton.disabled = true;
        }
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
