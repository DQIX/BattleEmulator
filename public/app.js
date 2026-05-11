const ui = {
    emulatorSelect: document.getElementById("emulatorSelect"),
    emulatorMeta: document.getElementById("emulatorMeta"),
    emulatorStatus: document.getElementById("emulatorStatus"),
    offsetSeconds: document.getElementById("offsetSeconds"),
    searchRangeSeconds: document.getElementById("searchRangeSeconds"),
    threads: document.getElementById("threads"),
    actionInput: document.getElementById("actionInput"),
    runButton: document.getElementById("runButton"),
    autoTimerPreview: document.getElementById("autoTimerPreview"),
    autoTimerStatus: document.getElementById("autoTimerStatus"),
    autoTimerStartButton: document.getElementById("autoTimerStartButton"),
    autoTimerUseButton: document.getElementById("autoTimerUseButton"),
    autoTimerFractionField: document.getElementById("autoTimerFractionField"),
    autoTimerFractionInput: document.getElementById("autoTimerFractionInput"),
    autoTimerResetButton: document.getElementById("autoTimerResetButton"),
    autoTimerResetConfirm: document.getElementById("autoTimerResetConfirm"),
    autoTimerResetConfirmButton: document.getElementById("autoTimerResetConfirmButton"),
    autoTimerResetCancelButton: document.getElementById("autoTimerResetCancelButton"),
    seedHex: document.getElementById("seedHex"),
    seedSpeed: document.getElementById("seedSpeed"),
    seedElapsed: document.getElementById("seedElapsed"),
    seedDrift: document.getElementById("seedDrift"),
    seedState: document.getElementById("seedState"),
    dumpOutput: document.getElementById("dumpOutput"),
    logOutput: document.getElementById("logOutput"),
    shortcutList: document.querySelector(".shortcut-list"),
    themeSelect: document.getElementById("themeSelect"),
    langSelect: document.getElementById("langSelect"),
    preloadToggle: document.getElementById("preloadToggle"),
    memoTableBody: document.getElementById("memoTableBody"),
    memoEmpty: document.getElementById("memoEmpty"),
    memoScroll: document.getElementById("memoScroll"),
    memoCopyMarkdown: document.getElementById("memoCopyMarkdown"),
    memoCopyCsv: document.getElementById("memoCopyCsv"),
    visionConnectButton: document.getElementById("visionConnectButton"),
    visionApplyFormatButton: document.getElementById("visionApplyFormatButton"),
    visionstage: document.getElementById("visionstage")
};

const DEFAULT_OFFSET_SECONDS = 15;
const OFFSET_STORAGE_KEY = "dq9OffsetSeconds";
const DEFAULT_SEARCH_RANGE_SECONDS = 6;
const SEARCH_RANGE_STORAGE_KEY = "dq9SearchRangeSeconds";
const SHORTCUT_STORAGE_KEY = "dq9ButtonShortcuts";
const AUTO_TIMER_MAX_SECONDS = 30 * 60 * 60;
const AUTO_TIMER_CORRECTION_LIMIT_MS = 5 * 60 * 1000;
const AUTO_TIMER_FRACTION_SCALE = 10000;
const AUTO_TIMER_FRACTION_HIDE_DELAY_MS = 5 * 60 * 1000;
const SEED_MEMO_STORAGE_KEY = "dq9SeedMemoList";
const SEED_MEMO_LIMIT = 200;
const SEED_TIME_SCALE = 100n;
const SEED_SECONDS_NUMERATOR = 10000n;
const SEED_SECONDS_DIVISOR = 799n;
const SHORTCUT_MODIFIER_CODES = new Set([
    "ShiftLeft",
    "ShiftRight",
    "ControlLeft",
    "ControlRight",
    "AltLeft",
    "AltRight",
    "MetaLeft",
    "MetaRight"
]);
const SHORTCUT_FOCUS_TARGETS = new Set([
    "actionInput",
    "dumpOutput",
    "visionstage",
]);

const state = {
    emulators: [],
    active: null,
    running: false,
    lang: document.documentElement.dataset.lang || "ja",
    theme: document.documentElement.dataset.theme || "lightSepia",
    emulatorStatusKey: "idle",
    preload: localStorage.getItem("dq9Preload") === "1",
    offsetSeconds: DEFAULT_OFFSET_SECONDS,
    searchRangeSeconds: DEFAULT_SEARCH_RANGE_SECONDS,
    preloadQueue: Promise.resolve(),
    moduleCache: new Map(),
    workerScriptText: "",
    workerBlobUrl: "",
    memoList: [],
    urlOverrides: null,
    urlOverridesAppliedNonEmu: false,
    urlOverridesAppliedEmu: false,
    autoTimerAnchor: null,
    autoTimerTickHandle: null,
    autoTimerAppliedPrefix: "",
    autoTimerLastUse: null,
    autoTimerFractionHideHandle: null,
    autoTimerFractionSourceTimeText: "",
    autoTimerCorrectionCount: 0,
    shortcutBindings: {},
    shortcutCaptureTarget: "",
    shortcutPointerInsideWindow: true
};

const logLines = [];
const SEARCH_RANGE_SECONDS_PATTERN = /^(?:0\.(?:0[1-9]|[1-9]\d?)|(?:[1-9]|1[0-4])(?:\.\d{1,2})?|15(?:\.0{1,2})?)$/;
const SEARCH_RANGE_SECONDS_PARTIAL_PATTERN = /^(?:|0|0\.|0\.\d{0,2}|[1-9](?:\.\d{0,2})?|1[0-4](?:\.\d{0,2})?|15(?:\.0{0,2})?)$/;

function getDictionary() {
    const config = window.APP_CONFIG || {};
    return config.i18n ? config.i18n[state.lang] : null;
}

function t(key, fallback) {
    const dict = getDictionary();
    if (dict && dict[key]) {
        return dict[key];
    }
    return fallback;
}

function pad2(value) {
    return String(value).padStart(2, "0");
}

function formatInputTime(parsed) {
    return `${pad2(parsed.hours)}:${pad2(parsed.minutes)}:${pad2(parsed.seconds)}`;
}

function formatTimerPreview(totalSeconds) {
    if (!Number.isFinite(totalSeconds) || totalSeconds < 0) {
        return "--:--:--";
    }
    const clamped = Math.min(AUTO_TIMER_MAX_SECONDS, Math.floor(totalSeconds));
    const hours = Math.floor(clamped / 3600);
    const minutes = Math.floor((clamped % 3600) / 60);
    const seconds = clamped % 60;
    return `${pad2(hours)}:${pad2(minutes)}:${pad2(seconds)}`;
}

function formatActionTime(totalSeconds) {
    const clamped = Math.max(0, Math.min(AUTO_TIMER_MAX_SECONDS, Math.floor(totalSeconds)));
    const hours = Math.floor(clamped / 3600);
    const minutes = Math.floor((clamped % 3600) / 60);
    const seconds = clamped % 60;
    return `${hours} ${minutes} ${seconds}`;
}

function formatFractionDigits(value) {
    return String(value).padStart(4, "0");
}

function formatMemoFractionDigits(value) {
    const digits = normalizeFractionDigits(value);
    if (digits === null || digits === 0) {
        return "0";
    }
    return formatFractionDigits(digits);
}

function formatSearchRangeSeconds(value) {
    if (!Number.isFinite(value)) {
        return String(DEFAULT_SEARCH_RANGE_SECONDS);
    }
    return value.toFixed(2).replace(/\.?0+$/, "");
}

function normalizeFractionDigits(value) {
    if (value === null || value === undefined || value === "") {
        return null;
    }
    const numeric = Number.parseInt(String(value), 10);
    if (!Number.isFinite(numeric)) {
        return null;
    }
    return Math.max(0, Math.min(AUTO_TIMER_FRACTION_SCALE - 1, numeric));
}

function splitPreciseSeconds(totalSeconds) {
    const safeSeconds = Math.max(0, Number.isFinite(totalSeconds) ? totalSeconds : 0);
    const totalUnits = Math.floor((safeSeconds + 1e-9) * AUTO_TIMER_FRACTION_SCALE);
    const wholeSeconds = Math.floor(totalUnits / AUTO_TIMER_FRACTION_SCALE);
    const fraction = totalUnits % AUTO_TIMER_FRACTION_SCALE;
    return {
        wholeSeconds,
        fraction
    };
}

function parsedToTotalSeconds(parsed) {
    if (!parsed) {
        return 0;
    }
    return parsed.hours * 3600 + parsed.minutes * 60 + parsed.seconds;
}

function getAutoTimerFractionDigits() {
    return normalizeFractionDigits(ui.autoTimerFractionInput ? ui.autoTimerFractionInput.value : "");
}

