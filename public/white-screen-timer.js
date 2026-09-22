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
            watching: ["動作中", "認識枠全体の白画面を30fpsで確認しています。白くなったら自動でタイマーを開始します。"],
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
            watching: ["Active", "Checking the entire recognition frame for white at 30fps. A white frame starts the timer automatically."],
            timestamp: ["Capture time unavailable", "This feed does not provide the actual capture time, so auto-start is unavailable. Use Start Timer."],
            unsupported: ["Frame capture unsupported", "This browser cannot provide video frames with capture times. Use Start Timer."],
            size: ["Check recognition area", "The video is smaller than the recognition area. Check the camera output size."],
            error: ["Cannot inspect video", "Reconnect the camera, or use Start Timer to start manually."]
        }
    };

    // Separate device, textures and canvas: never borrow or mutate the vision matcher's resources.
    class WhiteFrameDetector {
        constructor() {
            this.canvas = document.createElement("canvas");
            this.context = this.canvas.getContext("2d", {willReadFrequently: true});
            this.device = null;
            this.attempted = false;
        }

        async prepare() {
            if (this.attempted) return;
            this.attempted = true;
            if (!navigator.gpu) return;
            let device;
            try {
                const adapter = await navigator.gpu.requestAdapter();
                if (!adapter) return;
                device = await adapter.requestDevice();
                let lost = false;
                device.lost.then(() => {
                    lost = true;
                    if (this.device === device) this.useCpu();
                });
                const module = device.createShaderModule({code: `
@group(0) @binding(0) var frame: texture_2d<f32>;
@group(0) @binding(1) var<storage, read_write> nonWhite: atomic<u32>;
var<workgroup> tileNonWhite: atomic<u32>;

@compute @workgroup_size(16, 16)
fn main(@builtin(global_invocation_id) pixel: vec3<u32>,
        @builtin(local_invocation_index) localIndex: u32) {
    if (localIndex == 0u) { atomicStore(&tileNonWhite, 0u); }
    workgroupBarrier();
    let size = textureDimensions(frame);
    if (pixel.x < size.x && pixel.y < size.y) {
        if (any(textureLoad(frame, vec2<i32>(pixel.xy), 0) != vec4<f32>(1.0))) {
            atomicStore(&tileNonWhite, 1u);
        }
    }
    workgroupBarrier();
    if (localIndex == 0u && atomicLoad(&tileNonWhite) != 0u) {
        atomicStore(&nonWhite, 1u);
    }
}`});
                const pipeline = await device.createComputePipelineAsync({
                    layout: "auto", compute: {module, entryPoint: "main"}
                });
                if (lost) throw new Error("White detector GPU lost during initialization");
                this.pipeline = pipeline;
                this.device = device;
            } catch (error) {
                device?.destroy();
                console.warn("White detector uses CPU:", error);
            }
        }

        useCpu() {
            const device = this.device;
            this.device = null;
            this.texture = null;
            this.output = null;
            this.readback = null;
            device?.destroy();
        }

        async detect(video, rect, forceCpu = false) {
            const width = rect.sourceCropWidth;
            const height = rect.sourceCropHeight;
            const device = forceCpu ? null : this.device;
            if (!device) {
                if (this.canvas.width !== width || this.canvas.height !== height) {
                    this.canvas.width = width;
                    this.canvas.height = height;
                }
                const context = this.context;
                context.clearRect(0, 0, width, height);
                context.imageSmoothingEnabled = false;
                // Snapshot synchronously in the frame callback, before any await.
                context.drawImage(video, rect.sourceX, rect.sourceY, width, height, 0, 0, width, height);
                const {data} = context.getImageData(0, 0, width, height);
                for (let i = 0; i < data.length; i += 4) {
                    if (data[i] !== 255 || data[i + 1] !== 255 || data[i + 2] !== 255 || data[i + 3] !== 255) {
                        return false;
                    }
                }
                return true;
            }

            device.pushErrorScope("validation");
            let scopePopped = false;
            let readback;
            try {
                if (!this.texture || this.texture.width !== width || this.texture.height !== height) {
                    this.texture?.destroy();
                    this.output?.destroy();
                    this.readback?.destroy();
                    this.texture = device.createTexture({
                        size: [width, height], format: "rgba8unorm",
                        usage: GPUTextureUsage.TEXTURE_BINDING | GPUTextureUsage.COPY_DST | GPUTextureUsage.RENDER_ATTACHMENT
                    });
                    this.output = device.createBuffer({size: 4, usage: GPUBufferUsage.STORAGE | GPUBufferUsage.COPY_SRC | GPUBufferUsage.COPY_DST});
                    this.readback = device.createBuffer({size: 4, usage: GPUBufferUsage.COPY_DST | GPUBufferUsage.MAP_READ});
                    this.bindGroup = device.createBindGroup({
                        layout: this.pipeline.getBindGroupLayout(0),
                        entries: [{binding: 0, resource: this.texture.createView()}, {binding: 1, resource: {buffer: this.output}}]
                    });
                }
                readback = this.readback;
                // Copy the callback's frame immediately; a later video frame must not inherit its captureTime.
                device.queue.copyExternalImageToTexture(
                    {source: video, origin: [rect.sourceX, rect.sourceY]},
                    {texture: this.texture, colorSpace: "srgb"}, [width, height]
                );
                const encoder = device.createCommandEncoder();
                encoder.clearBuffer(this.output);
                const pass = encoder.beginComputePass();
                pass.setPipeline(this.pipeline);
                pass.setBindGroup(0, this.bindGroup);
                pass.dispatchWorkgroups(Math.ceil(width / 16), Math.ceil(height / 16));
                pass.end();
                encoder.copyBufferToBuffer(this.output, 0, readback, 0, 4);
                device.queue.submit([encoder.finish()]);
                const validation = device.popErrorScope();
                scopePopped = true;
                const [, error] = await Promise.all([readback.mapAsync(GPUMapMode.READ), validation]);
                if (error) throw new Error(error.message);
                return new Uint32Array(readback.getMappedRange())[0] === 0;
            } finally {
                if (!scopePopped) await device.popErrorScope();
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
    let pendingFrames = [];
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
            pendingFrames = [];
            if (frameHandle !== null) video.cancelVideoFrameCallback(frameHandle);
            frameHandle = null;
            active = nextActive;
            clockOrigin = null;
            lastFrameTime = null;
            lastBucket = -1;
            frameStatus = "watching";
        }
        if (active && frameHandle === null) {
            void detector.prepare();
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
        const token = generation;
        const capturedStream = stream;
        const capturedTrack = track;
        const result = {captureTime, done: false, white: false};
        pendingFrames.push(result);
        // Keep sampling when GPU readback takes longer than a frame. CPU results wait their turn.
        const forceCpu = busy;
        if (!forceCpu) busy = true;
        const acceptResult = (white) => {
            if (token !== generation || capturedStream !== video.srcObject || !active || !wantsDetection()) return;
            if (video.paused || capturedTrack.readyState !== "live" || capturedTrack.muted
                || capturedStream.getVideoTracks()[0] !== capturedTrack) return;
            result.white = white;
            result.done = true;
            while (pendingFrames[0]?.done) {
                const first = pendingFrames.shift();
                if (first.white && timer.startFromCapture(first.captureTime)) break;
            }
        };
        // detect() copies synchronously before its first await. Only the result is asynchronous.
        detector.detect(video, rect, forceCpu).then(acceptResult).catch((error) => {
            const hadGpu = !forceCpu && Boolean(detector.device);
            if (hadGpu) detector.useCpu();
            acceptResult(false);
            if (token === generation) {
                frameStatus = hadGpu ? "watching" : "error";
                render();
            }
            console.warn("White frame inspection failed:", error);
        }).finally(() => {
            if (!forceCpu) busy = false;
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
        detector.useCpu();
    });
    window.addEventListener("pageshow", () => {
        pageActive = true;
        sync(true);
    });
    new MutationObserver(syncLanguage).observe(document.documentElement, {attributes: true, attributeFilter: ["data-lang"]});
    restoreSettings();
    syncLanguage();
})();
