(() => {
    "use strict";

    const ui = {
        panel: document.getElementById("whiteStartPanel"),
        enabled: document.getElementById("whiteStartEnabled"),
        debug: document.getElementById("whiteStartDebug"),
        status: document.getElementById("whiteStartStatus"),
        detail: document.getElementById("whiteStartDetail")
    };
    if (!ui.panel || !ui.enabled || !ui.debug || !window.battleAutoTimer) return;

    const ENABLED_KEY = "dq9WhiteScreenTimerEnabled";
    const DEBUG_KEY = "dq9WhiteScreenTimerDebug";
    const TEXT = {
        ja: {
            title: "白画面でタイマーを自動開始",
            description: "",
            enabled: "白画面で自動開始",
            debug: "停止中も通常の映像認識を使う（デバッグ）",
            help: "",
            idle: ["接続待ち", ""],
            off: ["OFF", "白画面からの自動開始は無効です。映像認識は通常どおり動作します。"],
            debugMode: ["デバッグ中", "タイマー停止中も通常の映像認識を使います。白画面は検知しません。"],
            running: ["タイマー動作中", "白画面の検知を停止し、通常の映像認識を使っています。"],
            waiting: ["映像待ち", "カメラから新しい映像が届くと、白画面の検知を再開します。"],
            watching: ["動作中", "白画面を検知しています…"],
            timestamp: ["撮影時刻を取得できません", "この映像では実際の撮影時刻を取得できないため、自動開始できません。タイマー開始ボタンを使ってください。"],
            unsupported: ["フレーム取得に未対応", "このブラウザでは撮影時刻付きの映像を取得できません。タイマー開始ボタンを使ってください。"],
            size: ["認識枠を確認してください", "映像が認識枠より小さいため、枠全体を確認できません。カメラの出力サイズを確認してください。"],
            error: ["映像を確認できません", "カメラを接続し直してください。タイマー開始ボタンからも開始できます。"]
        },
        en: {
            title: "Start timer on a white screen",
            description: "",
            enabled: "Start automatically on white",
            debug: "Use normal recognition while stopped (debug)",
            help: "",
            idle: ["Waiting for connection", ""],
            off: ["OFF", "White-screen auto-start is disabled. Normal recognition is available."],
            debugMode: ["Debug", "Normal recognition runs while the timer is stopped. White detection is disabled."],
            running: ["Timer running", "White detection is stopped and normal recognition is in use."],
            waiting: ["Waiting for video", "White detection resumes when new camera frames arrive."],
            watching: ["Active", "Detecting a white screen…"],
            timestamp: ["Capture time unavailable", "This feed does not provide the actual capture time, so auto-start is unavailable. Use Start Timer."],
            unsupported: ["Frame capture unsupported", "This browser cannot provide video frames with capture times. Use Start Timer."],
            size: ["Check recognition area", "The video is smaller than the recognition area. Check the camera output size."],
            error: ["Cannot inspect video", "Reconnect the camera, or use Start Timer to start manually."]
        }
    };

    const RING_FRAME_COUNT = 60;
    const PROBE_INTERVAL_MS = 200;
    const RESULT_BYTES = 16;

    // Separate device and buffers: never borrow or mutate the vision matcher's resources.
    class WhiteFrameDetector {
        constructor() {
            this.device = null;
            this.pipeline = null;
            this.output = null;
            this.readback = null;
            this.params = null;
            this.ringTexture = null;
            this.ringView = null;
            this.ringWidth = 0;
            this.ringHeight = 0;
            this.preparePromise = null;
            this.unavailable = false;
        }

        async prepare() {
            if (this.device) return true;
            if (this.unavailable) return false;
            if (this.preparePromise) return this.preparePromise;
            this.preparePromise = this.initialize().finally(() => {
                this.preparePromise = null;
            });
            return this.preparePromise;
        }

        async initialize() {
            if (!navigator.gpu) {
                this.unavailable = true;
                return false;
            }
            let device = null;
            try {
                const adapter = await navigator.gpu.requestAdapter();
                if (!adapter) {
                    this.unavailable = true;
                    return false;
                }
                device = await adapter.requestDevice();
                let lost = false;
                device.lost.then((info) => {
                    lost = true;
                    if (this.device === device) this.releaseGpu();
                    if (info.reason !== "destroyed") {
                        console.warn("White detector WebGPU device lost:", info);
                    }
                });
                const module = device.createShaderModule({code: `
struct Params {
    size: vec2<u32>,
    startLayer: u32,
    count: u32,
}

struct FrameStats {
    below96: atomic<u32>,
    below112: atomic<u32>,
    below120: atomic<u32>,
    spread12: atomic<u32>,
}

struct Stats {
    frames: array<FrameStats, 60>,
}

@group(0) @binding(0) var frame: texture_2d_array<f32>;
@group(0) @binding(1) var<storage, read_write> stats: Stats;
@group(0) @binding(2) var<uniform> params: Params;
var<workgroup> tileBelow96: atomic<u32>;
var<workgroup> tileBelow112: atomic<u32>;
var<workgroup> tileBelow120: atomic<u32>;
var<workgroup> tileSpread12: atomic<u32>;

// The camera feed has passed through a lossy encoder. The supplied white-frame
// sample contains neutral RGB values as low as 122, so byte-exact 255 is not a
// valid definition of white here.
const WHITE_MIN_CHANNEL: f32 = 0.47058824; // 120 / 255
const WHITE_MAX_CHANNEL_SPREAD: f32 = 0.047058824; // 12 / 255

@compute @workgroup_size(16, 16)
fn main(@builtin(global_invocation_id) pixel: vec3<u32>,
        @builtin(local_invocation_index) localIndex: u32) {
    if (localIndex == 0u) {
        atomicStore(&tileBelow96, 0u);
        atomicStore(&tileBelow112, 0u);
        atomicStore(&tileBelow120, 0u);
        atomicStore(&tileSpread12, 0u);
    }
    workgroupBarrier();
    if (pixel.x < params.size.x && pixel.y < params.size.y) {
        let layer = (params.startLayer + pixel.z) % 60u;
        let rgba = textureLoad(frame, vec2<i32>(i32(pixel.x), i32(pixel.y)), i32(layer), 0);
        let low = min(rgba.r, min(rgba.g, rgba.b));
        let high = max(rgba.r, max(rgba.g, rgba.b));
        if (low < (96.0 / 255.0)) { atomicAdd(&tileBelow96, 1u); }
        if (low < (112.0 / 255.0)) { atomicAdd(&tileBelow112, 1u); }
        if (low < WHITE_MIN_CHANNEL) { atomicAdd(&tileBelow120, 1u); }
        if (high - low > WHITE_MAX_CHANNEL_SPREAD) { atomicAdd(&tileSpread12, 1u); }
    }
    workgroupBarrier();
    if (localIndex == 0u) {
        atomicAdd(&stats.frames[pixel.z].below96, atomicLoad(&tileBelow96));
        atomicAdd(&stats.frames[pixel.z].below112, atomicLoad(&tileBelow112));
        atomicAdd(&stats.frames[pixel.z].below120, atomicLoad(&tileBelow120));
        atomicAdd(&stats.frames[pixel.z].spread12, atomicLoad(&tileSpread12));
    }
}`});
                const pipeline = await device.createComputePipelineAsync({
                    layout: "auto", compute: {module, entryPoint: "main"}
                });
                if (lost) throw new Error("White detector GPU lost during initialization");
                const output = device.createBuffer({
                    size: RESULT_BYTES * RING_FRAME_COUNT,
                    usage: GPUBufferUsage.STORAGE | GPUBufferUsage.COPY_SRC | GPUBufferUsage.COPY_DST
                });
                const readback = device.createBuffer({
                    size: RESULT_BYTES * RING_FRAME_COUNT,
                    usage: GPUBufferUsage.COPY_DST | GPUBufferUsage.MAP_READ
                });
                const params = device.createBuffer({
                    size: 16,
                    usage: GPUBufferUsage.UNIFORM | GPUBufferUsage.COPY_DST
                });
                this.pipeline = pipeline;
                this.output = output;
                this.readback = readback;
                this.params = params;
                this.device = device;
                return true;
            } catch (error) {
                device?.destroy();
                console.warn("White detector WebGPU initialization failed:", error);
                return false;
            }
        }

        releaseGpu() {
            const device = this.device;
            this.device = null;
            this.pipeline = null;
            this.output?.destroy();
            this.readback?.destroy();
            this.params?.destroy();
            this.ringTexture?.destroy();
            this.output = null;
            this.readback = null;
            this.params = null;
            this.ringTexture = null;
            this.ringView = null;
            this.ringWidth = 0;
            this.ringHeight = 0;
            device?.destroy();
        }

        ensureRing(width, height) {
            const device = this.device;
            if (!device) {
                throw new Error("White detector WebGPU is not ready");
            }
            if (this.ringTexture && this.ringWidth === width && this.ringHeight === height) return;
            this.ringTexture?.destroy();
            this.ringTexture = device.createTexture({
                size: {width, height, depthOrArrayLayers: RING_FRAME_COUNT},
                format: "rgba8unorm",
                usage: GPUTextureUsage.COPY_DST | GPUTextureUsage.TEXTURE_BINDING | GPUTextureUsage.RENDER_ATTACHMENT
            });
            this.ringView = this.ringTexture.createView({
                dimension: "2d-array",
                baseArrayLayer: 0,
                arrayLayerCount: RING_FRAME_COUNT
            });
            this.ringWidth = width;
            this.ringHeight = height;
        }

        capture(video, rect, layer) {
            const width = rect.sourceCropWidth;
            const height = rect.sourceCropHeight;
            const device = this.device;
            if (!device) {
                throw new Error("White detector WebGPU is not ready");
            }
            this.ensureRing(width, height);
            // WebGPU captures the external source data when this call is issued.
            // Keep this inside requestVideoFrameCallback so captureTime and pixels
            // refer to the same browser-provided frame. No CPU pixel readback occurs.
            device.queue.copyExternalImageToTexture(
                {
                    source: video,
                    origin: {x: rect.sourceX, y: rect.sourceY}
                },
                {
                    texture: this.ringTexture,
                    origin: {x: 0, y: 0, z: layer},
                    colorSpace: "srgb",
                    premultipliedAlpha: false
                },
                {width, height, depthOrArrayLayers: 1}
            );
        }

        async inspect(startLayer, count) {
            const device = this.device;
            if (!device || !this.ringTexture || !this.ringView) {
                throw new Error("White detector GPU ring is not ready");
            }
            if (!Number.isInteger(startLayer) || startLayer < 0 || startLayer >= RING_FRAME_COUNT
                || !Number.isInteger(count) || count <= 0 || count > RING_FRAME_COUNT) {
                throw new Error("Invalid white detector inspection range");
            }
            const byteLength = count * RESULT_BYTES;
            const readback = this.readback;
            try {
                device.queue.writeBuffer(this.params, 0, new Uint32Array([
                    this.ringWidth, this.ringHeight, startLayer, count
                ]));
                const bindGroup = device.createBindGroup({
                    layout: this.pipeline.getBindGroupLayout(0),
                    entries: [
                        {binding: 0, resource: this.ringView},
                        {binding: 1, resource: {buffer: this.output}},
                        {binding: 2, resource: {buffer: this.params}}
                    ]
                });
                const encoder = device.createCommandEncoder();
                encoder.clearBuffer(this.output, 0, byteLength);
                const pass = encoder.beginComputePass();
                pass.setPipeline(this.pipeline);
                pass.setBindGroup(0, bindGroup);
                pass.dispatchWorkgroups(
                    Math.ceil(this.ringWidth / 16),
                    Math.ceil(this.ringHeight / 16),
                    count
                );
                pass.end();
                encoder.copyBufferToBuffer(this.output, 0, readback, 0, byteLength);
                device.queue.submit([encoder.finish()]);
                await readback.mapAsync(GPUMapMode.READ, 0, byteLength);
                return new Uint32Array(readback.getMappedRange(0, byteLength)).slice();
            } finally {
                if (readback?.mapState === "mapped") readback.unmap();
            }
        }

        static isWhite(values, index = 0) {
            const base = index * 4;
            return values[base + 2] === 0 && values[base + 3] === 0;
        }
    }

    const detector = new WhiteFrameDetector();
    const timer = window.battleAutoTimer;
    let source = null;
    let stream = null;
    let track = null;
    let active = false;
    let frameHandle = null;
    let generation = 0;
    let lastFrameTime = null;
    let lastProbeFrameTime = null;
    let frameSequence = 0;
    let lastConfirmedNonWhiteSequence = null;
    let inspectionBusy = false;
    let resolvingWhite = false;
    const captureTimes = new Float64Array(RING_FRAME_COUNT);
    const slotSequences = new Array(RING_FRAME_COUNT).fill(-1);
    let frameStatus = "watching";
    let pageActive = true;

    function resetFrameHistory() {
        lastFrameTime = null;
        lastProbeFrameTime = null;
        frameSequence = 0;
        lastConfirmedNonWhiteSequence = null;
        resolvingWhite = false;
        captureTimes.fill(0);
        slotSequences.fill(-1);
    }

    function restoreSettings() {
        try {
            ui.enabled.checked = localStorage.getItem(ENABLED_KEY) !== "0";
            ui.debug.checked = localStorage.getItem(DEBUG_KEY) === "1";
        } catch (error) {
            console.warn("White detector settings unavailable:", error);
        }
    }

    function wantsDetection() {
        return Boolean(source?.isActive() && ui.enabled.checked && !ui.debug.checked && !timer.isRunning());
    }

    function statusKey() {
        if (!ui.enabled.checked) return "off";
        if (ui.debug.checked) return "debugMode";
        if (timer.isRunning()) return "running";
        if (!source?.isActive()) return "idle";
        if (typeof source.video.requestVideoFrameCallback !== "function") return "unsupported";
        return active ? frameStatus : "waiting";
    }

    function render() {
        const strings = TEXT[document.documentElement.dataset.lang] || TEXT.ja;
        const key = statusKey();
        const [status, detail] = strings[key];
        if (ui.status.textContent !== status) ui.status.textContent = status;
        if (ui.detail.textContent !== detail) ui.detail.textContent = detail;
        ui.status.classList.toggle("chip-accent", key === "watching");
        ui.panel.dataset.state = key;
    }

    function syncLanguage() {
        const strings = TEXT[document.documentElement.dataset.lang] || TEXT.ja;
        ui.panel.querySelectorAll("[data-white-start-text]").forEach((element) => {
            element.textContent = strings[element.dataset.whiteStartText];
        });
        render();
    }

    const onTrackChange = () => sync(true);

    function sync(invalidate = false) {
        const nextStream = source?.video.srcObject || null;
        const nextTrack = nextStream?.getVideoTracks()[0] || null;
        const changed = nextStream !== stream || nextTrack !== track;
        if (changed) {
            for (const event of ["ended", "mute", "unmute"]) {
                track?.removeEventListener(event, onTrackChange);
                nextTrack?.addEventListener(event, onTrackChange);
            }
            stream = nextStream;
            track = nextTrack;
        }
        const video = source?.video;
        const nextActive = Boolean(pageActive && wantsDetection() && track?.readyState === "live" && !track.muted
            && !video.paused && video.readyState >= 2 && typeof video.requestVideoFrameCallback === "function");
        if (invalidate || changed || active !== nextActive) {
            generation += 1;
            if (frameHandle !== null) video.cancelVideoFrameCallback(frameHandle);
            frameHandle = null;
            active = nextActive;
            resetFrameHistory();
            frameStatus = "watching";
        }
        if (active && frameHandle === null) {
            void detector.prepare().then((ready) => {
                if (!ready && active && wantsDetection()) {
                    frameStatus = "error";
                    render();
                }
            });
            frameHandle = video.requestVideoFrameCallback(onFrame);
        }
        render();
    }

    function onFrame(now, metadata) {
        frameHandle = null;
        sync();
        if (!active) return;
        const captureTime = metadata.captureTime;
        // mediaTime is only a sampling clock. Never substitute it or callback/display time for captureTime.
        if (!Number.isFinite(captureTime) || captureTime < 0 || captureTime > performance.now()) {
            frameStatus = "timestamp";
            render();
            return;
        }
        const frameTime = Number.isFinite(metadata.mediaTime) ? metadata.mediaTime * 1000 : captureTime;
        if (lastFrameTime !== null && frameTime < lastFrameTime) {
            sync(true);
            return;
        }
        lastFrameTime = frameTime;
        if (resolvingWhite) return;
        const rect = source.getRect();
        const video = source.video;
        if (![rect.sourceX, rect.sourceY, rect.sourceCropWidth, rect.sourceCropHeight].every(Number.isInteger)
            || rect.sourceX < 0 || rect.sourceY < 0 || rect.sourceCropWidth <= 0 || rect.sourceCropHeight <= 0
            || rect.sourceX + rect.sourceCropWidth > video.videoWidth
            || rect.sourceY + rect.sourceCropHeight > video.videoHeight) {
            frameStatus = "size";
            render();
            return;
        }
        frameStatus = "watching";
        render();
        if (!detector.device) return;
        const sequence = frameSequence++;
        const slot = sequence % RING_FRAME_COUNT;
        try {
            // Copy every browser video frame into GPU memory. CPU keeps only
            // captureTime and the sequence number for each ring slot.
            detector.capture(video, rect, slot);
        } catch (error) {
            frameStatus = "error";
            render();
            console.warn("White frame capture failed:", error);
            return;
        }
        captureTimes[slot] = captureTime;
        slotSequences[slot] = sequence;
        if (lastProbeFrameTime !== null && frameTime - lastProbeFrameTime < PROBE_INTERVAL_MS) return;
        if (inspectionBusy) return;
        lastProbeFrameTime = frameTime;
        const token = generation;
        const capturedStream = stream;
        const capturedTrack = track;
        inspectionBusy = true;
        const resultIsCurrent = () => {
            if (token !== generation || capturedStream !== video.srcObject || !active || !wantsDetection()) return false;
            if (video.paused || capturedTrack.readyState !== "live" || capturedTrack.muted
                || capturedStream.getVideoTracks()[0] !== capturedTrack) return false;
            return true;
        };
        detector.inspect(slot, 1).then(async (probeValues) => {
            if (!resultIsCurrent()) return;
            if (new URLSearchParams(location.search).has("whiteDebug")) {
                document.title = `WDBG ${rect.sourceCropWidth}x${rect.sourceCropHeight} `
                    + `seq=${sequence} b96=${probeValues[0]} b112=${probeValues[1]} `
                    + `b120=${probeValues[2]} s12=${probeValues[3]}`;
            }
            if (!WhiteFrameDetector.isWhite(probeValues)) {
                lastConfirmedNonWhiteSequence = sequence;
                return;
            }
            resolvingWhite = true;
            const oldestSequence = Math.max(0, frameSequence - RING_FRAME_COUNT);
            const startSequence = lastConfirmedNonWhiteSequence === null
                ? oldestSequence
                : lastConfirmedNonWhiteSequence + 1;
            if (startSequence < oldestSequence) {
                throw new Error("White transition fell outside the one-second GPU frame ring");
            }
            const count = sequence - startSequence + 1;
            const historyValues = await detector.inspect(startSequence % RING_FRAME_COUNT, count);
            if (!resultIsCurrent()) return;
            let firstWhiteOffset = -1;
            for (let index = 0; index < count; index += 1) {
                if (WhiteFrameDetector.isWhite(historyValues, index)) {
                    firstWhiteOffset = index;
                    break;
                }
            }
            if (firstWhiteOffset < 0) {
                throw new Error("White probe was not white during historical verification");
            }
            const firstWhiteSequence = startSequence + firstWhiteOffset;
            const firstWhiteSlot = firstWhiteSequence % RING_FRAME_COUNT;
            if (slotSequences[firstWhiteSlot] !== firstWhiteSequence) {
                throw new Error("First white frame was overwritten before verification");
            }
            const firstWhiteCaptureTime = captureTimes[firstWhiteSlot];
            if (!Number.isFinite(firstWhiteCaptureTime)) {
                throw new Error("First white frame captureTime is unavailable");
            }
            if (new URLSearchParams(location.search).has("whiteDebug")) {
                document.title = `WDBG first=${firstWhiteSequence} probe=${sequence} `
                    + `dt=${(captureTime - firstWhiteCaptureTime).toFixed(3)}ms`;
            }
            timer.startFromCapture(firstWhiteCaptureTime);
        }).catch((error) => {
            if (token === generation) {
                frameStatus = "error";
                render();
            }
            console.warn("White frame inspection failed:", error);
        }).finally(() => {
            inspectionBusy = false;
            if (token === generation && !timer.isRunning()) resolvingWhite = false;
        });
    }

    window.whiteScreenTimer = Object.freeze({
        attach(captureSource) {
            source = captureSource;
            for (const event of ["playing", "pause", "emptied", "loadeddata", "resize", "error"]) {
                source.video.addEventListener(event, () => sync(true));
            }
            sync(true);
        },
        sync,
        shouldSuspendRecognition: wantsDetection
    });
    for (const [toggle, key] of [[ui.enabled, ENABLED_KEY], [ui.debug, DEBUG_KEY]]) {
        toggle.addEventListener("change", () => {
            try {
                localStorage.setItem(key, toggle.checked ? "1" : "0");
            } catch (error) {
                console.warn("White detector setting was not saved:", error);
            }
            sync(true);
        });
    }
    window.addEventListener("battle-auto-timer-change", () => {
        const running = timer.isRunning();
        sync(true);
        if (running) {
            detector.releaseGpu();
        }
    });
    window.addEventListener("storage", (event) => {
        if (event.key === null || event.key === ENABLED_KEY || event.key === DEBUG_KEY) {
            restoreSettings();
            sync(true);
        }
    });
    window.addEventListener("pagehide", () => {
        pageActive = false;
        sync(true);
        detector.releaseGpu();
    });
    window.addEventListener("pageshow", () => {
        pageActive = true;
        sync(true);
    });
    new MutationObserver(syncLanguage).observe(document.documentElement, {attributes: true, attributeFilter: ["data-lang"]});
    restoreSettings();
    syncLanguage();
})();