function clearAutoTimerFractionHideTimer() {
    if (state.autoTimerFractionHideHandle !== null) {
        clearTimeout(state.autoTimerFractionHideHandle);
        state.autoTimerFractionHideHandle = null;
    }
}

function updateAutoTimerFractionVisibility() {
    if (!ui.autoTimerFractionField) {
        return;
    }
    ui.autoTimerFractionField.classList.remove("is-hidden");
    if (ui.autoTimerFractionInput) {
        ui.autoTimerFractionInput.disabled = false;
    }
}

function setAutoTimerFractionDigits(value, timeText = "") {
    if (!ui.autoTimerFractionInput) {
        return;
    }
    const digits = normalizeFractionDigits(value);
    ui.autoTimerFractionInput.value = digits === null || digits === 0 ? "" : formatFractionDigits(digits);
    state.autoTimerFractionSourceTimeText = digits === null || digits === 0 ? "" : timeText;
    updateAutoTimerFractionVisibility();
}

function restartAutoTimerFractionHideTimer() {
    clearAutoTimerFractionHideTimer();
}

function extractInputTimeText(text) {
    const tokens = String(text || "").trim().split(/\s+/).filter(Boolean);
    if (tokens.length < 3) {
        return "";
    }
    return tokens.slice(0, 3).join(" ");
}

function formatDatePartsUTC(date) {
    const year = date.getUTCFullYear();
    const month = pad2(date.getUTCMonth() + 1);
    const day = pad2(date.getUTCDate());
    const hours = pad2(date.getUTCHours());
    const minutes = pad2(date.getUTCMinutes());
    const seconds = pad2(date.getUTCSeconds());
    return `${year}-${month}-${day} ${hours}:${minutes}:${seconds}`;
}

function formatRealTimeDisplay(savedAt) {
    if (!savedAt) {
        return "-";
    }
    const base = new Date(savedAt);
    if (Number.isNaN(base.getTime())) {
        return "-";
    }
    if (state.lang === "ja") {
        const jstDate = new Date(base.getTime() + 9 * 60 * 60 * 1000);
        return `${formatDatePartsUTC(jstDate)} JST`;
    }
    return `${formatDatePartsUTC(base)} UTC`;
}

function findMemoFractionDigitsForInput(inputText) {
    const normalizedInput = String(inputText || "").trim();
    const timeText = extractInputTimeText(normalizedInput);
    if (!timeText || !state.memoList.length) {
        return null;
    }
    const entry = state.memoList.find((item) => item.input === normalizedInput) ||
        state.memoList.find((item) => item.timeText === timeText);
    if (!entry) {
        return null;
    }
    const digits = normalizeFractionDigits(entry.fractionDigits);
    return digits === null ? 0 : digits;
}

function restoreAutoTimerFractionForInput(inputText) {
    const timeText = extractInputTimeText(inputText);
    if (!timeText) {
        setAutoTimerFractionDigits(null, "");
        return;
    }
    const currentDigits = getAutoTimerFractionDigits();
    if (state.autoTimerFractionSourceTimeText === timeText && currentDigits !== null) {
        return;
    }
    const digits = findMemoFractionDigitsForInput(inputText);
    if (digits !== null && digits > 0) {
        setAutoTimerFractionDigits(digits, timeText);
        return;
    }
    setAutoTimerFractionDigits(null, "");
}

async function hashStringSHA256(text) {
    const data = new TextEncoder().encode(text);
    const buf = await crypto.subtle.digest("SHA-256", data);
    const hashArray = Array.from(new Uint8Array(buf));
    return hashArray.map(b => b.toString(16).padStart(2, "0")).join("");
}


function loadSeedMemos() {
    const stored = localStorage.getItem(SEED_MEMO_STORAGE_KEY);
    if (!stored) {
        return [];
    }
    try {
        const parsed = JSON.parse(stored);
        return Array.isArray(parsed) ? parsed : [];
    } catch (err) {
        return [];
    }
}

function saveSeedMemos(list) {
    localStorage.setItem(SEED_MEMO_STORAGE_KEY, JSON.stringify(list));
}

async function buildMemoFingerprint(entry) {
    const payload = {
        input: entry.input,
        timeText: entry.timeText,
        fractionDigits: entry.fractionDigits || 0,
        driftText: entry.driftText || "",
        offsetSeconds: entry.offsetSeconds,
        searchRangeSeconds: entry.searchRangeSeconds
    };
    return await hashStringSHA256(JSON.stringify(payload));
}

function buildOverrideUrl(entry) {
    const url = new URL(window.location.href);
    url.search = "";
    url.searchParams.set("emu", entry.emulatorLabel);
    url.searchParams.set("offset", String(entry.offsetSeconds));
    url.searchParams.set("range", String(entry.searchRangeSeconds));
    url.searchParams.set("input", entry.input);
    if (entry.fractionDigits > 0) {
        url.searchParams.set("fraction", String(entry.fractionDigits));
    }
    return url.toString();
}

function copyText(text) {
    if (!text) {
        return;
    }
    if (navigator.clipboard && navigator.clipboard.writeText) {
        navigator.clipboard.writeText(text).catch(() => {
        });
        return;
    }
    const temp = document.createElement("textarea");
    temp.value = text;
    temp.style.position = "fixed";
    temp.style.opacity = "0";
    document.body.appendChild(temp);
    temp.focus();
    temp.select();
    try {
        document.execCommand("copy");
    } catch (err) {
    }
    document.body.removeChild(temp);
}

function appendLog(line) {
    const timestamp = new Date().toLocaleTimeString();
    logLines.push(`[${timestamp}] ${line}`);
    ui.logOutput.value = logLines.join("\n");
    ui.logOutput.scrollTop = ui.logOutput.scrollHeight;
}

function setSeedState(text, count) {
    const label = t(text, text);
    if (typeof count === "number") {
        ui.seedState.textContent = `${label} (${count})`;
    } else {
        ui.seedState.textContent = label;
    }
}

function renderSeedMemos(list) {
    if (!ui.memoTableBody || !ui.memoEmpty || !ui.memoScroll) {
        return;
    }
    ui.memoTableBody.innerHTML = "";
    if (!list.length) {
        ui.memoEmpty.hidden = false;
        ui.memoScroll.hidden = true;
        return;
    }
    ui.memoEmpty.hidden = true;
    ui.memoScroll.hidden = false;

    list.forEach((entry) => {
        entry.memo = entry.memo || "";
        entry.fractionDigits = normalizeFractionDigits(entry.fractionDigits) || 0;
        const row = document.createElement("tr");
        row.className = "memo-row";

        const cellTime = document.createElement("td");
        cellTime.textContent = entry.timeText;

        const cellFraction = document.createElement("td");
        cellFraction.textContent = formatMemoFractionDigits(entry.fractionDigits);

        const cellRealTime = document.createElement("td");
        cellRealTime.textContent = formatRealTimeDisplay(entry.savedAt);

        const cellEmu = document.createElement("td");
        cellEmu.textContent = entry.emulatorLabel;

        const cellOffset = document.createElement("td");
        cellOffset.textContent = String(entry.offsetSeconds);

        const cellRange = document.createElement("td");
        cellRange.textContent = String(entry.searchRangeSeconds);

        const cellDrift = document.createElement("td");
        cellDrift.textContent = entry.driftText || "-";

        const cellInput = document.createElement("td");
        cellInput.className = "memo-input";
        const inputText = document.createElement("span");
        inputText.textContent = entry.input;
        inputText.title = entry.input;
        const copyInput = document.createElement("a");
        copyInput.href = "#";
        copyInput.className = "memo-copy";
        copyInput.dataset.copyInput = entry.id;
        copyInput.textContent = t("memoCopyInput", "Copy input");
        cellInput.appendChild(inputText);
        cellInput.appendChild(copyInput);

        const cellNote = document.createElement("td");
        const noteInput = document.createElement("input");
        noteInput.type = "text";
        noteInput.className = "memo-note-input";
        noteInput.value = entry.memo;
        noteInput.dataset.memoId = entry.id;
        cellNote.appendChild(noteInput);

        const cellUrl = document.createElement("td");
        const copyUrl = document.createElement("a");
        copyUrl.href = buildOverrideUrl(entry);
        copyUrl.className = "memo-link";
        copyUrl.dataset.copyUrl = entry.id;
        copyUrl.textContent = t("memoCopyUrl", "Copy URL");
        cellUrl.appendChild(copyUrl);

        row.appendChild(cellTime);
        row.appendChild(cellFraction);
        row.appendChild(cellRealTime);
        row.appendChild(cellEmu);
        row.appendChild(cellOffset);
        row.appendChild(cellRange);
        row.appendChild(cellDrift);
        row.appendChild(cellInput);
        row.appendChild(cellNote);
        row.appendChild(cellUrl);
        ui.memoTableBody.appendChild(row);
    });
}

