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
            description: "映像認識パネルでカメラを接続してください。タイマー停止中に認識枠全体が完全に白くなると、そのフレームを撮影した時刻からタイマーを開始します。",
            enabled: "白画面で自動開始",
            debug: "停止中も通常の映像認識を使う（デバッグ）",
            help: "開始後は通常の映像認識に切り替わります。自動開始をOFF、またはデバッグをONにすると、停止中も通常認識を使います。設定はこのブラウザに保存されます。",
            idle: ["接続待ち", "映像認識パネルでカメラを接続し、映像認識を有効にしてください。"],
            off: ["OFF", "白画面からの自動開始は無効です。映像認識は通常どおり動作します。"],
            debugMode: ["デバッグ中", "タイマー停止中も通常の映像認識を使います。白画面は検知しません。"],
            running: ["タイマー動作中", "白画面の検知を停止し、通常の映像認識を使っています。"],
            waiting: ["映像待ち", "カメラから新しい映像が届くと、白画面の検知を再開します。"],
            watching: ["動作中", "認識枠全体の白画面をWebGPUで最大30fps確認しています。白くなったら自動でタイマーを開始します。"],
            timestamp: ["撮影時刻を取得できません", "この映像では実際の撮影時刻を取得できないため、自動開始できません。タイマー開始ボタンを使ってください。"],
            unsupported: ["フレーム取得に未対応", "このブラウザでは撮影時刻付きの映像を取得できません。タイマー開始ボタンを使ってください。"],
            size: ["認識枠を確認してください", "映像が認識枠より小さいため、枠全体を確認できません。カメラの出力サイズを確認してください。"],
            error: ["映像を確認できません", "カメラを接続し直してください。タイマー開始ボタンからも開始できます。"]
        },
        en: {
            title: "Start timer on a white screen",
            description: "Connect a camera in the Vision panel. While the timer is stopped, a completely white recognition frame starts the timer from the time that frame was captured.",
            enabled: "Start automatically on white",
            debug: "Use normal recognition while stopped (debug)",
            help: "Normal recognition resumes when the timer starts. Turn auto-start off or debug on to use normal recognition while stopped. Settings are saved in this browser.",
            idle: ["Waiting for connection", "Connect a camera and enable recognition in the Vision panel."],
            off: ["OFF", "White-screen auto-start is disabled. Normal recognition is available."],
            debugMode: ["Debug", "Normal recognition runs while the timer is stopped. White detection is disabled."],
            running: ["Timer running", "White detection is stopped and normal recognition is in use."],
            waiting: ["Waiting for video", "White detection resumes when new camera frames arrive."],
            watching: ["Active", "Checking the entire recognition frame for white with WebGPU at up to 30fps. A white frame starts the timer automatically."],
            timestamp: ["Capture time unavailable", "This feed does not provide the actual capture time, so auto-start is unavailable. Use Start Timer."],
            unsupported: ["Frame capture unsupported", "This browser cannot provide video frames with capture times. Use Start Timer."],
            size: ["Check recognition area", "The video is smaller than the recognition area. Check the camera output size."],
            error: ["Cannot inspect video", "Reconnect the camera, or use Start Timer to start manually."]
        }
    };

    // Separate device and buffers: never borrow or mutate the vision matcher's resources.
    class WhiteFrameDetector {
        constructor() {
            this.device = null;
            this.pipeline = null;
            this.output = null;
            this.readback = null;
            this.params = null;
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
                    console.warn("White detector WebGPU device lost:", info);
                });
                const module = device.createShaderModule({code: `
struct Params {
    origin: vec2<u32>,
    size: vec2<u32>,
}

@group(0) @binding(0) var frame: texture_external;
struct Stats {
    below96: atomic<u32>,
    below112: atomic<u32>,
    below120: atomic<u32>,
    spread12: atomic<u32>,
}

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
        let sourcePixel = params.origin + pixel.xy;
        let rgba = textureLoad(frame, vec2<i32>(i32(sourcePixel.x), i32(sourcePixel.y)));
        let low = min(rgba.r, min(rgba.g, rgba.b));
        let high = max(rgba.r, max(rgba.g, rgba.b));
        if (low < (96.0 / 255.0)) { atomicAdd(&tileBelow96, 1u); }
        if (low < (112.0 / 255.0)) { atomicAdd(&tileBelow112, 1u); }
        if (low < WHITE_MIN_CHANNEL) { atomicAdd(&tileBelow120, 1u); }
        if (high - low > WHITE_MAX_CHANNEL_SPREAD) { atomicAdd(&tileSpread12, 1u); }
    }
    workgroupBarrier();
    if (localIndex == 0u) {
        atomicAdd(&stats.below96, atomicLoad(&tileBelow96));
        atomicAdd(&stats.below112, atomicLoad(&tileBelow112));
        atomicAdd(&stats.below120, atomicLoad(&tileBelow120));
        atomicAdd(&stats.spread12, atomicLoad(&tileSpread12));
    }
}`});
                const pipeline = await device.createComputePipelineAsync({
                    layout: "auto", compute: {module, entryPoint: "main"}
                });
                if (lost) throw new Error("White detector GPU lost during initialization");
                const output = device.createBuffer({
                    size: 16,
                    usage: GPUBufferUsage.STORAGE | GPUBufferUsage.COPY_SRC | GPUBufferUsage.COPY_DST
                });
                const readback = device.createBuffer({
                    size: 16,
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
            this.output = null;
            this.readback = null;
            this.params = null;
            device?.destroy();
        }

        async detect(video, rect) {
            const width = rect.sourceCropWidth;
            const height = rect.sourceCropHeight;
            const device = this.device;
            if (!device) {
                throw new Error("White detector WebGPU is not ready");
            }
            const readback = this.readback;
            try {
                // Import and submit in this requestVideoFrameCallback task. This keeps the
                // captureTime tied to exactly the video frame sampled by the GPU without a
                // full-frame CPU readback or an intermediate rgba8 texture copy.
                const externalTexture = device.importExternalTexture({source: video, colorSpace: "srgb"});
                device.queue.writeBuffer(this.params, 0, new Uint32Array([
                    rect.sourceX, rect.sourceY, width, height
                ]));
                const bindGroup = device.createBindGroup({
                    layout: this.pipeline.getBindGroupLayout(0),
                    entries: [
                        {binding: 0, resource: externalTexture},
                        {binding: 1, resource: {buffer: this.output}},
                        {binding: 2, resource: {buffer: this.params}}
                    ]
                });
                const encoder = device.createCommandEncoder();
                encoder.clearBuffer(this.output);
                const pass = encoder.beginComputePass();
                pass.setPipeline(this.pipeline);
                pass.setBindGroup(0, bindGroup);
                pass.dispatchWorkgroups(Math.ceil(width / 16), Math.ceil(height / 16));
                pass.end();
                encoder.copyBufferToBuffer(this.output, 0, readback, 0, 16);
                device.queue.submit([encoder.finish()]);
                await readback.mapAsync(GPUMapMode.READ);
                const values = new Uint32Array(readback.getMappedRange());
                return {
                    white: values[2] === 0 && values[3] === 0,
                    below96: values[0],
                    below112: values[1],
                    below120: values[2],
                    spread12: values[3]
                };
            } finally {
                if (readback?.mapState === "mapped") readback.unmap();
            }
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
    let busy = false;
    let clockOrigin = null;
    let lastBucket = -1;
    let lastFrameTime = null;
    let frameStatus = "watching";
    let pageActive = true;

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
            clockOrigin = null;
            lastFrameTime = null;
            lastBucket = -1;
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
        }
        lastFrameTime = frameTime;
        clockOrigin ??= frameTime;
        const bucket = Math.floor((frameTime - clockOrigin) * 30 / 1000 + 0.000001);
        if (bucket <= lastBucket) return;
        lastBucket = bucket;
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
        // WebGPU readback is intentionally single-flight. If the GPU takes longer
        // than one 30fps sampling interval, skip this frame instead of queueing work
        // indefinitely or falling back to getImageData() on the CPU.
        if (busy || !detector.device) return;
        const token = generation;
        const capturedStream = stream;
        const capturedTrack = track;
        busy = true;
        const acceptResult = (result) => {
            if (token !== generation || capturedStream !== video.srcObject || !active || !wantsDetection()) return;
            if (video.paused || capturedTrack.readyState !== "live" || capturedTrack.muted
                || capturedStream.getVideoTracks()[0] !== capturedTrack) return;
            if (new URLSearchParams(location.search).has("whiteDebug")) {
                document.title = `WDBG ${rect.sourceCropWidth}x${rect.sourceCropHeight} `
                    + `b96=${result.below96} b112=${result.below112} b120=${result.below120} s12=${result.spread12}`;
            }
            if (result.white) timer.startFromCapture(captureTime);
        };
        // detect() imports and submits the callback's video frame before its first await.
        detector.detect(video, rect).then(acceptResult).catch((error) => {
            if (token === generation) {
                frameStatus = "error";
                render();
            }
            console.warn("White frame inspection failed:", error);
        }).finally(() => {
            busy = false;
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
    window.addEventListener("battle-auto-timer-change", () => sync(true));
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