async function recordSeedMemo(parsed, inputText, driftText) {
    if (!state.active || !parsed) {
        return;
    }
    const entry = {
        emulatorLabel: state.active.label,
        input: inputText,
        timeText: formatInputTime(parsed),
        fractionDigits: getAutoTimerFractionDigits() || 0,
        driftText: driftText || "",
        offsetSeconds: state.offsetSeconds,
        searchRangeSeconds: state.searchRangeSeconds,
        savedAt: new Date().toISOString()
    };
    entry.id = await buildMemoFingerprint(entry);
    const exists = state.memoList.some((item) => item.id === entry.id);
    if (exists) {
        return;
    }
    state.memoList.unshift(entry);
    if (state.memoList.length > SEED_MEMO_LIMIT) {
        state.memoList.length = SEED_MEMO_LIMIT;
    }
    saveSeedMemos(state.memoList);
    renderSeedMemos(state.memoList);
}

function memoToMarkdown(list) {
    const header = ["Time", "Fraction", "Real", "Emu", "Offset", "Range", "Drift", "Input", "Memo"];
    const rows = list.map((entry) => [
        entry.timeText,
        formatMemoFractionDigits(entry.fractionDigits),
        formatRealTimeDisplay(entry.savedAt),
        entry.emulatorLabel,
        entry.offsetSeconds,
        entry.searchRangeSeconds,
        entry.driftText || "-",
        entry.input.replace(/\n/g, " "),
        entry.memo || ""
    ]);
    const lines = [
        `| ${header.join(" | ")} |`,
        `| ${header.map(() => "---").join(" | ")} |`,
        ...rows.map((row) => `| ${row.join(" | ")} |`)
    ];
    return lines.join("\n");
}

function memoToCsv(list) {
    const escapeField = (value) => `"${String(value).replace(/"/g, '""')}"`;
    const header = ["Time", "Fraction", "Real", "Emu", "Offset", "Range", "Drift", "Input", "Memo"];
    const rows = list.map((entry) => [
        entry.timeText,
        formatMemoFractionDigits(entry.fractionDigits),
        formatRealTimeDisplay(entry.savedAt),
        entry.emulatorLabel,
        entry.offsetSeconds,
        entry.searchRangeSeconds,
        entry.driftText || "-",
        entry.input.replace(/\n/g, " "),
        entry.memo || ""
    ]);
    return [header, ...rows].map((row) => row.map(escapeField).join(",")).join("\n");
}

function parseUrlOverrides() {
    const url = new URL(window.location.href);
    const params = url.searchParams;
    const overrides = {};
    let changed = false;

    if (params.has("emu")) {
        const emu = params.get("emu").trim();
        if (emu) {
            overrides.emu = emu;
            params.delete("emu");
            changed = true;
        }
    }

    if (params.has("offset")) {
        const v = normalizeOffsetSeconds(params.get("offset"));
        if (v !== null) {
            overrides.offsetSeconds = v;
            params.delete("offset");
            changed = true;
        }
    }

    if (params.has("range")) {
        const v = normalizeSearchRangeSeconds(params.get("range"));
        if (v !== null) {
            overrides.searchRangeSeconds = v;
            params.delete("range");
            changed = true;
        }
    }

    if (params.has("input")) {
        const input = params.get("input").trim();
        if (input) {
            overrides.actionInput = input;
            params.delete("input");
            changed = true;
        }
    }

    if (params.has("fraction")) {
        const fractionDigits = normalizeFractionDigits(params.get("fraction"));
        if (fractionDigits !== null) {
            overrides.fractionDigits = fractionDigits;
            params.delete("fraction");
            changed = true;
        }
    }

    if (changed) {
        history.replaceState(null, "", url.toString());
    }

    return Object.keys(overrides).length ? overrides : null;
}


function findEmulatorIndexByLabel(label) {
    return state.emulators.findIndex((emu) => emu.label === label);
}

function applyUrlOverrides(applyEmu) {
    const overrides = state.urlOverrides;
    if (!overrides) {
        return;
    }
    if (!state.urlOverridesAppliedNonEmu) {
        if (typeof overrides.offsetSeconds === "number") {
            setOffsetSeconds(overrides.offsetSeconds);
        }
        if (typeof overrides.searchRangeSeconds === "number") {
            setSearchRangeSeconds(overrides.searchRangeSeconds);
        }
        if (overrides.actionInput && ui.actionInput) {
            ui.actionInput.value = overrides.actionInput;
            if (typeof overrides.fractionDigits === "number" && ui.autoTimerFractionInput) {
                setAutoTimerFractionDigits(
                    overrides.fractionDigits,
                    extractInputTimeText(overrides.actionInput)
                );
            } else {
                restoreAutoTimerFractionForInput(overrides.actionInput);
            }
        } else if (typeof overrides.fractionDigits === "number" && ui.autoTimerFractionInput) {
            setAutoTimerFractionDigits(overrides.fractionDigits, "");
        }
        state.urlOverridesAppliedNonEmu = true;
    }
    if (applyEmu && overrides.emu && !state.urlOverridesAppliedEmu) {
        const index = findEmulatorIndexByLabel(overrides.emu);
        if (index >= 0) {
            ui.emulatorSelect.selectedIndex = index;
            setActiveEmulator(index);
            state.urlOverridesAppliedEmu = true;
        }
    }
}

function formatScaledSeconds(scaledValue, scale) {
    const digits = scale.toString().length - 1;
    const sign = scaledValue < 0n ? "-" : "";
    const absValue = scaledValue < 0n ? -scaledValue : scaledValue;
    const whole = absValue / scale;
    const fraction = absValue % scale;
    return `${sign}${whole.toString()}.${fraction.toString().padStart(digits, "0")}`;
}

function computeSeedSecondsScaled(seed) {
    const shifted = seed >> 16n;
    const SCALE_RATIO = BigInt(AUTO_TIMER_FRACTION_SCALE) / SEED_TIME_SCALE; // 10000n / 100n = 100n
    return (shifted * SEED_SECONDS_NUMERATOR * SCALE_RATIO) / SEED_SECONDS_DIVISOR;
}

function computeSeedTimeUnits(seed) {
    // computeSeedSecondsScaled が既に AUTO_TIMER_FRACTION_SCALE(10000) 倍スケールになったのでそのまま返す
    return computeSeedSecondsScaled(seed);
}

function computeRealSecondsScaled(parsed, preciseTimeUnits = null) {
    if (preciseTimeUnits !== null) {
        // preciseTimeUnits は既に AUTO_TIMER_FRACTION_SCALE(10000) 倍スケール
        return preciseTimeUnits;
    }
    const totalSeconds = parsed.hours * 3600 + parsed.minutes * 60 + parsed.seconds;
    return BigInt(totalSeconds) * BigInt(AUTO_TIMER_FRACTION_SCALE);
}

function computeSeedDriftText(seed, parsed, preciseTimeUnits = null) {
    const seedSecondsScaled = computeSeedSecondsScaled(seed);
    const realSecondsScaled = computeRealSecondsScaled(parsed, preciseTimeUnits);
    const offsetScaled = BigInt(normalizeOffsetSeconds(state.offsetSeconds) * AUTO_TIMER_FRACTION_SCALE);
    const driftScaled = realSecondsScaled - seedSecondsScaled - offsetScaled;
    return formatScaledSeconds(driftScaled, BigInt(AUTO_TIMER_FRACTION_SCALE));
}

function setSeedValues(seedText, parsedTime, preciseTimeUnits = null) {
    if (!seedText) {
        ui.seedHex.textContent = "-";
        ui.seedSpeed.textContent = "-";
        ui.seedElapsed.textContent = "-";
        ui.seedDrift.textContent = "-";
        return;
    }
    const seed = BigInt(seedText);
    ui.seedHex.textContent = `0x${seed.toString(16)}`;
    ui.seedDrift.textContent = parsedTime ? computeSeedDriftText(seed, parsedTime, preciseTimeUnits) : "-";
}

function clearOutputs() {
    ui.dumpOutput.value = "";
    setSeedValues("");
    setSeedState("waiting");
}

function parseIntValue(el) {
    const value = Number.parseInt(el.value, 10);
    return Number.isFinite(value) ? value : 0;
}

function normalizeOffsetSeconds(value) {
    const parsed = Number.parseInt(value, 10);
    if (!Number.isFinite(parsed) || parsed < 0) {
        return DEFAULT_OFFSET_SECONDS;
    }
    return parsed;
}

function normalizeSearchRangeSeconds(value) {
    const text = String(value ?? "").trim();
    if (!SEARCH_RANGE_SECONDS_PATTERN.test(text)) {
        return DEFAULT_SEARCH_RANGE_SECONDS;
    }
    const parsed = Number(text);
    if (!Number.isFinite(parsed)) {
        return DEFAULT_SEARCH_RANGE_SECONDS;
    }
    return parsed;
}

function loadOffsetSeconds() {
    const stored = localStorage.getItem(OFFSET_STORAGE_KEY);
    return normalizeOffsetSeconds(stored);
}

function setOffsetSeconds(value) {
    const normalized = normalizeOffsetSeconds(value);
    state.offsetSeconds = normalized;
    if (ui.offsetSeconds) {
        ui.offsetSeconds.value = String(normalized);
    }
    localStorage.setItem(OFFSET_STORAGE_KEY, String(normalized));
    if (
        state.autoTimerAnchor &&
        state.autoTimerAppliedPrefix &&
        ui.actionInput &&
        ui.actionInput.value.startsWith(state.autoTimerAppliedPrefix)
    ) {
        const appliedSeconds = computeAutoTimerAppliedSeconds();
        if (appliedSeconds !== null) {
            const suffix = ui.actionInput.value.slice(state.autoTimerAppliedPrefix.length);
            const timeText = formatActionTime(appliedSeconds);
            ui.actionInput.value = `${timeText}${suffix}`;
            state.autoTimerAppliedPrefix = `${timeText} `;
            if (state.autoTimerLastUse) {
                state.autoTimerLastUse.timeText = timeText;
            }
            if (state.autoTimerFractionSourceTimeText) {
                state.autoTimerFractionSourceTimeText = timeText;
            }
        }
    }
    updateAutoTimerPreview();
}

function loadSearchRangeSeconds() {
    const stored = localStorage.getItem(SEARCH_RANGE_STORAGE_KEY);
    return normalizeSearchRangeSeconds(stored);
}

function setSearchRangeSeconds(value) {
    const normalized = normalizeSearchRangeSeconds(value);
    state.searchRangeSeconds = normalized;
    if (ui.searchRangeSeconds) {
        ui.searchRangeSeconds.value = formatSearchRangeSeconds(normalized);
    }
    localStorage.setItem(SEARCH_RANGE_STORAGE_KEY, String(normalized));
}

function getShortcutRows() {
    return Array.from(document.querySelectorAll(".shortcut-row[data-shortcut-target]"));
}

function parseShortcutBinding(value) {
    if (!value) {
        return null;
    }
    const parts = String(value).split("+").map((part) => part.trim()).filter(Boolean);
    if (!parts.length) {
        return null;
    }
    const binding = {
        ctrlKey: false,
        altKey: false,
        shiftKey: false,
        metaKey: false,
        code: ""
    };
    parts.forEach((part) => {
        if (part === "Ctrl") {
            binding.ctrlKey = true;
        } else if (part === "Alt") {
            binding.altKey = true;
        } else if (part === "Shift") {
            binding.shiftKey = true;
        } else if (part === "Meta") {
            binding.metaKey = true;
        } else if (!binding.code) {
            binding.code = part;
        }
    });
    if (!binding.code || SHORTCUT_MODIFIER_CODES.has(binding.code) || binding.code === "ControlLeft") {
        return null;
    }
    return binding;
}

function serializeShortcutBinding(binding) {
    if (!binding || !binding.code) {
        return "";
    }
    const parts = [];
    if (binding.ctrlKey) {
        parts.push("Ctrl");
    }
    if (binding.altKey) {
        parts.push("Alt");
    }
    if (binding.shiftKey) {
        parts.push("Shift");
    }
    if (binding.metaKey) {
        parts.push("Meta");
    }
    parts.push(binding.code);
    return parts.join("+");
}

function loadShortcutBindings() {
    let stored = {};
    try {
        stored = JSON.parse(localStorage.getItem(SHORTCUT_STORAGE_KEY) || "{}");
    } catch (err) {
        stored = {};
    }
    const bindings = {};
    getShortcutRows().forEach((row) => {
        const target = row.dataset.shortcutTarget;
        const hasStoredValue = Object.prototype.hasOwnProperty.call(stored, target);
        const parsed = hasStoredValue
            ? parseShortcutBinding(stored[target])
            : parseShortcutBinding(row.dataset.shortcutDefault);
        if (target && parsed) {
            bindings[target] = parsed;
        }
    });
    return bindings;
}

function saveShortcutBindings() {
    const serialized = {};
    getShortcutRows().forEach((row) => {
        const target = row.dataset.shortcutTarget;
        if (!target) {
            return;
        }
        serialized[target] = serializeShortcutBinding(state.shortcutBindings[target]);
    });
    localStorage.setItem(SHORTCUT_STORAGE_KEY, JSON.stringify(serialized));
}

function formatShortcutCode(code) {
    if (/^Key[A-Z]$/.test(code)) {
        return code.slice(3);
    }
    if (/^Digit[0-9]$/.test(code)) {
        return code.slice(5);
    }
    if (/^Numpad[0-9]$/.test(code)) {
        return `Num ${code.slice(6)}`;
    }
    const namedCodes = {
        Backquote: "`",
        Minus: "-",
        Equal: "=",
        BracketLeft: "[",
        BracketRight: "]",
        Backslash: "\\",
        Semicolon: ";",
        Quote: "'",
        Comma: ",",
        Period: ".",
        Slash: "/",
        Space: "Space",
        Enter: "Enter",
        Escape: "Esc",
        Tab: "Tab",
        Backspace: "Backspace",
        Delete: "Delete",
        Insert: "Insert",
        Home: "Home",
        End: "End",
        PageUp: "PageUp",
        PageDown: "PageDown",
        ArrowUp: "Up",
        ArrowDown: "Down",
        ArrowLeft: "Left",
        ArrowRight: "Right"
    };
    return namedCodes[code] || code;
}

function formatShortcutBinding(binding) {
    if (!binding || !binding.code) {
        return t("shortcutNotSet", "Not set");
    }
    const parts = [];
    if (binding.ctrlKey) {
        parts.push("Ctrl");
    }
    if (binding.altKey) {
        parts.push("Alt");
    }
    if (binding.shiftKey) {
        parts.push("Shift");
    }
    if (binding.metaKey) {
        parts.push("Meta");
    }
    parts.push(formatShortcutCode(binding.code));
    return parts.join("+");
}

function renderShortcutSettings() {
    getShortcutRows().forEach((row) => {
        const target = row.dataset.shortcutTarget;
        const captureButton = row.querySelector("[data-shortcut-capture]");
        const clearButton = row.querySelector("[data-shortcut-clear]");
        if (!target || !captureButton) {
            return;
        }
        const binding = state.shortcutBindings[target] || null;
        const capturing = state.shortcutCaptureTarget === target;
        captureButton.textContent = capturing
            ? t("shortcutWaiting", "Press a key")
            : formatShortcutBinding(binding);
        captureButton.classList.toggle("is-capturing", capturing);
        if (clearButton) {
            clearButton.disabled = !binding;
        }
    });
}

function setShortcutBinding(target, binding) {
    if (!target) {
        return;
    }
    if (binding && binding.code) {
        state.shortcutBindings[target] = binding;
    } else {
        delete state.shortcutBindings[target];
    }
    saveShortcutBindings();
    renderShortcutSettings();
}

function getShortcutFromEvent(event) {
    if (!event || !event.code || event.code === "ControlLeft" || SHORTCUT_MODIFIER_CODES.has(event.code)) {
        return null;
    }
    return {
        code: event.code,
        ctrlKey: Boolean(event.ctrlKey),
        altKey: Boolean(event.altKey),
        shiftKey: Boolean(event.shiftKey),
        metaKey: Boolean(event.metaKey)
    };
}

function matchesShortcut(binding, event) {
    if (!binding || !event) {
        return false;
    }
    return binding.code === event.code &&
        binding.ctrlKey === Boolean(event.ctrlKey) &&
        binding.altKey === Boolean(event.altKey) &&
        binding.shiftKey === Boolean(event.shiftKey) &&
        binding.metaKey === Boolean(event.metaKey);
}

function isShortcutTargetAvailable(target) {
    const element = ui[target];
    if (!element || element.hidden) {
        return false;
    }
    if ("disabled" in element && element.disabled) {
        return false;
    }
    if (!SHORTCUT_FOCUS_TARGETS.has(target) && typeof element.click !== "function") {
        return false;
    }
    const style = window.getComputedStyle(element);
    return style.display !== "none" && style.visibility !== "hidden" && element.getClientRects().length > 0;
}

function focusShortcutTarget(target) {
    const element = ui[target];
    if (target === "visionstage") {
        element.scrollIntoView({
            behavior: "smooth",
            block: "center"
        });
        return;
    }
    if (!element || typeof element.focus !== "function") {
        return;
    }

    // 今フォーカス中なら一度外す
    if (document.activeElement === element) {
        element.blur();
    } else if (document.activeElement &&
        typeof document.activeElement.blur === "function") {
        // 他の要素にフォーカス中でも外しておく
        document.activeElement.blur();
    }

    element.focus();

    if (target === "actionInput" &&
        typeof element.setSelectionRange === "function") {
        const end = element.value.length;
        element.setSelectionRange(end, end);
    } else {
        element.scrollIntoView({
            behavior: "smooth",
            block: "center"
        });

    }
}

function handleShortcutCapture(event) {
    if (!state.shortcutCaptureTarget) {
        return false;
    }
    if (event.code === "Escape") {
        state.shortcutCaptureTarget = "";
        renderShortcutSettings();
        return true;
    }
    if (event.code === "Backspace" || event.code === "Delete") {
        setShortcutBinding(state.shortcutCaptureTarget, null);
        state.shortcutCaptureTarget = "";
        renderShortcutSettings();
        return true;
    }
    const binding = getShortcutFromEvent(event);
    if (!binding) {
        return true;
    }
    setShortcutBinding(state.shortcutCaptureTarget, binding);
    state.shortcutCaptureTarget = "";
    renderShortcutSettings();
    return true;
}

function handleWindowShortcut(event) {
    if (handleShortcutCapture(event)) {
        return;
    }
    if (!state.shortcutPointerInsideWindow || event.code === "ControlLeft") {
        return;
    }
    for (const [target, binding] of Object.entries(state.shortcutBindings)) {
        if (!matchesShortcut(binding, event)) {
            continue;
        }
        event.preventDefault();
        if (!isShortcutTargetAvailable(target)) {
            return;
        }
        if (SHORTCUT_FOCUS_TARGETS.has(target)) {
            focusShortcutTarget(target);
            return;
        }
        ui[target].click();
        return;
    }
}

function initShortcutSettings() {
    state.shortcutBindings = loadShortcutBindings();
    renderShortcutSettings();
    if (!ui.shortcutList) {
        return;
    }
    ui.shortcutList.addEventListener("click", (event) => {
        const captureButton = event.target.closest("[data-shortcut-capture]");
        if (captureButton) {
            const target = captureButton.dataset.shortcutTarget;
            state.shortcutCaptureTarget = state.shortcutCaptureTarget === target ? "" : target;
            renderShortcutSettings();
            return;
        }
        const clearButton = event.target.closest("[data-shortcut-clear]");
        if (!clearButton) {
            return;
        }
        setShortcutBinding(clearButton.dataset.shortcutTarget, null);
        if (state.shortcutCaptureTarget === clearButton.dataset.shortcutTarget) {
            state.shortcutCaptureTarget = "";
            renderShortcutSettings();
        }
    });
    window.addEventListener("keydown", handleWindowShortcut);
}

function initMemoLedger() {
    state.memoList = loadSeedMemos();
    renderSeedMemos(state.memoList);
    state.urlOverrides = parseUrlOverrides();
    state.urlOverridesAppliedNonEmu = false;
    state.urlOverridesAppliedEmu = false;
    applyUrlOverrides(false);
}

function setAutoTimerStatusText(message) {
    if (ui.autoTimerStatus) {
        ui.autoTimerStatus.textContent = message;
    }
}

function setAutoTimerResetConfirmVisible(visible) {
    if (!ui.autoTimerResetConfirm) {
        return;
    }
    ui.autoTimerResetConfirm.classList.toggle("is-hidden", !visible);
}

function getAutoTimerStatusReadyText() {
    return t(
        "autoTimerStatusReady",
        "Preview is locked to performance.now() and ignores wall-clock changes."
    );
}

function getAutoTimerStatusIdleText() {
    return t(
        "autoTimerStatusIdle",
        "No timer anchor yet. Press Start Timer when the in-game timer begins."
    );
}

function computeAutoTimerSeconds(nowPerf = performance.now()) {
    if (!state.autoTimerAnchor) {
        return null;
    }
    const elapsedSeconds = Math.max(0, (nowPerf - state.autoTimerAnchor.perfNow) / 1000);
    return state.autoTimerAnchor.totalSeconds + elapsedSeconds;
}

function computeAutoTimerAppliedSeconds(nowPerf = performance.now()) {
    const seconds = computeAutoTimerSeconds(nowPerf);
    if (seconds === null) {
        return null;
    }
    return seconds - normalizeOffsetSeconds(state.offsetSeconds);
}

function updateAutoTimerButtons() {
    const hasAnchor = Boolean(state.autoTimerAnchor);
    if (ui.autoTimerUseButton) {
        ui.autoTimerUseButton.disabled = !hasAnchor || state.running;
    }
    if (ui.autoTimerResetButton) {
        ui.autoTimerResetButton.disabled = !hasAnchor || state.running;
    }
    if (ui.autoTimerResetConfirmButton) {
        ui.autoTimerResetConfirmButton.disabled = !hasAnchor || state.running;
    }
    if (ui.offsetSeconds) {
        ui.offsetSeconds.disabled = state.running || hasAnchor;
    }
    ui.autoTimerStartButton.disabled = state.running || hasAnchor;
}

function updateAutoTimerPreview() {
    const seconds = computeAutoTimerAppliedSeconds();
    if (ui.autoTimerPreview) {
        ui.autoTimerPreview.textContent = formatTimerPreview(seconds);
    }
    updateAutoTimerButtons();
}

function stopAutoTimerTicker() {
    if (state.autoTimerTickHandle !== null) {
        clearTimeout(state.autoTimerTickHandle);
        state.autoTimerTickHandle = null;
    }
}

function scheduleAutoTimerTick() {
    stopAutoTimerTicker();
    updateAutoTimerPreview();
    if (!state.autoTimerAnchor) {
        return;
    }
    const elapsedMs = Math.max(0, performance.now() - state.autoTimerAnchor.perfNow);
    const delay = 1000 - (Math.floor(elapsedMs) % 1000);
    state.autoTimerTickHandle = setTimeout(scheduleAutoTimerTick, Math.max(80, delay));
}

function setAutoTimerAnchorSeconds(totalSeconds, perfNow) {
    state.autoTimerAnchor = {
        totalSeconds,
        perfNow
    };
    setAutoTimerResetConfirmVisible(false);
    setAutoTimerStatusText(getAutoTimerStatusReadyText());
    scheduleAutoTimerTick();
}

function setAutoTimerAnchor(parsed, perfNow) {
    setAutoTimerAnchorSeconds(parsedToTotalSeconds(parsed), perfNow);
}

function startManualAutoTimer(perfNow = performance.now()) {
    setAutoTimerAnchorSeconds(normalizeOffsetSeconds(state.offsetSeconds), perfNow);
}

function clearAutoTimerAnchor() {
    state.autoTimerAnchor = null;
    state.autoTimerAppliedPrefix = "";
    state.autoTimerLastUse = null;
    state.autoTimerCorrectionCount = 0;
    clearAutoTimerFractionHideTimer();
    setAutoTimerFractionDigits(null, "");
    setAutoTimerResetConfirmVisible(false);
    stopAutoTimerTicker();
    updateAutoTimerPreview();
    setAutoTimerStatusText(getAutoTimerStatusIdleText());
}

function extractActionSuffix(text) {
    const tokens = text.trim().split(/\s+/).filter(Boolean);
    if (tokens.length <= 3) {
        return "";
    }
    return tokens.slice(3).join(" ");
}

function focusActionInputAtTop() {
    window.scrollTo({top: 0, behavior: "smooth"});
    requestAnimationFrame(() => {
        if (!ui.actionInput) {
            return;
        }
        ui.actionInput.focus({preventScroll: true});
        const end = ui.actionInput.value.length;
        ui.actionInput.setSelectionRange(end, end);
    });
}

function getPreciseSearchTimeUnits(parsed, rawInput) {
    const baseUnits = BigInt(parsedToTotalSeconds(parsed) * AUTO_TIMER_FRACTION_SCALE);
    const fractionDigits = getAutoTimerFractionDigits();
    if (
        fractionDigits === null ||
        fractionDigits === 0 ||
        extractInputTimeText(rawInput) !== state.autoTimerFractionSourceTimeText
    ) {
        return baseUnits;
    }
    return baseUnits + BigInt(fractionDigits);
}

function computeSeedRange(hours, minutes, seconds, offsetSeconds, preciseTimeUnits = null) {
    const seedShift = 65536n;
    const totalTimeUnits = preciseTimeUnits === null
        ? BigInt((hours * 3600 + minutes * 60 + seconds) * AUTO_TIMER_FRACTION_SCALE)
        : preciseTimeUnits;
    const offset = BigInt(normalizeOffsetSeconds(offsetSeconds) * AUTO_TIMER_FRACTION_SCALE);
    const range = BigInt(normalizeSearchRangeSeconds(state.searchRangeSeconds) * AUTO_TIMER_FRACTION_SCALE);
    const unitScale = BigInt(AUTO_TIMER_FRACTION_SCALE);
    const numerator1 = 2n * (totalTimeUnits - offset) - range;
    const numerator2 = 2n * (totalTimeUnits - offset) + range;
    const start = (numerator1 * 1000000n * seedShift) / (2n * 125155n * unitScale);
    const end   = (numerator2 * 1000000n * seedShift) / (2n * 125155n * unitScale);
    return { start, end };
}
function splitRange(start, end, threads) {
    const ranges = [];
    if (end <= start) {
        return ranges;
    }
    const length = end - start;
    const chunk = (length + BigInt(threads) - 1n) / BigInt(threads);
    for (let i = 0; i < threads; i += 1) {
        const rangeStart = start + chunk * BigInt(i);
        const rangeEnd = rangeStart + chunk > end ? end : rangeStart + chunk;
        if (rangeStart < rangeEnd) {
            ranges.push({start: rangeStart, end: rangeEnd});
        }
    }
    return ranges;
}

function enqueuePreload(task) {
    state.preloadQueue = state.preloadQueue.then(task).catch(() => {
    });
    return state.preloadQueue;
}

async function ensureWorkerScript() {
    if (state.workerScriptText) {
        return;
    }
    try {
        const response = await fetch("worker.js");
        state.workerScriptText = await response.text();
        const blob = new Blob([state.workerScriptText], {type: "application/javascript"});
        state.workerBlobUrl = URL.createObjectURL(blob);
    } catch (err) {
        appendLog("worker.js preload failed");
    }
}

async function ensureModulePayload(moduleUrl) {
    if (state.moduleCache.has(moduleUrl)) {
        return state.moduleCache.get(moduleUrl);
    }

    const jsText = await fetch(moduleUrl).then((r) => r.text());

    const payload = {jsText};
    state.moduleCache.set(moduleUrl, payload);
    return payload;
}


function preloadModule(moduleUrl) {
    return enqueuePreload(async () => {
        if (!state.preload) {
            return;
        }
        try {
            await ensureWorkerScript();
            await ensureModulePayload(moduleUrl);
            appendLog(`preloaded ${moduleUrl}`);
        } catch (err) {
            appendLog("preload failed");
        }
    });
}

function createWorkerClient(workerUrl) {
    const worker = new Worker(workerUrl || "worker.js");
    let counter = 0;
    const pending = new Map();

    worker.onmessage = (event) => {
        const {id, type, ...payload} = event.data;
        const entry = pending.get(id);
        if (!entry) return;
        pending.delete(id);
        entry.resolve({type, ...payload});
    };

    function call(type, payload) {
        return new Promise((resolve) => {
            const id = ++counter;
            pending.set(id, {resolve});
            worker.postMessage({id, type, ...payload});
        });
    }

    return {
        worker,
        call,

        // ここは将来拡張用に残すだけでOK
        ready: () => Promise.resolve(),

        terminate() {
            worker.terminate();
            pending.clear();
        }
    };
}


async function loadManifest() {
    try {
        const response = await fetch("emulators.json", {cache: "no-store"});
        if (!response.ok) {
            throw new Error("manifest not found");
        }
        const data = await response.json();
        if (!Array.isArray(data) || !data.length) {
            throw new Error("manifest empty");
        }
        state.emulators = data;
        populateEmulators();
        applyUrlOverrides(true);
        state.emulatorStatusKey = "ready";
        ui.emulatorStatus.textContent = t("ready", "ready");
    } catch (err) {
        state.emulatorStatusKey = "missing";
        ui.emulatorStatus.textContent = t("missing", "missing");
        ui.emulatorSelect.innerHTML = "";
        ui.emulatorMeta.textContent = "emulators.json not available";
        ui.runButton.disabled = true;
        appendLog("manifest load failed");
    }
}

function populateEmulators() {
    ui.emulatorSelect.innerHTML = "";
    state.emulators.forEach((emu, index) => {
        const option = document.createElement("option");
        option.value = String(index);
        option.textContent = `${emu.label} [${emu.branch}]`;
        ui.emulatorSelect.appendChild(option);
    });
    ui.emulatorSelect.selectedIndex = 0;
    setActiveEmulator(0);
}

function setActiveEmulator(index) {
    const emulator = state.emulators[index];
    if (!emulator) {
        return;
    }
    state.active = emulator;
    ui.emulatorMeta.textContent = `${emulator.branch} :: ${emulator.module}`;
    appendLog(`selected emulator ${emulator.label}`);
    if (state.preload) {
        const moduleUrl = new URL(emulator.module, window.location.href).toString();
        preloadModule(moduleUrl);
    }
}

function parseInput(text) {
    const tokens = text.trim().split(/\s+/).filter(Boolean);
    if (tokens.length < 4) {
        return {error: "input needs time and actions"};
    }
    const hours = Number.parseInt(tokens[0], 10);
    const minutes = Number.parseInt(tokens[1], 10);
    const seconds = Number.parseInt(tokens[2], 10);
    if (![hours, minutes, seconds].every(Number.isFinite)) {
        return {error: "invalid time format"};
    }
    return {
        hours,
        minutes,
        seconds,
        actions: tokens.slice(3)
    };
}

function shouldApplyAutoTimerCorrection(parsed, rawInput, nowPerf = performance.now()) {
    if (!parsed || !state.autoTimerLastUse) {
        return false;
    }
    if (nowPerf - state.autoTimerLastUse.perfNow > AUTO_TIMER_CORRECTION_LIMIT_MS) {
        return false;
    }
    return true;
}

function applySearchResultAutoTimerCorrection(seed) {
    const lastUse = state.autoTimerLastUse;
    if (!lastUse) {
        return false;
    }

    const seedTimeUnits = computeSeedTimeUnits(seed);
    const seedSeconds = Number(seedTimeUnits) / AUTO_TIMER_FRACTION_SCALE;

    const toolSecondsAtUse =
        Number(lastUse.preciseTimeUnitsAtUse) / AUTO_TIMER_FRACTION_SCALE;

    const periodSeconds = normalizeOffsetSeconds(lastUse.offsetSecondsAtUse);
    const correctedSeconds = periodSeconds > 0
        ? seedSeconds + Math.round((toolSecondsAtUse - seedSeconds) / periodSeconds) * periodSeconds
        : seedSeconds;

    setAutoTimerAnchorSeconds(correctedSeconds, lastUse.perfNow);
    state.autoTimerCorrectionCount += 1;

    const preciseTime = splitPreciseSeconds(correctedSeconds);
    setAutoTimerFractionDigits(
        preciseTime.fraction,
        formatActionTime(preciseTime.wholeSeconds)
    );

    return true;
}

function applyLanguage(lang) {
    const config = window.APP_CONFIG || {};
    const dictionary = config.i18n ? config.i18n[lang] : null;
    if (!dictionary) {
        return;
    }
    document.querySelectorAll("[data-i18n]").forEach((node) => {
        const key = node.getAttribute("data-i18n");
        if (dictionary[key]) {
            node.textContent = dictionary[key];
        }
    });
    if (state.emulatorStatusKey) {
        ui.emulatorStatus.textContent = dictionary[state.emulatorStatusKey] || state.emulatorStatusKey;
    }
    document.documentElement.dataset.lang = lang;
    document.documentElement.lang = lang;
    state.lang = lang;
    localStorage.setItem("dq9Lang", lang);
    renderSeedMemos(state.memoList);
    setAutoTimerStatusText(
        state.autoTimerAnchor ? getAutoTimerStatusReadyText() : getAutoTimerStatusIdleText()
    );
    updateAutoTimerPreview();
    renderShortcutSettings();
}

function applyTheme(theme) {
    document.documentElement.dataset.theme = theme;
    state.theme = theme;
    localStorage.setItem("dq9Theme", theme);
}

function initSettings() {
    const config = window.APP_CONFIG || {};
    const themes = config.themes || [];
    ui.themeSelect.innerHTML = "";
    themes.forEach((themeName) => {
        const option = document.createElement("option");
        option.value = themeName;
        option.textContent = themeName;
        ui.themeSelect.appendChild(option);
    });
    ui.themeSelect.value = state.theme;
    ui.langSelect.value = state.lang;
    ui.preloadToggle.checked = state.preload;
    state.shortcutBindings = loadShortcutBindings();
    applyLanguage(state.lang);
    applyTheme(state.theme);
    setOffsetSeconds(loadOffsetSeconds());
    setSearchRangeSeconds(loadSearchRangeSeconds());
    clearAutoTimerAnchor();
    renderShortcutSettings();
}

async function runSearch() {
    if (state.running || !state.active) {
        return;
    }

    const threads = Math.max(1, Math.min(32, parseIntValue(ui.threads) || 4));
    const rawInput = ui.actionInput.value;
    const input = rawInput.trim();
    if (!input) {
        appendLog("input is empty");
        return;
    }

    const parsed = parseInput(input);
    if (parsed.error) {
        appendLog(parsed.error);
        return;
    }
    restoreAutoTimerFractionForInput(input);
    const preciseTimeUnits = getPreciseSearchTimeUnits(parsed, input);
    const runStartedAtPerf = performance.now();
    const shouldCorrectAutoTimer = shouldApplyAutoTimerCorrection(parsed, input, runStartedAtPerf);

    const {start, end} = computeSeedRange(
        parsed.hours,
        parsed.minutes,
        parsed.seconds,
        state.offsetSeconds,
        preciseTimeUnits
    );
    const ranges = splitRange(start, end, threads);
    if (!ranges.length) {
        appendLog("invalid time range");
        return;
    }

    clearOutputs();
    state.running = true;
    ui.runButton.disabled = true;
    updateAutoTimerButtons();
    setSeedState("running", 0);
    appendLog(`range ${start} -> ${end} using ${ranges.length} workers`);
    const inputActions = parsed.actions.join(" ");

    const moduleUrl = new URL(state.active.module, window.location.href).toString();
    const payload = await ensureModulePayload(moduleUrl);
    await ensureWorkerScript();
    const clients = ranges.map(() => createWorkerClient("worker.js"));

    try {
        await Promise.all(clients.map((client) => client.ready()));

        const prepResults = await Promise.all(
            clients.map((client) =>
                client.call("prepare", {
                    moduleKey: moduleUrl,
                    moduleSource: payload.jsText,
                    input: inputActions
                })
            )
        );


        const count = prepResults[0].count || 0;
        if (!count) {
            appendLog(prepResults[0].error || "input parse failed");
            setSeedState("error");
            return;
        }
        if (count > 1) {
            appendLog(`multiple combinations detected (${count}), using first`);
        }

        const brutePromises = clients.map((client, index) =>
            client.call("bruteforce", {
                moduleUrl,
                resultIndex: 0,
                startSeed: ranges[index].start.toString(),
                endSeed: ranges[index].end.toString(),
                moduleKey: moduleUrl,
                moduleSource: payload.jsText,
            })
        );

        const tracked = brutePromises.map((promise, index) =>
            promise.then((result) => ({index, result}))
        );

        const pending = new Set(tracked);
        let foundSeed = "";
        let foundIndex = -1;
        let bestSpeed = null;
        let bestElapsed = null;
        let bestTurns = null;
        let totalFound = 0;
        while (pending.size) {
            const next = await Promise.race(Array.from(pending));
            pending.delete(tracked[next.index]);
            if (typeof next.result.found === "number") {
                totalFound += next.result.found;
                setSeedState("running", totalFound);
            }
            if (next.result.turns) {
                const turns = BigInt(next.result.turns);
                const elapsedMs = BigInt(next.result.elapsedMs || 1);
                const speed = (turns * 1000n) / (elapsedMs * 10000n);
                appendLog(
                    `worker ${next.index + 1} turns=${turns} elapsed=${elapsedMs}ms speed=${speed} (m turns/s)`
                );
                if (!bestSpeed || speed > bestSpeed) {
                    bestSpeed = speed;
                    bestElapsed = elapsedMs;
                    bestTurns = turns;
                }
            }
            if (next.result.seed) {
                if (foundSeed) {
                    foundSeed = ""; // 複数ヒット検出
                } else {
                    foundSeed = next.result.seed;
                    foundIndex = next.index;
                }
            }
        }

        if (!foundSeed || totalFound !== 1) {
            setSeedState("notFound", totalFound);
            if (totalFound > 1) {
                appendLog("multiple seeds found");
            }
            appendLog("seed not found");
            return;
        }

        setSeedValues(foundSeed, parsed, preciseTimeUnits);
        setSeedState("found", totalFound);
        appendLog(`seed found ${foundSeed}`);
        if (bestSpeed && bestElapsed && bestTurns) {
            ui.seedSpeed.textContent = `${bestSpeed} (m turns/s)`;
            ui.seedElapsed.textContent = `${bestElapsed} ms`;
        }

        clients.forEach((client, index) => {
            if (index !== foundIndex) {
                client.terminate();
            }
        });

        const searchClient = clients[foundIndex];
        let searchResult = await searchClient.call("search", {
            moduleUrl,
            resultIndex: 0,
            seed: foundSeed,
            numThreads: threads,
            dropbug: true,
            moduleKey: moduleUrl,
            moduleSource: payload.jsText,
        });

        if (searchResult.type === "error") {
            console.info(searchResult.message);
            throw new Error(searchResult.message);
        }

        if (searchResult.output.startsWith("SearchRequest failed")) {
            searchResult = await searchClient.call("search", {
                moduleUrl,
                resultIndex: 0,
                seed: foundSeed,
                numThreads: threads,
                dropbug: false,
                moduleKey: moduleUrl,
                moduleSource: payload.jsText,
            });
        }

        ui.dumpOutput.value = searchResult.output;

        const turns = BigInt(searchResult.turns);
        const elapsedMs = BigInt(searchResult.elapsedMs || 1);
        const speed = (turns * 1000n) / (elapsedMs * 10000n);
        appendLog(
            `searcher worker turns=${turns} elapsed=${elapsedMs}ms speed=${speed} (m turns/s)`
        );

        const driftText = computeSeedDriftText(BigInt(foundSeed), parsed, preciseTimeUnits);
        if (shouldCorrectAutoTimer) {
            applySearchResultAutoTimerCorrection(BigInt(foundSeed));
        }
        recordSeedMemo(parsed, input, driftText);

        appendLog("dump table ready");
        searchClient.terminate();
    } catch (err) {
        appendLog(err ? String(err) : "run failed");
        setSeedState("error");
    } finally {
        clients.forEach((client) => client.terminate());
        state.running = false;
        ui.runButton.disabled = false;
        updateAutoTimerButtons();
    }
}

let becameActive = false;
let movementTimer = null;
const SAFE_TIME = 3000;

function setMovementTimerOnce() {
    if (movementTimer !== null) {
        return;
    }
    movementTimer = setTimeout(() => {
        becameActive = false;
        movementTimer = null;
    }, SAFE_TIME);
}

function throttle(fn, wait) {
    let lastTime = 0;
    return function throttled(...args) {
        const now = Date.now();
        if (now - lastTime >= wait) {
            lastTime = now;
            fn.apply(this, args);
        }
    };
}

function movePointerGuard() {
    if (!becameActive) {
        return;
    }
    setMovementTimerOnce();
}

function isPointerUnsafe() {
    return becameActive;
}

function initPointerSafety() {
    const throttledMove = throttle(movePointerGuard, 200);
    window.addEventListener("mousemove", throttledMove);
    document.addEventListener("mouseenter", () => {
        state.shortcutPointerInsideWindow = true;
        becameActive = true;
        movementTimer = null;
        setMovementTimerOnce();
    });
    document.addEventListener("mouseleave", () => {
        state.shortcutPointerInsideWindow = false;
    });
}

function applyAutoTimerToInput() {
    const predictedSeconds = computeAutoTimerAppliedSeconds();
    if (predictedSeconds === null) {
        return;
    }
    const suffix = extractActionSuffix(ui.actionInput.value);
    const perfNow = performance.now();
    const preciseTime = splitPreciseSeconds(predictedSeconds);
    const timeText = formatActionTime(preciseTime.wholeSeconds);
    ui.actionInput.value = `${timeText}${suffix ? ` ${suffix}` : " "}`;
    state.autoTimerAppliedPrefix = `${timeText} `;
    state.autoTimerLastUse = {
        perfNow,
        inputText: ui.actionInput.value,
        timeText,
        fractionDigits: preciseTime.fraction,
        preciseTimeUnitsAtUse: BigInt(
            (preciseTime.wholeSeconds + normalizeOffsetSeconds(state.offsetSeconds)) * AUTO_TIMER_FRACTION_SCALE
            + preciseTime.fraction
        ),
        offsetSecondsAtUse: state.offsetSeconds
    };
    setAutoTimerFractionDigits(preciseTime.fraction, timeText);
    restartAutoTimerFractionHideTimer();
    setAutoTimerResetConfirmVisible(false);
    ui.actionInput.focus();
}

function applyVisionBattleFormatText(formatText) {
    if (!ui.actionInput) {
        return false;
    }
    const formatted = String(formatText || "").trim();
    if (!formatted) {
        return false;
    }
    const currentValue = ui.actionInput.value || "";
    const prefixMatch = currentValue.match(/^\s*(\d+)\s+(\d+)\s+(\d+)(?=\s|$)/);
    const prefix = prefixMatch ? `${prefixMatch[1]} ${prefixMatch[2]} ${prefixMatch[3]}` : "? ? ?";
    ui.actionInput.value = `${prefix} ${formatted}`;
    ui.actionInput.dispatchEvent(new Event("input", {bubbles: true}));
    focusActionInputAtTop();
    return true;
}

window.applyVisionBattleFormat = applyVisionBattleFormatText;

ui.emulatorSelect.addEventListener("change", (event) => {
    setActiveEmulator(Number(event.target.value));
    ui.actionInput.value = "";
    state.autoTimerAppliedPrefix = "";
    state.autoTimerLastUse = null;
    clearAutoTimerFractionHideTimer();
    setAutoTimerFractionDigits(null, "");
});

ui.runButton.addEventListener("click", () => {
    focusActionInputAtTop();
    runSearch();
});

ui.actionInput.addEventListener("keydown", (event) => {
    if (event.key === "Enter" && !event.shiftKey) {
        event.preventDefault();
        focusActionInputAtTop();
        runSearch();
    }
});

if (ui.autoTimerFractionInput) {
    ui.autoTimerFractionInput.addEventListener("input", () => {
        const digits = normalizeFractionDigits(ui.autoTimerFractionInput.value);
        if (digits === null || digits === 0) {
            setAutoTimerFractionDigits(null, "");
            return;
        }
        ui.autoTimerFractionInput.value = formatFractionDigits(digits);
        updateAutoTimerFractionVisibility();
    });
}

if (ui.autoTimerStartButton) {
    ui.autoTimerStartButton.addEventListener("click", () => {
        if (isPointerUnsafe() && state.autoTimerAnchor != null) {
            setAutoTimerStatusText(
                t("autoTimerStatusUnsafe", "Pointer just re-entered the window. Wait 1.5s and try again.")
            );
            return;
        }
        startManualAutoTimer();
    });
}

if (ui.autoTimerUseButton) {
    ui.autoTimerUseButton.addEventListener("click", () => {
        applyAutoTimerToInput();
    });
}

if (ui.autoTimerResetButton) {
    ui.autoTimerResetButton.addEventListener("click", () => {
        if (!state.autoTimerAnchor) {
            return;
        }
        if (isPointerUnsafe()) {
            setAutoTimerStatusText(
                t("autoTimerStatusUnsafe", "Pointer just re-entered the window. Wait 1.5s and try again.")
            );
            return;
        }
        setAutoTimerStatusText(getAutoTimerStatusReadyText());
        setAutoTimerResetConfirmVisible(true);
    });
}

if (ui.autoTimerResetConfirmButton) {
    ui.autoTimerResetConfirmButton.addEventListener("click", () => {
        if (isPointerUnsafe()) {
            setAutoTimerStatusText(
                t("autoTimerStatusUnsafe", "Pointer just re-entered the window. Wait 1.5s and try again.")
            );
            return;
        }
        clearAutoTimerAnchor();
    });
}

if (ui.autoTimerResetCancelButton) {
    ui.autoTimerResetCancelButton.addEventListener("click", () => {
        setAutoTimerResetConfirmVisible(false);
        setAutoTimerStatusText(
            state.autoTimerAnchor ? getAutoTimerStatusReadyText() : getAutoTimerStatusIdleText()
        );
    });
}

ui.themeSelect.addEventListener("change", (event) => {
    applyTheme(event.target.value);
});

ui.langSelect.addEventListener("change", (event) => {
    applyLanguage(event.target.value);
});

ui.preloadToggle.addEventListener("change", (event) => {
    state.preload = event.target.checked;
    localStorage.setItem("dq9Preload", state.preload ? "1" : "0");
    if (state.preload && state.active) {
        const moduleUrl = new URL(state.active.module, window.location.href).toString();
        preloadModule(moduleUrl);
    }
});

if (ui.memoCopyMarkdown) {
    ui.memoCopyMarkdown.addEventListener("click", (event) => {
        event.preventDefault();
        if (!state.memoList.length) {
            return;
        }
        copyText(memoToMarkdown(state.memoList));
        appendLog("memo markdown copied");
    });
}

if (ui.memoCopyCsv) {
    ui.memoCopyCsv.addEventListener("click", (event) => {
        event.preventDefault();
        if (!state.memoList.length) {
            return;
        }
        copyText(memoToCsv(state.memoList));
        appendLog("memo csv copied");
    });
}

if (ui.memoTableBody) {
    ui.memoTableBody.addEventListener("click", (event) => {
        const target = event.target.closest("a");
        if (!target) {
            return;
        }
        const inputId = target.dataset.copyInput;
        const urlId = target.dataset.copyUrl;
        if (!inputId && !urlId) {
            return;
        }
        event.preventDefault();
        const entry = state.memoList.find((item) => item.id === (inputId || urlId));
        if (!entry) {
            return;
        }
        if (inputId) {
            copyText(entry.input);
            appendLog("memo input copied");
        } else if (urlId) {
            copyText(buildOverrideUrl(entry));
            appendLog("memo url copied");
        }
    });

    ui.memoTableBody.addEventListener("input", (event) => {
        const target = event.target;
        if (!target.classList.contains("memo-note-input")) {
            return;
        }
        const entryId = target.dataset.memoId;
        const entry = state.memoList.find((item) => item.id === entryId);
        if (!entry) {
            return;
        }
        entry.memo = target.value;
        saveSeedMemos(state.memoList);
    });
}

if (ui.offsetSeconds) {
    ui.offsetSeconds.addEventListener("change", (event) => {
        setOffsetSeconds(event.target.value);
    });
}

if (ui.searchRangeSeconds) {
    ui.searchRangeSeconds.addEventListener("input", (event) => {
        if (SEARCH_RANGE_SECONDS_PARTIAL_PATTERN.test(event.target.value)) {
            return;
        }
        event.target.value = formatSearchRangeSeconds(state.searchRangeSeconds);
    });
    ui.searchRangeSeconds.addEventListener("change", (event) => {
        setSearchRangeSeconds(event.target.value);
    });
}

loadManifest();
initSettings();
initMemoLedger();
initShortcutSettings();
initPointerSafety();


let composing = false;

const vowelMap = {
    "あ": "a",
    "い": "i",
    "う": "u",
    "え": "e",
    "お": "o",
    "ア": "a",
    "イ": "i",
    "ウ": "u",
    "エ": "e",
    "オ": "o"
};

function normalizeActionInput(value) {
    // 1. 全角英数字などを半角へ
    let v = value.normalize("NFKC");

    // 2. 母音だけ名指し変換
    v = v.replace(/[あいうえおアイウエオ]/g, ch => vowelMap[ch]);

    return v;
}

ui.actionInput.addEventListener("compositionstart", () => {
    composing = true;
});

ui.actionInput.addEventListener("compositionend", () => {
    composing = false;
    ui.actionInput.value = normalizeActionInput(ui.actionInput.value);
    restoreAutoTimerFractionForInput(ui.actionInput.value);
});

ui.actionInput.addEventListener("input", () => {
    if (composing) return;
    ui.actionInput.value = normalizeActionInput(ui.actionInput.value);
    restoreAutoTimerFractionForInput(ui.actionInput.value);
});

// テキスト選択は許可したまま、テキストのD&D（ドラッグで文字が移動/外部へD&D/ドロップで置換）だけ無効化する
function disableTextDnDForElement(el) {
    if (!el) return;

    // 既存機能を壊さないため、対象要素にだけ局所的に適用する
    el.addEventListener("dragstart", (event) => {
        // selection drag による「文字が移動する」挙動を止める
        event.preventDefault();
        event.stopPropagation();
    });

    // ドロップでtextareaの内容が置き換わる/挿入されるのを止める
    el.addEventListener("drop", (event) => {
        event.preventDefault();
        event.stopPropagation();
    });

    // drop を許可しない場合、dragover 側でも preventDefault しておくと安定する
    el.addEventListener("dragover", (event) => {
        event.preventDefault();
        event.stopPropagation();
    });
}

function initDisableTextDnD() {
    // 主要な textarea 類
    disableTextDnDForElement(ui.actionInput);
    disableTextDnDForElement(ui.dumpOutput);
    disableTextDnDForElement(ui.logOutput);

    // Seed Memo のメモ入力欄（動的生成）も対象にする（既存の input イベントとは競合しない）
    if (ui.memoTableBody) {
        ui.memoTableBody.addEventListener("dragstart", (event) => {
            const target = event.target;
            if (target && target.classList && target.classList.contains("memo-note-input")) {
                event.preventDefault();
                event.stopPropagation();
            }
        });
        ui.memoTableBody.addEventListener("drop", (event) => {
            const target = event.target;
            if (target && target.classList && target.classList.contains("memo-note-input")) {
                event.preventDefault();
                event.stopPropagation();
            }
        });
        ui.memoTableBody.addEventListener("dragover", (event) => {
            const target = event.target;
            if (target && target.classList && target.classList.contains("memo-note-input")) {
                event.preventDefault();
                event.stopPropagation();
            }
        });
    }
}

initDisableTextDnD();
