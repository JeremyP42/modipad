/* app.js - ModiPAD web control panel (with i18n) */

/* Single version of the firmware + web UI. Keep in sync with the ?v= query
 * on style.css / app.js / locales so the browser refreshes its cache. */
const APP_VERSION = "5.3.8";

const LIBRARY_DIRS = {
    /* Row 1: SD card library */
    sd_pagebg:   ["sd:images/pages"],
    sd_btnbg:    ["sd:images/buttons/100x100", "sd:images/buttons/70x70"],
    sd_appicons: ["sd:images/icons/pages"],
    sd_btnicons: ["sd:images/icons/buttons", "sd:images/icons/actions"],
    /* Row 2: device (internal FS) copy */
    fs_pagebg:   ["images/pages"],
    fs_btnbg:    ["images/buttons/100x100", "images/buttons/70x70"],
    fs_appicons: ["images/icons/pages"],
    fs_btnicons: ["images/icons/buttons", "images/icons/actions"],
};

const ALL_DIRS = [
    "images/buttons/100x100",
    "images/buttons/70x70",
    "images/pages",
    "images/icons/buttons",
    "images/icons/pages",
    "images/icons/actions",
    "sd:images/buttons/100x100",
    "sd:images/buttons/70x70",
    "sd:images/pages",
    "sd:images/icons/buttons",
    "sd:images/icons/pages",
    "sd:images/icons/actions",
];

/* Fixed button geometry (must match ui_renderer.cpp). Buttons are square. */
const BTN_MAX = 100;
const CAPTION_H = 16;
const GAP_V = 8;
const STATUS_BAR_H = 30;
const MATRIX_OPTIONS = [
    { rows: 2, cols: 3 },
    { rows: 2, cols: 4 },
    { rows: 3, cols: 3 },
    { rows: 3, cols: 4 },
];

function clampMatrix(rows, cols) {
    rows = Math.min(3, Math.max(2, rows | 0));
    cols = Math.min(4, Math.max(3, cols | 0));
    return { rows, cols };
}

function pageMatrix(page) {
    const mx = (page && (page.matrix || page.grid)) || {};
    return clampMatrix(mx.rows || 2, mx.cols || 4);
}

function gridGeometry(rows, cols) {
    const row_h = Math.floor((320 - STATUS_BAR_H - rows * CAPTION_H - (rows + 1) * GAP_V) / rows);
    const side = Math.max(8, Math.min(BTN_MAX, row_h));
    const gap_h = Math.max(0, Math.floor((480 - cols * side) / (cols + 1)));
    const gap_v = Math.max(0, Math.floor((320 - STATUS_BAR_H - (rows * side + rows * CAPTION_H)) / (rows + 1)));
    return { side, gap_h, gap_v };
}

function buttonBgDir(rows) {
    return rows >= 3 ? "buttons/70x70" : "buttons/100x100";
}

/* ------------------------------------------------------------------ */
/* Button style presets (mirror of src/button_style.c)                */
/* ------------------------------------------------------------------ */
const STYLE_PRESETS = {
    glass:   { preset: "glass",   radius: 16, border_width: 1, border_color: "#ffffff", border_opa: 50,  shadow_width: 14, shadow_ofs_y: 6, shadow_color: "#000000", shadow_opa: 60 },
    solid:   { preset: "solid",   radius: 12, border_width: 0, border_color: "#ffffff", border_opa: 0,   shadow_width: 8,  shadow_ofs_y: 4, shadow_color: "#000000", shadow_opa: 70 },
    flat:    { preset: "flat",    radius: 10, border_width: 0, border_color: "#ffffff", border_opa: 0,   shadow_width: 0,  shadow_ofs_y: 0, shadow_color: "#000000", shadow_opa: 0 },
    rounded: { preset: "rounded", radius: 26, border_width: 1, border_color: "#ffffff", border_opa: 20,  shadow_width: 10, shadow_ofs_y: 5, shadow_color: "#000000", shadow_opa: 60 },
    sharp:   { preset: "sharp",   radius: 2,  border_width: 1, border_color: "#ffffff", border_opa: 25,  shadow_width: 6,  shadow_ofs_y: 3, shadow_color: "#000000", shadow_opa: 50 },
    neon:    { preset: "neon",    radius: 14, border_width: 2, border_color: "#00e5ff", border_opa: 100, shadow_width: 14, shadow_ofs_y: 0, shadow_color: "#00e5ff", shadow_opa: 80 },
};
const PRESETS = STYLE_PRESETS;
const STYLE_KEYS = ["glass", "solid", "flat", "rounded", "sharp", "neon"];

function presetObject(name) {
    const src = (state.config && state.config.styles && state.config.styles[name]) ||
                STYLE_PRESETS[name] || STYLE_PRESETS.glass;
    return Object.assign({}, src);
}

function defaultTextStyle() {
    return { size: 14, bold: false, color: "#ffffff",
             shadow_dir: 4, shadow_color: "#000000", shadow_size: 0, shadow_strength: 0 };
}

/* Caption (text under the button) styling — same shape as the button text. */
function defaultCaptionStyle() {
    return { size: 12, bold: false, color: "#ffffff",
             shadow_dir: 4, shadow_color: "#000000", shadow_size: 0, shadow_strength: 0 };
}

/* Seed config.styles from the built-in presets on first use. */
function ensureStyles() {
    if (!state.config.styles || typeof state.config.styles !== "object" || !Object.keys(state.config.styles).length) {
        state.config.styles = {};
        STYLE_KEYS.forEach((k) => {
            const o = Object.assign({}, STYLE_PRESETS[k]);
            o.text = defaultTextStyle();
            o.caption = defaultCaptionStyle();
            state.config.styles[k] = o;
        });
    }
    Object.keys(state.config.styles).forEach((k) => {
        if (!state.config.styles[k].text) state.config.styles[k].text = defaultTextStyle();
        if (!state.config.styles[k].caption) state.config.styles[k].caption = defaultCaptionStyle();
    });
    if (!state.config.default_style) {
        state.config.default_style =
            (state.config.defaults && state.config.defaults.button_style && state.config.defaults.button_style.preset) || "glass";
    }
}

/* ---- Styles tab ---- */
let editingStyle = null;
let FONT_SIZES = [10, 12, 14, 16, 18];

/* Font sizes are the unique Roboto sizes available on the device FS and SD. */
async function loadFontSizes() {
    const s = await getJson("/api/fonts", null);
    if (Array.isArray(s) && s.length) {
        FONT_SIZES = [...new Set(s.map(Number))].filter((n) => n > 0).sort((a, b) => a - b);
    }
    ["style-text-size", "style-caption-size", "bu-text-size", "bu-caption-size"].forEach((id) => {
        const sel = $(id);
        if (sel && !sel.options.length) {
            FONT_SIZES.forEach((n) => {
                const o = document.createElement("option");
                o.value = String(n);
                o.textContent = n + " px";
                sel.appendChild(o);
            });
        }
    });
}

/* A style is marked as used only when a page or a button actually refers to it,
 * so the "+" marker matches the per-page style shown on the Pages tab. The
 * default style is deliberately excluded - it is already flagged by "★". */
function styleIsUsed(name) {
    if (!name) return false;
    const pages = [];
    if (state.config.main_page) pages.push(state.config.main_page);
    for (const p of (state.config.pages || [])) pages.push(p);
    for (const page of pages) {
        if (!page) continue;
        if (page.button_style && page.button_style.preset === name) return true;
        for (const b of (page.buttons || [])) {
            if (b && b.style && b.style.preset === name) return true;
        }
    }
    return false;
}

function renderStylesPage() {
    ensureStyles();
    loadFontSizes();
    const list = $("styles-list");
    if (!list) return;
    list.innerHTML = "";
    Object.keys(state.config.styles).forEach((name) => {
        const st = state.config.styles[name];
        const card = document.createElement("div");
        card.className = "style-card";
        const btn = document.createElement("button");
        btn.type = "button";
        btn.className = "style-preview";
        btn.style.background = "linear-gradient(180deg,#8fa1bb,#5d6e88)";
        applyStyleCss(btn, st);
        const tx = st.text || defaultTextStyle();
        const lbl = document.createElement("span");
        lbl.textContent = "Aa";
        lbl.style.fontSize = (tx.size || 14) + "px";
        lbl.style.fontWeight = tx.bold ? "700" : "400";
        lbl.style.color = tx.color || "#ffffff";
        lbl.style.textShadow = textShadowCss(tx);
        btn.appendChild(lbl);
        btn.onclick = () => openStyleEditor(name);
        const nm = document.createElement("div");
        nm.className = "style-name";
        const used = styleIsUsed(name) ? "+ " : "";
        nm.textContent = used + presetLabel(name) + (state.config.default_style === name ? " ★" : "");
        card.appendChild(btn);
        card.appendChild(nm);
        list.appendChild(card);
    });
    if (!editingStyle || !state.config.styles[editingStyle]) {
        const defName = state.config.default_style;
        applyPreviewStyle(state.config.styles[defName] ||
            Object.assign(presetObject("glass"), { text: defaultTextStyle(), caption: defaultCaptionStyle() }));
    }
}

function openStyleEditor(name) {
    ensureStyles();
    editingStyle = name;
    const st = state.config.styles[name];
    if (!st) return;
    $("style-editor").style.display = "block";
    $("style-name").value = presetLabel(name);
    $("style-radius").value = st.radius != null ? st.radius : 12;
    $("style-border-width").value = st.border_width != null ? st.border_width : 0;
    $("style-border-color").value = st.border_color || "#ffffff";
    setRangeNum("style-border-opa", st.border_opa != null ? st.border_opa : 0);
    $("style-btn-shadow-color").value = st.shadow_color || "#000000";
    $("style-btn-shadow-size").value = st.shadow_width || 0;
    setRangeNum("style-btn-shadow-strength", st.shadow_opa != null ? st.shadow_opa : 0);
    if ($("style-btn-shadow-dir")) {
        const sdir = st.shadow_dir != null ? st.shadow_dir : ((st.shadow_ofs_y || 0) > 0 ? 7 : 4);
        $("style-btn-shadow-dir").value = String(sdir);
    }
    const tx = st.text || defaultTextStyle();
    $("style-text-bold").checked = !!tx.bold;
    $("style-text-color").value = tx.color || "#ffffff";
    $("style-shadow-color").value = tx.shadow_color || "#000000";
    $("style-shadow-size").value = tx.shadow_size || 0;
    setRangeNum("style-shadow-strength", tx.shadow_strength != null ? tx.shadow_strength : 0);
    ensureFontSelect($("style-text-size"));
    if ($("style-text-size")) $("style-text-size").value = String(tx.size || 14);
    if ($("style-shadow-dir")) $("style-shadow-dir").value = String(tx.shadow_dir != null ? tx.shadow_dir : 4);
    const cap = st.caption || defaultCaptionStyle();
    $("style-caption-bold").checked = !!cap.bold;
    $("style-caption-color").value = cap.color || "#ffffff";
    $("style-caption-shadow-color").value = cap.shadow_color || "#000000";
    $("style-caption-shadow-size").value = cap.shadow_size || 0;
    setRangeNum("style-caption-shadow-strength", cap.shadow_strength != null ? cap.shadow_strength : 0);
    ensureFontSelect($("style-caption-size"));
    if ($("style-caption-size")) $("style-caption-size").value = String(cap.size || 12);
    if ($("style-caption-shadow-dir")) $("style-caption-shadow-dir").value = String(cap.shadow_dir != null ? cap.shadow_dir : 4);
    const isDefault = state.config.default_style === name;
    $("style-delete").disabled = isDefault;
    $("style-delete-note").style.display = isDefault ? "inline" : "none";
    renderStylePreview();
}

function createStyle() {
    ensureStyles();
    let i = 1, name;
    do { name = "style_" + i++; } while (state.config.styles[name]);
    state.config.styles[name] = Object.assign(presetObject("glass"), { preset: "glass", text: defaultTextStyle(), caption: defaultCaptionStyle() });
    scheduleSave();
    renderStylesPage();
    openStyleEditor(name);
}

/* Rename a style everywhere it is referenced (default, global, pages, buttons). */
function renameStyleRefs(oldKey, newKey) {
    if (oldKey === newKey) return;
    if (state.config.default_style === oldKey) state.config.default_style = newKey;
    const g = state.config.defaults && state.config.defaults.button_style;
    if (g && g.preset === oldKey) g.preset = newKey;
    (state.config.pages || []).forEach((p) => {
        if (p.button_style && p.button_style.preset === oldKey) p.button_style.preset = newKey;
        (p.buttons || []).forEach((b) => {
            if (b.style && b.style.preset === oldKey) b.style.preset = newKey;
        });
    });
}

function saveStyle() {
    if (!editingStyle) return;
    const oldKey = editingStyle;
    const entered = ($("style-name").value || "").trim();
    /* The field shows the display name; keep the key unless the user changed it. */
    const newKey = (entered && entered !== presetLabel(oldKey)) ? entered : oldKey;
    const obj = readStyleEditor();
    if (newKey !== oldKey) {
        delete state.config.styles[oldKey];
        renameStyleRefs(oldKey, newKey);
        editingStyle = newKey;
    }
    state.config.styles[editingStyle] = obj;
    scheduleSave();
    if (window.__clearDirty) window.__clearDirty("styles");
    renderStylesPage();
    openStyleEditor(editingStyle);
    toast(t("messages.saved"));
}

function deleteStyle() {
    if (!editingStyle) return;
    if (state.config.default_style === editingStyle) {
        toast(t("styles.default_note"));
        return;
    }
    delete state.config.styles[editingStyle];
    scheduleSave();
    $("style-editor").style.display = "none";
    editingStyle = null;
    renderStylesPage();
}

/* Full copy of a style under a new name ("<name> 1"). */
function duplicateStyle() {
    if (!editingStyle) return;
    const src = state.config.styles[editingStyle];
    if (!src) return;
    const base = presetLabel(editingStyle);
    let name = base + " 1";
    let i = 1;
    while (state.config.styles[name]) { i++; name = base + " " + i; }
    const copy = JSON.parse(JSON.stringify(src));
    copy.preset = name;
    state.config.styles[name] = copy;
    scheduleSave();
    renderStylesPage();
    openStyleEditor(name);
    toast(t("messages.style_duplicated"));
}

function effectiveStyle(btn, page) {
    if (btn && btn.style && typeof btn.style === "object") {
        return Object.assign(presetObject(btn.style.preset), btn.style);
    }
    if (page && page.button_style && typeof page.button_style === "object") {
        return Object.assign(presetObject(page.button_style.preset), page.button_style);
    }
    const g = state.config.defaults && state.config.defaults.button_style;
    return Object.assign(presetObject(g && g.preset), g || {});
}

function hexToRgb(hex) {
    const h = String(hex || "#000000").replace("#", "");
    const full = h.length === 3 ? h.split("").map((c) => c + c).join("") : h;
    const v = parseInt(full, 16) || 0;
    return { r: (v >> 16) & 255, g: (v >> 8) & 255, b: v & 255 };
}

function rgba(hex, opaPercent) {
    const c = hexToRgb(hex);
    const a = Math.max(0, Math.min(100, opaPercent == null ? 100 : opaPercent)) / 100;
    return `rgba(${c.r},${c.g},${c.b},${a})`;
}

function applyStyleCss(el, st) {
    el.style.borderRadius = (st.radius != null ? st.radius : 14) + "px";
    if (st.border_width > 0) {
        el.style.border = `${st.border_width}px solid ${rgba(st.border_color, st.border_opa)}`;
    } else {
        el.style.border = "none";
    }
    if (st.shadow_width > 0) {
        let dx = 0, dy = 0;
        if (st.shadow_dir == null) {
            dy = st.shadow_ofs_y || 0;
        } else if (st.shadow_dir !== 4) {
            const m = Math.abs(st.shadow_ofs_y || 0) || 4;
            dx = ((st.shadow_dir % 3) - 1) * m;
            dy = (Math.floor(st.shadow_dir / 3) - 1) * m;
        }
        el.style.boxShadow = `${dx}px ${dy}px ${st.shadow_width}px ${rgba(st.shadow_color, st.shadow_opa)}`;
    } else {
        el.style.boxShadow = "none";
    }
}

function presetLabel(name) {
    const key = "buttons.preset_" + (name || "glass");
    const s = t(key);
    /* Custom styles have no i18n entry; show their name as-is. */
    return (s === key) ? (name || "glass") : s;
}

/* CSS text-shadow string for a style's text block. Direction 4 (centre) means
 * a shadow all around the text (360 deg => eight offsets). */
function textShadowCss(tx) {
    if (!tx || !(tx.shadow_size > 0 && tx.shadow_strength > 0)) {
        return "none";
    }
    const col = rgba(tx.shadow_color, tx.shadow_strength);
    const s = tx.shadow_size;
    if (tx.shadow_dir === 4) {
        const dirs = [[-1, -1], [0, -1], [1, -1], [-1, 0], [1, 0], [-1, 1], [0, 1], [1, 1]];
        return dirs.map(([dx, dy]) => `${dx * s}px ${dy * s}px ${s}px ${col}`).join(",");
    }
    const dx = ((tx.shadow_dir % 3) - 1) * s;
    const dy = (Math.floor(tx.shadow_dir / 3) - 1) * s;
    return `${dx}px ${dy}px ${s}px ${col}`;
}

function getEffectiveStyle(btn) {
    return Object.assign({}, effectiveStyle(btn, currentPage()));
}


function getCurrentButton() {
    const page = currentPage();
    if (!page || state.editingButtonIndex < 0) return null;
    return page.buttons[state.editingButtonIndex];
}



const state = {
    settings: {},
    config: { pages: [] },
    currentPageIndex: -1,
    previewPageIndex: 0,
    editingButtonIndex: -1,
    selectedCell: null,
    buttonClipboard: null,
    issues: [],
    assetNames: {},
    category: "sd_pagebg",
    imageIndex: {},
    pickImage: "",
    pickIcon: "",    iconCache: null,
    saveTimer: null,
    lang: "en",
    translations: {},
    macroSteps: [],
    obsPickIcon: "",
    syncNeeded: false,
};
const MEDIA_OPTIONS = ["PLAY_PAUSE", "NEXT", "PREV", "VOL_UP", "VOL_DOWN", "MUTE"];
const STEP_TYPES = ["key", "text", "multimedia", "delay"];

const $ = (id) => document.getElementById(id);

/* ------------------------------------------------------------------ */
/* i18n                                                               */
/* ------------------------------------------------------------------ */
function t(key, params) {
    let value = state.translations;
    for (const k of key.split(".")) {
        if (value && value[k] !== undefined) {
            value = value[k];
        } else {
            return key;
        }
    }
    if (typeof value !== "string") return key;
    if (params) {
        value = value.replace(/\{(\w+)\}/g, (m, name) => (params[name] !== undefined ? params[name] : m));
    }
    return value;
}

function applyTranslations() {
    document.querySelectorAll("[data-i18n]").forEach((el) => {
        const key = el.getAttribute("data-i18n");
        const translation = t(key);
        if (translation && translation !== key) {
            el.textContent = translation;
        }
    });
    document.querySelectorAll("[data-i18n-placeholder]").forEach((el) => {
        const key = el.getAttribute("data-i18n-placeholder");
        const translation = t(key);
        if (translation && translation !== key) {
            el.placeholder = translation;
        }
    });
    const title = t("app.title");
    if (title && title !== "app.title") document.title = title;
}

async function loadTranslations(lang) {
    try {
        const res = await fetch(`/locales/${lang}.json?v=5.3.8`);
        if (!res.ok) throw new Error("HTTP " + res.status);
        state.translations = await res.json();
        state.lang = lang;
        window.ARM_CONFIRM = (lang === "ru") ? "Точно?" : "Sure?";
        localStorage.setItem("lang", lang);
        applyTranslations();
    } catch (e) {
        if (lang !== "en") {
            await loadTranslations("en");
        } else {
            console.error("Failed to load translations", e);
        }
    }
}

function markLangButtons() {
    document.querySelectorAll(".lang-btn").forEach((btn) => {
        btn.classList.toggle("active", btn.dataset.lang === state.lang);
    });
}

function changeLanguage(lang) {
    if (lang === state.lang) return;
    loadTranslations(lang).then(() => {
        markLangButtons();
        refreshDynamicText();
    });
}

function refreshDynamicText() {    renderPagesList();
    updateActionOptions();
    renderPreview();
    loadAbout();
    if ($("library-section") && $("library-section").classList.contains("active")) {
        loadLibrary(state.category);
    }
}

/* ------------------------------------------------------------------ */
/* Utilities                                                          */
/* ------------------------------------------------------------------ */
function toast(message) {
    let el = document.querySelector(".toast");
    if (!el) {
        el = document.createElement("div");
        el.className = "toast";
        el.setAttribute("role", "status");
        el.setAttribute("aria-live", "polite");
        document.body.appendChild(el);
    }
    el.textContent = message;
    el.classList.add("show");
    clearTimeout(el._timer);
    el._timer = setTimeout(() => el.classList.remove("show"), 2200);
}

async function getJson(url, fallback) {
    try {
        const res = await fetch(url);
        if (!res.ok) return fallback;
        return await res.json();
    } catch (e) {
        return fallback;
    }
}

async function postJson(url, body) {
    const res = await fetch(url, {
        method: "POST",
        headers: { "Content-Type": "application/json" },
        body: typeof body === "string" ? body : JSON.stringify(body),
    });
    return res.ok;
}

function scheduleSave() {
    clearTimeout(state.saveTimer);
    state.saveTimer = setTimeout(saveConfig, 500);
}

async function saveConfig() {
    const ok = await postJson("/api/config", JSON.stringify(state.config));
    if (!ok) {
        toast(t("messages.save_config_failed"));
    } else if (window.__clearDirty) {
        const act = document.querySelector(".settings-section.active");
        if (act) window.__clearDirty(act.id.replace("-section", ""));
    }
    refreshIssues();
    return ok;
}

function imgUrl(name) {
    if (!name) return "";
    const dir = state.imageIndex[name];
    if (typeof dir === "string" && dir.indexOf("sd:") === 0) {
        return `/sdimages/${dir.slice(3)}/${name}`;
    }
    let d = dir;
    if (typeof d === "string" && d.indexOf("images/") === 0) {
        d = d.slice("images/".length);
    }
    return d ? `/images/${d}/${name}` : `/images/${name}`;
}

/* Build a display URL for a file in a library dir (dir = "images/<sub>"). */
function assetUrl(dir, name) {
    if (dir.indexOf("sd:") === 0) return `/sdimages/${dir.slice(3)}/${name}`;
    const d = dir.indexOf("images/") === 0 ? dir.slice("images/".length) : dir;
    return `/images/${d}/${name}`;
}

async function buildImageIndex() {
    const index = {};
    const names = {};
    for (const dir of ALL_DIRS) {
        const items = await getJson(`/api/images?dir=${encodeURIComponent(dir)}`, []);
        for (const item of items) {
            if (!item.name) continue;
            names[item.name] = true; /* every asset name, for validation */
            if (!item.dir && index[item.name] === undefined) {
                index[item.name] = dir;
            }
        }
    }
    state.imageIndex = index;
    state.assetNames = names;
}

/* Older UI versions created new buttons with a placeholder background image
 * (gradient_blue.png) that is not shipped, which raised a bogus "missing image"
 * warning. Replace it with the equivalent native gradient. */
function migrateConfig() {
    const fix = (b) => {
        const bg = b && b.background;
        if (bg && bg.type === "image" && bg.image === "gradient_blue.png") {
            b.background = { type: "gradient", color: "#1E3A8A", color2: "#0EA5E9", direction: "vertical" };
        }
    };
    if (state.config.main_page) (state.config.main_page.buttons || []).forEach(fix);
    (state.config.pages || []).forEach((p) => (p.buttons || []).forEach(fix));
}

/* Config validation: friendly, non-blocking checks shown in the banner. */
function validateConfig() {
    const issues = [];
    const cfg = state.config || {};
    const pages = cfg.pages || [];
    const main = cfg.main_page;
    if (!main && pages.length === 0) {
        issues.push(t("issues.no_pages"));
        return issues;
    }
    const pageIds = new Set(pages.map((p) => p.id));
    const known = (name) => name && state.assetNames && state.assetNames[name];
    const checkImg = (name, where, what) => {
        if (name && !known(name)) {
            issues.push(t("issues.missing_image", { name, where, what }));
        }
    };
    const checkPage = (page, label) => {
        if (!page) return;
        if ((page.buttons || []).length === 0) {
            issues.push(t("issues.empty_page").replace("{name}", label));
        }
        const bg = page.background || {};
        if (bg.type === "image") checkImg(bg.image, label, t("issues.what_page_bg"));
        (page.buttons || []).forEach((b) => {
            const bname = b.caption || b.id || "";
            if (b.icon) checkImg(b.icon, label, t("issues.what_button_icon") + (bname ? ": " + bname : ""));
            const bb = b.background || {};
            if (bb.type === "image") checkImg(bb.image, label, t("issues.what_button_bg") + (bname ? ": " + bname : ""));
            if (b.type === "page_link") {
                const tgt = b.target_page;
                if (tgt && tgt !== "__HOME__" && !pageIds.has(tgt)) {
                    issues.push(t("issues.bad_link").replace("{name}", label).replace("{target}", tgt));
                }
            }
        });
    };
    if (main) checkPage(main, t("main_page.title"));
    pages.forEach((p) => checkPage(p, p.display_name || p.name || p.id));
    return issues;
}

function refreshIssues() {
    const issues = validateConfig();
    state.issues = issues;
    const el = $("issues-banner");
    if (!el) return;
    if (!issues.length) {
        el.style.display = "none";
        el.innerHTML = "";
        return;
    }
    el.style.display = "block";
    el.innerHTML = `<strong>${t("issues.title")} (${issues.length})</strong><ul>` +
        issues.slice(0, 20).map((s) => `<li>${s}</li>`).join("") + "</ul>";
}

/* ------------------------------------------------------------------ */
/* Navigation                                                         */
/* ------------------------------------------------------------------ */
function showSection(name) {
    document.querySelectorAll(".settings-section").forEach((s) => s.classList.remove("active"));
    document.querySelectorAll(".nav-btn").forEach((b) => b.classList.toggle("active", b.dataset.section === name));
    const section = $(`${name}-section`);
    if (section) section.classList.add("active");
    if (name === "preview") renderPreview();
    if (name === "library") loadLibrary(state.category);    if (name === "about-device") loadAbout();
    if (name === "pages") renderStyleManager();
    if (name === "styles") renderStylesPage();
    if (name === "system") {
        populateGlobalStyleSelect();
        applyConfigToUI();
        loadBackupList();
    }
    if (window.__fitCanvases) window.__fitCanvases();
}

/* ------------------------------------------------------------------ */
/* System settings                                                    */
/* ------------------------------------------------------------------ */
function applySettingsToUI() {
    const s = state.settings;
    $("brightness").value = s.brightness ?? 80;
    $("brightness-value").textContent = (s.brightness ?? 80) + "%";
    $("sound-enabled").checked = s.sound_enabled ?? false;
    if ($("radio-mode")) $("radio-mode").value = String(s.radio_mode ?? 0);
    $("sleep-timeout").value = String(s.sleep_timeout ?? 300);
    if ($("panel-visible")) $("panel-visible").checked = s.status_bar_visible ?? true;
    if ($("panel-transparency")) {
        $("panel-transparency").value = String(s.status_bar_transparency ?? 0);
        if ($("panel-transparency-value")) $("panel-transparency-value").textContent = (s.status_bar_transparency ?? 0) + "%";
    }
    if ($("show-stats")) $("show-stats").checked = s.show_stats ?? false;
    applyPanelInteraction();
    if (window.setUiBrightness) window.setUiBrightness($("brightness").value);
    if (window.__fillRanges) window.__fillRanges();
    if ($("ota-sd-state")) otaStatus();
}

function updateBrightness(value) {
    state.settings.brightness = Number(value);
    $("brightness-value").textContent = value + "%";
}

function updateSound() {
    state.settings.sound_enabled = $("sound-enabled").checked;
}

/* FPS/CPU is inside the bar: only usable while the panel is shown; hiding the
 * panel also clears the FPS/CPU checkbox. */
function applyPanelInteraction() {
    const stats = $("show-stats");
    if (!stats) return;
    const vis = $("panel-visible") ? $("panel-visible").checked : true;
    if (!vis) {
        stats.checked = false;
        stats.disabled = true;
        state.settings.show_stats = false;
    } else {
        stats.disabled = false;
    }
}

function updatePanelVisible() {
    state.settings.status_bar_visible = $("panel-visible").checked;
    applyPanelInteraction();
    renderPreview();
    renderGridCanvas();
}

function updatePanelTransparency(value) {
    state.settings.status_bar_transparency = Number(value);
    if ($("panel-transparency-value")) $("panel-transparency-value").textContent = value + "%";
    renderPreview();
    renderGridCanvas();
}

function updateShowStats() {
    state.settings.show_stats = $("show-stats").checked;
    renderPreview();
}

/* Single radio mode: 0 = BLE, 1 = Wi-Fi AP, 2 = Wi-Fi client (OBS). */
function onRadioModeChange() {
    const m = Number($("radio-mode").value) || 0;
    state.settings.radio_mode = m;
    state.settings.wifi_enabled = (m !== 0);
    state.settings.ble_enabled = (m === 0);
    const net = ensureNetworkConfig();
    net.mode = (m === 2) ? "sta" : "ap";
    scheduleSave();
}

function updateSleepTimeout() {
    state.settings.sleep_timeout = Number($("sleep-timeout").value);
}

async function saveSystemSettings() {
    const ok = await postJson("/api/settings", state.settings);
    if (ok && window.__clearDirty) window.__clearDirty("system");
    toast(ok ? t("messages.saved") : t("messages.save_settings_failed"));
}

/* Global Save (header): commits the active editor (button / style) and the
 * system settings + config in one go, so there is a single save button. */
async function saveAll() {
    const active = document.querySelector(".settings-section.active");
    const sec = active ? active.id.replace("-section", "") : "";
    if (sec === "buttons") {
        const ed = $("button-editor");
        if (ed && ed.style.display !== "none" && state.editingButtonIndex >= 0) {
            await saveButton();
        }
    } else if (sec === "styles") {
        const ed = $("style-editor");
        if (editingStyle && ed && ed.style.display !== "none") {
            saveStyle();
        }
    }
    await saveSystemSettings();
    if (sec !== "buttons") await saveConfig();
    if (window.__clearDirty) { window.__clearDirty(sec); window.__clearDirty("system"); }
    toast(t("messages.saved"));
}

/* Dark/light background switch for the style preview block. */
function togglePreviewBg(on) {
    const p = document.querySelector(".style-preview-panel");
    if (p) p.classList.toggle("light", !!on);
}

/* Same switch for the single-button preview block on the Buttons tab. */
function toggleButtonPreviewBg(on) {
    const p = document.querySelector(".button-preview-container");
    if (p) p.classList.toggle("light", !!on);
}

async function reloadDevice() {
    toast(t("messages.rebooting"));
    try {
        await fetch("/api/reload", { method: "POST" });
    } catch (e) {
        /* connection drops during reboot */
    }
}

/* ---- Diagnostics log ---- */
async function loadSysLog() {
    const el = $("sys-log");
    if (!el) return;
    el.textContent = t("system.loading");
    try {
        const res = await fetch("/api/log", { cache: "no-store" });
        el.textContent = await res.text();
        el.scrollTop = el.scrollHeight;
    } catch (e) {
        el.textContent = "error";
    }
}

/* ---- Config backup ---- */
function downloadConfig() {
    window.location.href = "/api/config";
}

function restoreConfig(input) {
    const file = input.files && input.files[0];
    if (!file) return;
    const reader = new FileReader();
    reader.onload = async () => {
        try {
            const res = await fetch("/api/config", { method: "POST", body: reader.result });
            if (res.ok) {
                toast(t("messages.saved"));
                reloadDevice();
            } else {
                toast(t("messages.save_config_failed"));
            }
        } catch (e) {
            toast(t("messages.save_config_failed"));
        }
    };
    reader.readAsText(file);
    input.value = "";
}

async function saveBackupToSd() {
    try {
        const res = await fetch("/api/backup/save", { method: "POST" });
        const data = await res.json().catch(() => ({}));
        if (data && data.status === "ok") {
            toast(t("messages.saved") + ": " + (data.name || ""));
            loadBackupList();
        } else {
            toast(t("system.no_sd"));
        }
    } catch (e) {
        toast(t("messages.error"));
    }
}

async function loadBackupList() {
    const sel = $("backup-list");
    if (!sel) return;
    const items = await getJson("/api/backup/list", []);
    sel.innerHTML = "";
    if (!Array.isArray(items) || !items.length) {
        const o = document.createElement("option");
        o.value = "";
        o.textContent = t("system.no_files");
        sel.appendChild(o);
        return;
    }
    items.forEach((n) => {
        const o = document.createElement("option");
        o.value = n;
        o.textContent = n;
        sel.appendChild(o);
    });
}

async function importBackup() {
    const sel = $("backup-list");
    const name = sel ? sel.value : "";
    if (!name) {
        toast(t("system.no_files"));
        return;
    }
    const ok = await postJson("/api/backup/import", { name });
    toast(ok ? t("messages.rebooting") : t("messages.error"));
}

/* ---- Firmware update (OTA) ---- */
async function otaStatus() {
    const el = $("ota-sd-state");
    if (!el) return;
    const d = await getJson("/api/ota/status", { sd_update: false });
    el.textContent = d.sd_update ? t("system.sd_found") : "";
}

function uploadFirmware(input) {
    const file = input.files && input.files[0];
    if (!file) return;
    if (!confirm(t("system.fw_confirm"))) {
        input.value = "";
        return;
    }
    toast(t("system.updating"));
    fetch("/api/ota", { method: "POST", body: file })
        .then(() => toast(t("messages.rebooting")))
        .catch(() => toast(t("messages.rebooting")));
    input.value = "";
}

async function otaFromSd() {
    if (!confirm(t("system.fw_confirm"))) return;
    toast(t("system.updating"));
    const ok = await postJson("/api/ota/sd", {});
    if (ok) toast(t("messages.rebooting"));
}

/* The sync button is grey/disabled by default and turns red only when
 * something that lives in the device's own storage (page/button backgrounds,
 * icons, caption fonts) has changed. */
function setSyncNeeded(on) {
    state.syncNeeded = !!on;
    const b = $("btn-sync-fs");
    if (b) {
        b.classList.toggle("armed", state.syncNeeded);
        b.disabled = !state.syncNeeded;
    }
}

/* Copy every asset used by the current configuration from the SD card into the
 * device's own storage (so pages open fast). Separate from saving the config,
 * which happens immediately on every change. */
async function syncDeviceFs() {
    const btn = $("btn-sync-fs");
    if (btn) btn.disabled = true;
    toast(t("app.sync_running"));
    try {
        const res = await fetch("/api/fs/sync", { method: "POST" });
        const data = await res.json().catch(() => ({}));
        if (data && data.copied !== undefined) {
            toast(t("app.sync_done") + " (" + data.copied + ")");
        } else {
            toast(t("app.sync_done"));
        }
        setSyncNeeded(false);
    } catch (e) {
        toast(t("app.sync_failed"));
        setSyncNeeded(true);
    }
}

/* ---- OBS + network (stored in config.json) ---- */
function ensureNetworkConfig() {
    if (!state.config.network) {
        state.config.network = {
            mode: "ap",
            ap_ssid: "ModiPAD_Setup",
            ap_password: "12345678",
            ssid: "",
            password: "",
        };
    }
    const n = state.config.network;
    if (n.ap_ssid === undefined) n.ap_ssid = n.ssid || "ModiPAD_Setup";
    if (n.ap_password === undefined) n.ap_password = n.password || "12345678";
    if (n.ssid === undefined) n.ssid = "";
    if (n.password === undefined) n.password = "";
    return n;
}

function netMode() {
    return (Number(state.settings.radio_mode) === 2) ? "sta" : "ap";
}

function applyConfigToUI() {
    ensureStyles();
    if (!state.config.obs) state.config.obs = { host: "", port: 4455, password: "" };
    const net = ensureNetworkConfig();
    if ($("obs-host")) $("obs-host").value = state.config.obs.host || "";
    if ($("obs-port")) $("obs-port").value = state.config.obs.port || 4455;
    if ($("obs-password")) $("obs-password").value = state.config.obs.password || "";

    if ($("bt-device-name")) $("bt-device-name").value = state.config.device_name || "ModiPAD";
    if ($("net-ap-ssid")) $("net-ap-ssid").value = net.ap_ssid || "";
    if ($("net-ap-password")) $("net-ap-password").value = net.ap_password || "";
    if ($("net-password")) $("net-password").value = net.password || "";
    /* The SSID picker itself carries the selected network (no separate field). */
    setScanOptions(net.ssid ? [{ ssid: net.ssid }] : [], net.ssid);
}

/* Fill the SSID dropdown. `items` is a list of {ssid,rssi} or plain strings. */
function setScanOptions(items, selected) {
    const sel = $("net-scan-list");
    if (!sel) return;
    sel.innerHTML = "";
    let matched = false;
    (items || []).forEach((it) => {
        const ssid = typeof it === "string" ? it : it.ssid;
        if (!ssid) return;
        const o = document.createElement("option");
        o.value = ssid;
        o.textContent = (it && it.rssi !== undefined) ? `${ssid} (${it.rssi} dBm)` : ssid;
        if (selected && ssid === selected) {
            o.selected = true;
            matched = true;
        }
        sel.appendChild(o);
    });
    if (!sel.options.length) {
        const o = document.createElement("option");
        o.value = "";
        o.textContent = t("system.network_none");
        sel.appendChild(o);
    } else if (!matched) {
        sel.selectedIndex = 0;
    }
}

function updateObsConfig() {
    state.config.obs = {
        host: $("obs-host").value,
        port: Number($("obs-port").value) || 4455,
        password: $("obs-password").value,
    };
    scheduleSave();
}

/* Both Wi-Fi sections stay visible; the radio mode picks which one is used. */
function updateNetworkConfig() {
    const net = ensureNetworkConfig();
    net.mode = netMode();
    net.ap_ssid = $("net-ap-ssid") ? $("net-ap-ssid").value : net.ap_ssid;
    net.ap_password = $("net-ap-password") ? $("net-ap-password").value : net.ap_password;
    net.ssid = $("net-scan-list") ? $("net-scan-list").value : net.ssid;
    net.password = $("net-password") ? $("net-password").value : net.password;
    state.config.network = net;
    scheduleSave();
}

/* BLE device name (what Windows/Bluetooth shows when pairing). */
function updateDeviceName() {
    if (!$("bt-device-name")) return;
    const name = $("bt-device-name").value.trim();
    state.config.device_name = name || "ModiPAD";
    scheduleSave();
}

/* Scan for Wi-Fi networks and fill the SSID picker (device-side scan). */
async function requestWifiScan() {
    const sel = $("net-scan-list");
    if (!sel) return;
    const previous = sel.value;
    sel.innerHTML = `<option>${t("system.network_scanning")}</option>`;
    try {
        await fetch("/api/wifi/scan", { method: "POST" });
    } catch (e) {
        return;
    }
    for (let i = 0; i < 15; i++) {
        await new Promise((r) => setTimeout(r, 800));
        const data = await getJson("/api/wifi/scan", { running: false, networks: [] });
        if (!data.running) {
            setScanOptions(data.networks || [], previous);
            updateNetworkConfig();
            return;
        }
    }
}

function onScanPick() {
    updateNetworkConfig();
}

/* Apply the client credentials on the device right away (also saved). */
async function connectWifi() {
    updateNetworkConfig();
    const ok = await postJson("/api/wifi/apply", {
        mode: "sta",
        ssid: $("net-scan-list") ? $("net-scan-list").value : "",
        password: $("net-password") ? $("net-password").value : "",
    });
    toast(ok ? t("messages.wifi_connecting") : t("messages.error"));
}

/* ------------------------------------------------------------------ */
/* Pages                                                              */
/* ------------------------------------------------------------------ */
/* One card in the Pages list. The Main page uses index -1 and cannot be deleted. */
function buildPageCard(page, index) {
    const isMain = index === -1;
    const card = document.createElement("div");
    card.className = "page-card" + (isMain ? " page-card-main" : "");

    const header = document.createElement("div");
    header.className = "page-card-header";
    const icon = page.icon || (isMain ? "home.png" : null);
    if (icon) {
        const img = document.createElement("img");
        img.src = imgUrl(icon);
        img.alt = "";
        header.appendChild(img);
    }
    const title = document.createElement("strong");
    title.textContent = isMain
        ? t("main_page.title")
        : (page.name || page.id || `Page ${index + 1}`);
    header.appendChild(title);
    card.appendChild(header);

    const nameInput = document.createElement("input");
    nameInput.type = "text";
    nameInput.value = page.name || "";
    nameInput.placeholder = t("pages.name_placeholder");
    if (isMain) nameInput.disabled = true;
    nameInput.onchange = () => {
        page.name = nameInput.value;
        title.textContent = page.name;
        populatePageSelect();
        scheduleSave();
    };
    card.appendChild(nameInput);

    const editRow = document.createElement("div");
    editRow.className = "page-actions";

    const editBtn = document.createElement("button");
    editBtn.className = "btn btn-save";
    editBtn.textContent = t("pages.edit");
    editBtn.onclick = () => editPage(index);
    editRow.appendChild(editBtn);
    card.appendChild(editRow);

    const actions = document.createElement("div");
    actions.className = "page-actions";

    const dupBtn = document.createElement("button");
    dupBtn.className = "btn btn-secondary";
    dupBtn.textContent = t("pages.duplicate");
    dupBtn.onclick = () => duplicatePage(index);
    actions.appendChild(dupBtn);

    if (!isMain) {
        const delBtn = document.createElement("button");
        delBtn.className = "btn btn-delete";
        delBtn.textContent = t("pages.delete");
        delBtn.onclick = () => deletePage(index);
        actions.appendChild(delBtn);
    }

    card.appendChild(actions);
    return card;
}

function renderPagesList() {
    const list = $("pages-list");
    if (!list) return;
    list.innerHTML = "";
    /* Main is always first and cannot be deleted. */
    list.appendChild(buildPageCard(mainPageObj(), -1));
    state.config.pages.forEach((page, index) => {
        list.appendChild(buildPageCard(page, index));
    });
    $("pages-count").textContent = t("pages.count", { n: state.config.pages.length + 1 });
}

function populatePageSelect() {
    const select = $("page-select");
    if (!select) return;
    select.innerHTML = "";
    const main = document.createElement("option");
    main.value = "-1";
    main.textContent = t("main_page.title");
    select.appendChild(main);
    state.config.pages.forEach((page, index) => {
        const option = document.createElement("option");
        option.value = index;
        option.textContent = page.name || page.id || `Page ${index + 1}`;
        select.appendChild(option);
    });
    select.value = String(state.currentPageIndex);
}

function addPage() {
    const n = state.config.pages.length + 1;
    state.config.pages.push({
        id: "page_" + Date.now(),
        name: t("pages.new_name", { n }),
        background: { type: "image", image: "age_bg_dark.png" },
        matrix: { rows: 2, cols: 4 },
        buttons: [],
    });
    renderPagesList();
    populatePageSelect();
    scheduleSave();
    toast(t("messages.page_added"));
}

function deletePage(index) {
    if (!confirm(t("messages.delete_page_confirm"))) return;
    state.config.pages.splice(index, 1);
    if (state.config.pages.length === 0) {
        state.currentPageIndex = -1;
    } else if (state.currentPageIndex >= state.config.pages.length) {
        state.currentPageIndex = state.config.pages.length - 1;
    }
    renderPagesList();
    populatePageSelect();
    loadPageButtons();
    scheduleSave();
}

/* Full copy of a page, inserted right after the source (the Main page, index -1,
 * is copied as the first regular page). The name gets a trailing " 1" so the
 * duplicate is easy to tell apart. */
function duplicatePage(index) {
    const isMain = index === -1;
    const src = isMain ? mainPageObj() : state.config.pages[index];
    if (!src) return;
    const copy = JSON.parse(JSON.stringify(src));
    copy.id = "page_" + Date.now();
    const base = src.name || src.display_name || src.id || t("pages.new_name", { n: 1 });
    copy.name = base + " 1";
    if (copy.display_name != null) copy.display_name = base + " 1";
    const stamp = Date.now();
    (copy.buttons || []).forEach((b, i) => {
        b.id = "btn_" + stamp + "_" + i;
    });
    if (isMain) state.config.pages.unshift(copy);
    else state.config.pages.splice(index + 1, 0, copy);
    renderPagesList();
    populatePageSelect();
    scheduleSave();
    toast(t("messages.page_duplicated"));
}

function editPage(index) {
    state.currentPageIndex = index;
    populatePageSelect();
    loadPageButtons();
    showSection("buttons");
}

/* ------------------------------------------------------------------ */
/* Grid / button editor                                               */
/* ------------------------------------------------------------------ */
function currentPage() {
    if (state.currentPageIndex === -1) return mainPageObj();
    return state.config.pages[state.currentPageIndex];
}

function computeLayout(page) {
    const mx = pageMatrix(page);
    const geo = gridGeometry(mx.rows, mx.cols);
    return { rows: mx.rows, cols: mx.cols, bw: geo.side, bh: geo.side, gap_h: geo.gap_h, gap_v: geo.gap_v, W: 480, H: 320, status_h: STATUS_BAR_H };
}

function cellPos(layout, row, col) {
    return {
        x: layout.gap_h + col * (layout.bw + layout.gap_h),
        y: layout.status_h + layout.gap_v + row * (layout.bh + CAPTION_H + layout.gap_v),
    };
}

/* ------------------------------------------------------------------ */
/* Drag & swap: hold a button in an editable grid and drop it onto     */
/* another cell (empty = move, occupied = swap). Kept deliberately      */
/* small: one pointer-driven state machine shared by every grid.        */
/* ------------------------------------------------------------------ */
let dragState = null;
let dragSuppressClick = 0;

function dragCellAt(container, layout, clientX, clientY) {
    const rect = container.getBoundingClientRect();
    /* The canvas may be CSS-scaled (M21); map viewport px back to 480x320 space. */
    const scale = (container.clientWidth ? rect.width / container.clientWidth : 1) || 1;
    const x = (clientX - rect.left) / scale;
    const y = (clientY - rect.top) / scale;
    for (let r = 0; r < layout.rows; r++) {
        for (let c = 0; c < layout.cols; c++) {
            const p = cellPos(layout, r, c);
            if (x >= p.x && x <= p.x + layout.bw && y >= p.y && y <= p.y + layout.bh) {
                return { row: r, col: c };
            }
        }
    }
    return null;
}

function dragHighlight(cell) {
    if (!dragState) return;
    if (dragState.hl) dragState.hl.classList.remove("drop-target");
    dragState.hl = null;
    if (!cell) return;
    const el = dragState.container.querySelector(
        `.device-button[data-row="${cell.row}"][data-col="${cell.col}"]`
    );
    if (el) {
        el.classList.add("drop-target");
        dragState.hl = el;
    }
}

function dragMove(e) {
    if (!dragState) return;
    if (!dragState.started) {
        if (Math.hypot(e.clientX - dragState.x, e.clientY - dragState.y) < 6) return;
        dragState.started = true;
        dragState.el.classList.add("dragging");
    }
    e.preventDefault();
    dragHighlight(dragCellAt(dragState.container, dragState.layout, e.clientX, e.clientY));
}

function dragEnd(e) {
    window.removeEventListener("pointermove", dragMove);
    window.removeEventListener("pointerup", dragEnd);
    const d = dragState;
    dragState = null;
    if (!d || !d.started) return; /* a plain click still opens the editor */
    d.el.classList.remove("dragging");
    if (d.hl) d.hl.classList.remove("drop-target");
    dragSuppressClick = Date.now() + 400;
    const to = dragCellAt(d.container, d.layout, e.clientX, e.clientY);
    if (to && (to.row !== d.from.row || to.col !== d.from.col) && typeof d.onDrop === "function") {
        d.onDrop(d.from, to);
    }
}

function dragStart(e, container, layout, row, col, onDrop) {
    if (e.button && e.button !== 0) return;
    dragState = {
        container, layout, onDrop,
        from: { row, col },
        x: e.clientX, y: e.clientY,
        el: e.currentTarget,
        hl: null,
        started: false,
    };
    window.addEventListener("pointermove", dragMove);
    window.addEventListener("pointerup", dragEnd);
}

function moveOrSwap(page, from, to) {
    const buttons = page && page.buttons;
    if (!buttons) return null;
    const src = buttons.find((b) => b.position && b.position.row === from.row && b.position.col === from.col);
    if (!src) return null;
    const dst = buttons.find((b) => b.position && b.position.row === to.row && b.position.col === to.col);
    if (dst) dst.position = { row: from.row, col: from.col };
    src.position = { row: to.row, col: to.col };
    return src;
}

function readMatrixInput() {
    const el = $("grid-matrix");
    const v = el ? el.value : "2x4";
    const parts = v.split("x").map((n) => Number(n));
    return clampMatrix(parts[0], parts[1]);
}

function loadPageButtons() {
    state.currentPageIndex = Number($("page-select").value);
    const page = currentPage();
    if (!page) return;
    state.editingButtonIndex = -1;

    const mx = pageMatrix(page);
    if ($("grid-matrix")) $("grid-matrix").value = mx.rows + "x" + mx.cols;
    if ($("page-style-preset")) {
        if (!$("page-style-preset").options.length) fillPresetSelect($("page-style-preset"), true);
        $("page-style-preset").value = page.button_style ? page.button_style.preset : "";
    }
    updatePageStyleStats();
    updatePageBgUi();
    populatePageBgPicker();
    $("button-editor").style.display = "none";
    calculateGrid();
}

function calculateGrid() {
    const page = currentPage();
    if (!page) return;
    page.matrix = readMatrixInput();
    delete page.grid;
    const layout = computeLayout(page);
    $("calc-button-size").textContent = `${layout.bw} x ${layout.bh} px`;
    renderGridCanvas();
    renderPreview();
    scheduleSave();
}

/* Shared device-like preview: status bar (name + BT/WiFi), page background,
 * button backgrounds, icons and captions. Used by every grid/preview block. */
function mockStatusBar(page) {
    const s = state.settings || {};
    /* Hidden panel: nothing to draw (the page content stays where it is). */
    if (s.status_bar_visible === false) return null;
    const bar = document.createElement("div");
    bar.className = "mock-statusbar";
    /* Transparency: 0 = opaque, 100 = fully transparent. */
    const tr = Math.max(0, Math.min(100, s.status_bar_transparency != null ? s.status_bar_transparency : 0));
    bar.style.background = `rgba(17,17,17,${(100 - tr) / 100})`;
    const title = document.createElement("span");
    title.className = "mock-title";
    title.textContent = page.display_name || page.name || "";
    bar.appendChild(title);
    if (s.show_stats) {
        const stats = document.createElement("span");
        stats.className = "mock-stats";
        stats.textContent = "FPS 0  CPU 0%";
        bar.appendChild(stats);
    }
    const icons = document.createElement("span");
    icons.className = "mock-icons";
    ["icon_bt.png", "icon_wifi.png", "icon_sd.png"].forEach((n) => {
        const im = document.createElement("img");
        im.src = assetUrl("images/icons/system", n);
        im.alt = n;
        icons.appendChild(im);
    });
    bar.appendChild(icons);
    return bar;
}

/* ---- Native gradient backgrounds ----
 * The device renders these itself (no PNG), which is much faster and avoids
 * image decoding. Presets set two colours the user can then tweak. */
const GRADIENT_PRESETS = {
    ocean:    { color: "#1E3A8A", color2: "#0EA5E9" },
    sky:      { color: "#0EA5E9", color2: "#67E8F9" },
    cyan:     { color: "#0891B2", color2: "#22D3EE" },
    teal:     { color: "#0D9488", color2: "#2DD4BF" },
    emerald:  { color: "#059669", color2: "#34D399" },
    green:    { color: "#16A34A", color2: "#4ADE80" },
    lime:     { color: "#65A30D", color2: "#A3E635" },
    amber:    { color: "#D97706", color2: "#FBBF24" },
    gold:     { color: "#CA8A04", color2: "#FDE047" },
    orange:   { color: "#EA580C", color2: "#FB923C" },
    coral:    { color: "#F97316", color2: "#FB7185" },
    red:      { color: "#DC2626", color2: "#F87171" },
    rose:     { color: "#E11D48", color2: "#FDA4AF" },
    pink:     { color: "#DB2777", color2: "#F472B6" },
    fuchsia:  { color: "#C026D3", color2: "#E879F9" },
    purple:   { color: "#7C3AED", color2: "#A78BFA" },
    violet:   { color: "#6D28D9", color2: "#8B5CF6" },
    indigo:   { color: "#4338CA", color2: "#6366F1" },
    slate:    { color: "#0F2027", color2: "#2C5364" },
    midnight: { color: "#0F172A", color2: "#334155" },
};

/* Render gradient presets as clickable squares (a dropdown only shows names,
 * which says nothing about how the gradient looks). */
function renderGradientPresets(container, onPick, bg) {
    if (!container) return;
    container.innerHTML = "";
    Object.keys(GRADIENT_PRESETS).forEach((k) => {
        const p = GRADIENT_PRESETS[k];
        if (!p) return; /* skip the "custom" pseudo-entry */
        const el = document.createElement("div");
        el.className = "grad-swatch";
        el.style.background = `linear-gradient(to bottom, ${p.color}, ${p.color2})`;
        el.title = k;
        if (bg && bg.color && bg.color2 &&
            bg.color.toLowerCase() === p.color.toLowerCase() &&
            bg.color2.toLowerCase() === p.color2.toLowerCase()) {
            el.classList.add("selected");
        }
        el.onclick = () => onPick(k);
        container.appendChild(el);
    });
}

function gradientCss(bg) {
    if (!bg || bg.type !== "gradient" || !bg.color || !bg.color2) return null;
    const dir = bg.direction === "horizontal" ? "to right" : "to bottom";
    return `linear-gradient(${dir}, ${bg.color}, ${bg.color2})`;
}

function paintMock(container, page, opts) {
    opts = opts || {};
    if (!container || !page) return;
    container.innerHTML = "";
    const layout = computeLayout(page);
    container.style.width = layout.W + "px";
    container.style.height = layout.H + "px";

    const bg = page.background || {};
    const pgGrad = gradientCss(bg);
    if (pgGrad) {
        container.style.backgroundImage = "none";
        container.style.background = pgGrad;
    } else if (bg.image) {
        container.style.backgroundImage = "url('" + imgUrl(bg.image) + "')";
        container.style.backgroundSize = "cover";
        container.style.backgroundPosition = "center";
        container.style.backgroundColor = "#0d1117";
    } else {
        container.style.backgroundImage = "none";
        container.style.background = bg.color || "#1a1a2e";
    }

    const sb = mockStatusBar(page);
    if (sb) container.appendChild(sb);

    for (let r = 0; r < layout.rows; r++) {
        for (let c = 0; c < layout.cols; c++) {
            const pos = cellPos(layout, r, c);
            let btn = (page.buttons || []).find(
                (b) => b.position && b.position.row === r && b.position.col === c
            );
            /* Live draft of the button currently being edited (not saved yet). */
            if (opts.override && opts.override.cell && opts.override.cell.row === r && opts.override.cell.col === c) {
                btn = opts.override.btn;
            }
            const div = document.createElement("div");
            div.className = "device-button";
            div.style.left = pos.x + "px";
            div.style.top = pos.y + "px";
            div.style.width = layout.bw + "px";
            div.style.height = layout.bh + "px";

            if (btn) {
                const st = effectiveStyle(btn, page);
                applyStyleCss(div, st);
                const b = btn.background || {};
                const bGrad = gradientCss(b);
                if (bGrad) {
                    div.style.backgroundImage = "none";
                    div.style.background = bGrad;
                } else if (b.image) {
                    div.style.backgroundImage = "url('" + imgUrl(b.image) + "')";
                    div.style.backgroundSize = "cover";
                } else if (b.type === "solid") {
                    div.style.background = b.color || "#2C3E50";
                }
                if (btn.content === "text" && btn.label) {
                    /* Text button: apply the style's text font/colour/shadow. */
                    const tx = document.createElement("span");
                    tx.className = "device-text";
                    tx.textContent = btn.label;
                    applyCaptionCss(tx, st.text || defaultTextStyle());
                    div.appendChild(tx);
                } else if (btn.icon) {
                    const im = document.createElement("img");
                    im.className = "mock-btn-icon";
                    im.src = imgUrl(btn.icon);
                    im.alt = btn.icon;
                    im.draggable = false;
                    div.appendChild(im);
                }
            } else {
                div.classList.add("empty");
                if (opts.editable) div.textContent = "+";
            }

            if (opts.selected && opts.selected.row === r && opts.selected.col === c) {
                div.classList.add("selected");
            }
            if (opts.editable) {
                div.dataset.row = r;
                div.dataset.col = c;
                div.onclick = () => {
                    if (dragSuppressClick && Date.now() < dragSuppressClick) return;
                    opts.onCell(r, c);
                };
                if (btn && opts.onDrop) {
                    div.onpointerdown = (e) => dragStart(e, container, layout, r, c, opts.onDrop);
                }
            }
            container.appendChild(div);

            if (btn && btn.caption) {
                const cap = document.createElement("div");
                cap.className = "device-caption";
                cap.style.left = pos.x + layout.bw / 2 + "px";
                cap.style.top = pos.y + layout.bh + 2 + "px";
                cap.textContent = btn.caption;
                applyCaptionCss(cap, effectiveStyle(btn, page).caption);
                container.appendChild(cap);
            }
        }
    }
}

function renderGridCanvas() {
    const page = currentPage();
    const canvas = $("grid-canvas");
    if (!page || !canvas) return;
    const editor = $("button-editor");
    const editing = state.editingButtonIndex >= 0 && editor && editor.style.display !== "none";
    const override = editing ? { cell: state.selectedCell, btn: readButtonFromForm() } : null;
    paintMock(canvas, page, { editable: true, selected: state.selectedCell, onCell: selectCell, onDrop: dropPageButton, override });
}

function dropPageButton(from, to) {
    const page = currentPage();
    const src = moveOrSwap(page, from, to);
    if (!src) return;
    state.selectedCell = { row: to.row, col: to.col };
    if (state.editingButtonIndex >= 0) {
        const idx = page.buttons.indexOf(src);
        if (idx >= 0) {
            state.editingButtonIndex = idx;
            loadButtonIntoForm(src);
        }
    }
    saveConfig();
    renderGridCanvas();
    renderPreview();
    toast(t("messages.button_moved"));
}

function selectCell(row, col) {
    const page = currentPage();
    if (!page) return;
    state.selectedCell = { row, col };
    if (!page.buttons) page.buttons = [];

    let index = page.buttons.findIndex((b) => b.position && b.position.row === row && b.position.col === col);
    if (index < 0) {
        const btn = {
            id: "btn_" + Date.now(),
            position: { row, col },
            background: { type: "gradient", color: "#1E3A8A", color2: "#0EA5E9", direction: "vertical" },
            icon: "copy.png",
            caption: t("buttons.new_label"),
            action: { type: "hotkey", keys: "CTRL+C" },
        };
        page.buttons.push(btn);
        index = page.buttons.length - 1;
    }

    state.editingButtonIndex = index;
    $("button-editor").style.display = "block";
    loadButtonIntoForm(page.buttons[index]);
    renderGridCanvas();
}

function loadButtonIntoForm(btn) {
    $("btn-caption").value = btn.caption || "";
    if ($("btn-content")) $("btn-content").value = btn.content === "text" ? "text" : "icon";
    if ($("btn-label")) $("btn-label").value = btn.label || "";
    updateContentMode();
    state.pickIcon = btn.icon || "";
    fillIconPicker($("btn-icon-picker"), ICON_DIRS_BUTTON, state.pickIcon, (name) => {
        state.pickIcon = name;
        setSyncNeeded(true);
        updateButtonPreview();
    });

    const bg = btn.background || {};
    $("btn-bg-type").value = bg.type === "solid" ? "solid"
        : (bg.type === "transparent" ? "transparent"
        : (bg.type === "gradient" ? "gradient" : "image"));
    $("btn-bg-color").value = bg.color || "#2C3E50";
    renderGradientPresets($("btn-bg-grad-presets"), onBtnGradPreset, bg.type === "gradient" ? bg : null);
    if ($("btn-grad-color1")) $("btn-grad-color1").value = (bg.type === "gradient" && bg.color) ? bg.color : "#1E3A8A";
    if ($("btn-grad-color2")) $("btn-grad-color2").value = (bg.type === "gradient" && bg.color2) ? bg.color2 : "#0EA5E9";
    if ($("btn-grad-dir")) $("btn-grad-dir").value = bg.direction === "horizontal" ? "horizontal" : "vertical";
    state.pickImage = bg.image || "";
    updateBgOptions();
    loadButtonImages();

    const action = btn.action || {};
    let atype = "hotkey";
    if (btn.type === "settings") atype = "settings";
    else if (btn.type === "page_link") atype = "page";
    else if (action.type) atype = action.type;
    $("btn-action-type").value = atype;
    state.obsPickIcon = "";
    if (atype !== "obs" && $("btn-obs-icon-picker")) {
        $("btn-obs-icon-picker").innerHTML = "";
    }
    if (atype === "macro") {
        state.macroSteps = Array.isArray(action.steps)
            ? action.steps.map((s) => ({
                  type: s.type || "key",
                  value: s.value !== undefined ? s.value : "",
                  delay_ms: Number(s.delay_ms) || 0,
              }))
            : [];
        if (!state.macroSteps.length && action.keys) {
            /* Legacy "A;B" macro -> key steps. */
            state.macroSteps = String(action.keys)
                .split(";")
                .map((k) => k.trim())
                .filter(Boolean)
                .map((k) => ({ type: "key", value: k, delay_ms: 0 }));
        }
        $("btn-hotkey").value = "";
    } else {
        state.macroSteps = [];
        $("btn-hotkey").value = action.keys || action.text || action.target || "";
        if (atype === "multimedia" && $("btn-multimedia")) {
            $("btn-multimedia").value = action.value || "PLAY_PAUSE";
        }
    }
    if (atype === "page") {
        populatePageTargetSelect($("btn-page-target"), btn.target_page || action.target || "__HOME__");
    }
    if (atype === "obs") {
        if ($("btn-obs-command")) $("btn-obs-command").value = action.command || "TOGGLE_REC";
        state.obsPickIcon = action.icon_rec || "";
        fillIconPicker($("btn-obs-icon-picker"), ICON_DIRS_BUTTON, state.obsPickIcon, (name) => {
            state.obsPickIcon = name;
            setSyncNeeded(true);
        });
    }
    updateActionOptions();

    if ($("btn-style-mode")) {
        if (!$("btn-style-preset").options.length) fillPresetSelect($("btn-style-preset"), false);
        let mode = "inherit";
        if (btn.style_unique) mode = "unique";
        else if (btn.style) mode = "named";
        $("btn-style-mode").value = mode;
        const g = (state.config.defaults && state.config.defaults.button_style) || { preset: "glass" };
        if (mode === "named") $("btn-style-preset").value = (btn.style && btn.style.preset) || g.preset || "glass";
        else $("btn-style-preset").value = g.preset || "glass";
        onBtnStyleModeChange(false);
    }
    updateStyleSourceInfo();

    updateButtonPreview();
}

function readButtonFromForm() {
    const actionType = $("btn-action-type").value;
    const actionValue = $("btn-hotkey").value;
    const bgType = $("btn-bg-type").value;

    let btnStyle = null;
    let btnStyleUnique = false;
    if ($("btn-style-mode")) {
        const m = $("btn-style-mode").value;
        if (m === "named") {
            btnStyle = { preset: $("btn-style-preset").value };
        } else if (m === "unique") {
            btnStyle = buRead();
            btnStyleUnique = true;
        }
    }

    const base = {
        id: "btn_" + Date.now(),
        position: state.selectedCell ? { row: state.selectedCell.row, col: state.selectedCell.col } : { row: 0, col: 0 },
        background: bgType === "solid"
            ? { type: "solid", color: $("btn-bg-color").value }
            : (bgType === "transparent"
                ? { type: "transparent" }
                : (bgType === "gradient"
                    ? {
                          type: "gradient",
                          color: $("btn-grad-color1").value,
                          color2: $("btn-grad-color2").value,
                          direction: $("btn-grad-dir").value,
                      }
                    : { type: "image", image: state.pickImage })),
        icon: state.pickIcon || null,
        content: ($("btn-content") ? $("btn-content").value : "icon"),
        label: ($("btn-label") ? $("btn-label").value : ""),
        style: btnStyle,
        style_unique: btnStyleUnique,
        caption: $("btn-caption").value,
    };

    if (actionType === "page") {
        base.type = "page_link";
        base.target_page = $("btn-page-target") ? $("btn-page-target").value : "__HOME__";
        return base;
    }
    if (actionType === "settings") {
        base.type = "settings";
        return base;
    }

    let action;
    if (actionType === "macro") {
        action = { type: "macro", steps: readMacroSteps() };
    } else if (actionType === "multimedia") {
        action = { type: "multimedia", value: $("btn-multimedia").value };
    } else if (actionType === "obs") {
        action = {
            type: "obs",
            command: $("btn-obs-command") ? $("btn-obs-command").value : "TOGGLE_REC",
            icon_rec: state.obsPickIcon || null,
        };
    } else {
        action = { type: actionType };
        if (actionType === "text") action.text = actionValue;
        else action.keys = actionValue;
    }
    base.action = action;
    return base;
}

/* Icon pickers: a grid of the real PNGs instead of a <select>. The union of
 * both icon folders is offered so any icon already used stays selectable. */
const ICON_DIRS_BUTTON = ["images/icons/buttons", "images/icons/actions",
                          "sd:images/icons/buttons", "sd:images/icons/actions",
                          "images/icons/pages", "sd:images/icons/pages"];
const ICON_DIRS_MAIN = ["images/icons/pages", "sd:images/icons/pages",
                        "images/icons/buttons", "sd:images/icons/buttons"];

async function iconList(dirs) {
    const out = [];
    for (const dir of dirs) {
        const items = await getJson("/api/images?dir=" + encodeURIComponent(dir), []);
        items
            .filter((it) => !it.dir && /\.png$/i.test(it.name))
            .map((it) => ({ dir, name: it.name }))
            .sort((a, b) => a.name.localeCompare(b.name))
            .forEach((it) => out.push(it));
    }
    return out;
}

function fillIconPicker(picker, dirs, selected, onPick) {
    if (!picker) return;
    picker.innerHTML = "";
    const none = document.createElement("div");
    none.className = "pick-item" + (selected ? "" : " selected");
    none.textContent = "\u2205";
    none.title = t("buttons.icon_none");
    none.style.cssText = "display:flex;align-items:center;justify-content:center;color:rgba(255,255,255,.5);font-size:18px";
    none.onclick = () => {
        picker.querySelectorAll(".pick-item").forEach((n) => n.classList.remove("selected"));
        none.classList.add("selected");
        onPick("");
    };
    picker.appendChild(none);

    const key = dirs.join(",");
    if (!state.iconCache) state.iconCache = {};
    if (!state.iconCache[key]) state.iconCache[key] = iconList(dirs);
    state.iconCache[key].then((list) => {
        list.forEach((it) => {
            const el = document.createElement("div");
            el.className = "pick-item" + (it.name === selected ? " selected" : "");
            const img = document.createElement("img");
            img.src = assetUrl(it.dir, it.name);
            img.alt = it.name;
            img.title = it.name;
            img.loading = "lazy";
            el.appendChild(img);
            el.onclick = () => {
                picker.querySelectorAll(".pick-item").forEach((n) => n.classList.remove("selected"));
                el.classList.add("selected");
                onPick(it.name);
            };
            picker.appendChild(el);
        });
    });
}

function updateButtonPreview() {
    const preview = $("button-preview");
    const iconEl = $("preview-icon");
    const captionEl = $("preview-caption");
    const textEl = $("preview-text");
    if (!preview) return;

    const page = currentPage();
    const side = page ? computeLayout(page).bw : 100; /* 100 (2 rows) or 70 (3 rows) */
    const styleMode = $("btn-style-mode") ? $("btn-style-mode").value : "inherit";
    let previewStyle;
    if (styleMode === "unique") {
        previewStyle = buRead();
    } else if (styleMode === "named") {
        previewStyle = presetObject($("btn-style-preset") ? $("btn-style-preset").value : "glass");
    } else {
        previewStyle = page ? effectiveStyle(getCurrentButton() || {}, page) : STYLE_PRESETS.glass;
    }
    preview.style.width = side + "px";
    preview.style.height = side + "px";
    applyStyleCss(preview, previewStyle);

    const bgType = $("btn-bg-type").value;
    if (bgType === "gradient") {
        preview.style.backgroundImage = "none";
        preview.style.background = gradientCss({
            type: "gradient",
            color: $("btn-grad-color1").value,
            color2: $("btn-grad-color2").value,
            direction: $("btn-grad-dir").value,
        });
    } else if (bgType === "transparent") {
        preview.style.backgroundImage = "none";
        preview.style.backgroundColor = "transparent";
    } else if (bgType === "solid") {
        preview.style.backgroundImage = "none";
        preview.style.backgroundColor = $("btn-bg-color").value;
    } else if (state.pickImage) {
        preview.style.backgroundImage = `url('${imgUrl(state.pickImage)}')`;
        preview.style.backgroundSize = "cover";
        preview.style.backgroundColor = "#2C3E50";
    } else {
        preview.style.backgroundImage = "none";
        preview.style.backgroundColor = "transparent";
    }

    const contentMode = $("btn-content") ? $("btn-content").value : "icon";
    if (contentMode === "text") {
        /* Text goes INSIDE the button (in place of the icon). */
        if (iconEl) iconEl.style.display = "none";
        const label = $("btn-label") ? $("btn-label").value : "";
        if (textEl) {
            const tx = previewStyle.text || defaultTextStyle();
            textEl.textContent = label;
            textEl.style.display = label ? "flex" : "none";
            textEl.style.color = tx.color || "#ffffff";
            textEl.style.fontWeight = tx.bold ? "700" : "400";
            textEl.style.fontSize = (tx.size || 14) + "px";
            textEl.style.textShadow = textShadowCss(tx);
        }
    } else {
        if (textEl) {
            textEl.textContent = "";
            textEl.style.display = "none";
        }
        if (iconEl) {
            const ic = state.pickIcon || "";
            if (ic) {
                iconEl.style.display = "block";
                iconEl.src = imgUrl(ic);
            } else {
                iconEl.style.display = "none";
            }
        }
    }

    /* Caption is always the text UNDER the button. */
    if (captionEl) {
        const cap = $("btn-caption") ? $("btn-caption").value : "";
        captionEl.textContent = cap;
        captionEl.style.display = cap ? "block" : "none";
        applyCaptionCss(captionEl, previewStyle.caption);
    }

    /* Live-refresh the page preview (grid + device) as the button changes. */
    renderGridCanvas();
    renderPreview();
}

function updateContentMode() {
    const mode = $("btn-content") ? $("btn-content").value : "icon";
    if ($("btn-content-icon")) $("btn-content-icon").style.display = mode === "text" ? "none" : "block";
    if ($("btn-content-text")) $("btn-content-text").style.display = mode === "text" ? "block" : "none";
    updateButtonPreview();
}

function updateBgOptions() {
    const type = $("btn-bg-type").value;
    if ($("bg-solid-options")) $("bg-solid-options").style.display = type === "solid" ? "block" : "none";
    if ($("bg-gradient-options")) $("bg-gradient-options").style.display = type === "gradient" ? "block" : "none";
    if ($("bg-image-options")) $("bg-image-options").style.display = type === "image" ? "block" : "none";
    updateButtonPreview();
}

function onBtnGradPreset(key) {
    const p = GRADIENT_PRESETS[key];
    if (p) {
        $("btn-grad-color1").value = p.color;
        $("btn-grad-color2").value = p.color2;
    }
    updateButtonPreview();
}


function updateActionOptions() {
    const type = $("btn-action-type").value;
    const label = $("action-value-label");
    const hint = $("action-hint");
    const input = $("btn-hotkey");
    const keysRow = $("btn-hotkey-keys");
    const pageSel = $("btn-page-target");

    const show = (id, on) => {
        if ($(id)) $(id).style.display = on ? "block" : "none";
    };
    show("hotkey-options", type === "hotkey" || type === "text" || type === "page");
    show("macro-options", type === "macro");
    show("multimedia-options", type === "multimedia");
    show("obs-options", type === "obs");

    /* For "go to page" only the page selector is relevant. */
    if (keysRow) keysRow.style.display = type === "page" ? "none" : "flex";
    if (input) input.style.display = type === "page" ? "none" : "block";
    if (pageSel) pageSel.style.display = type === "page" ? "block" : "none";

    if (type === "macro") {
        renderMacroSteps();
    } else if (type === "text") {
        label.textContent = t("buttons.action_value_text");
        hint.textContent = t("buttons.text_hint");
        input.placeholder = "Hello world";
    } else if (type === "page") {
        label.textContent = t("buttons.action_value_page");
        hint.textContent = t("buttons.page_hint");
        populatePageTargetSelect(pageSel, pageSel ? pageSel.value : "");
    } else if (type === "hotkey") {
        label.textContent = t("buttons.action_value_hotkey");
        hint.textContent = t("buttons.hotkey_hint");
        input.placeholder = "CTRL+C";
    } else if (type === "settings") {
        label.textContent = t("main_page.type_settings");
        hint.textContent = t("buttons.settings_hint");
    }

    /* Fill the OBS recording-icon picker lazily (also when the type is switched). */
    if (type === "obs" && $("btn-obs-icon-picker") && !$("btn-obs-icon-picker").childElementCount) {
        fillIconPicker($("btn-obs-icon-picker"), ICON_DIRS_BUTTON, state.obsPickIcon, (name) => {
            state.obsPickIcon = name;
            setSyncNeeded(true);
        });
    }
}

/* Page selector for "go to page": Main first, then every page. */
function populatePageTargetSelect(sel, selected) {
    if (!sel) return;
    const current = selected !== undefined ? selected : sel.value;
    sel.innerHTML = "";
    const main = document.createElement("option");
    main.value = "__HOME__";
    main.textContent = t("main_page.title");
    sel.appendChild(main);
    state.config.pages.forEach((p) => {
        const o = document.createElement("option");
        o.value = p.id;
        o.textContent = p.display_name || p.name || p.id;
        sel.appendChild(o);
    });
    if (current) sel.value = current;
}

/* ---- Macro steps editor ---- */
function defaultMacroStep() {
    return { type: "key", value: "", delay_ms: 0 };
}

function renderMacroSteps() {
    const box = $("macro-steps");
    if (!box) return;
    buildKeyChips("macro-keys", appendMacroToken);
    box.innerHTML = "";
    if (!state.macroSteps.length) {
        const empty = document.createElement("div");
        empty.className = "hint";
        empty.textContent = t("buttons.macro_empty");
        box.appendChild(empty);
        return;
    }
    state.macroSteps.forEach((step, i) => {
        const row = document.createElement("div");
        row.className = "macro-step";

        const typeSel = document.createElement("select");
        STEP_TYPES.forEach((tp) => {
            const o = document.createElement("option");
            o.value = tp;
            o.textContent = t("buttons.step_" + tp);
            if (tp === step.type) o.selected = true;
            typeSel.appendChild(o);
        });
        typeSel.onchange = () => {
            step.type = typeSel.value;
            renderMacroSteps();
        };
        row.appendChild(typeSel);

        if (step.type === "multimedia") {
            const sel = document.createElement("select");
            MEDIA_OPTIONS.forEach((m) => {
                const o = document.createElement("option");
                o.value = m;
                o.textContent = m;
                if (m === step.value) o.selected = true;
                sel.appendChild(o);
            });
            sel.onchange = () => { step.value = sel.value; };
            row.appendChild(sel);
        } else if (step.type === "delay") {
            const val = document.createElement("input");
            val.type = "number";
            val.min = "0";
            val.value = step.value || 0;
            val.placeholder = "ms";
            val.oninput = () => { step.value = Number(val.value) || 0; };
            row.appendChild(val);
        } else {
            const val = document.createElement("input");
            val.type = "text";
            val.value = step.value || "";
            val.placeholder = step.type === "text" ? t("buttons.action_value_text") : "CTRL+C";
            val.oninput = () => { step.value = val.value; };
            val.onfocus = () => { macroActiveInput = val; };
            row.appendChild(val);
        }

        if (step.type !== "delay") {
            const delay = document.createElement("input");
            delay.type = "number";
            delay.min = "0";
            delay.value = step.delay_ms || 0;
            delay.title = t("buttons.step_delay");
            delay.oninput = () => { step.delay_ms = Number(delay.value) || 0; };
            row.appendChild(delay);
        }

        const up = document.createElement("button");
        up.type = "button";
        up.className = "step-btn";
        up.textContent = "\u2191";
        up.onclick = () => moveMacroStep(i, -1);
        row.appendChild(up);

        const dn = document.createElement("button");
        dn.type = "button";
        dn.className = "step-btn";
        dn.textContent = "\u2193";
        dn.onclick = () => moveMacroStep(i, 1);
        row.appendChild(dn);

        const del = document.createElement("button");
        del.type = "button";
        del.className = "step-btn step-del";
        del.textContent = "\u2715";
        del.onclick = () => removeMacroStep(i);
        row.appendChild(del);

        box.appendChild(row);
    });
}

function addMacroStep() {
    state.macroSteps.push(defaultMacroStep());
    renderMacroSteps();
}

function removeMacroStep(i) {
    state.macroSteps.splice(i, 1);
    renderMacroSteps();
}

function moveMacroStep(i, dir) {
    const j = i + dir;
    if (j < 0 || j >= state.macroSteps.length) return;
    const a = state.macroSteps;
    const tmp = a[i];
    a[i] = a[j];
    a[j] = tmp;
    renderMacroSteps();
}

function readMacroSteps() {
    return state.macroSteps.map((s) => ({
        type: s.type,
        value: s.type === "delay" ? (Number(s.value) || 0) : (s.value || ""),
        delay_ms: Number(s.delay_ms) || 0,
    }));
}

/* ---- Special-key helper buttons for hotkey inputs ---- */
const HOTKEY_KEYS = [
    "CTRL", "ALT", "SHIFT", "WIN", "ENTER", "TAB", "ESC",
    "BACKSPACE", "DELETE", "INSERT", "HOME", "END", "PAGEUP", "PAGEDOWN",
    "UP", "DOWN", "LEFT", "RIGHT", "SPACE",
    "F1", "F2", "F3", "F4", "F5", "F6", "F7", "F8", "F9", "F10", "F11", "F12",
];

function appendHotkeyKey(inputId, token) {
    const input = $(inputId);
    if (!input) return;
    let v = input.value || "";
    if (v.length && !v.endsWith("+")) v += "+";
    input.value = v + token;
    input.dispatchEvent(new Event("input"));
}

function buildKeyChips(containerId, onKey) {
    const box = $(containerId);
    if (!box || box.dataset.built) return;
    box.dataset.built = "1";
    HOTKEY_KEYS.forEach((k) => {
        const b = document.createElement("button");
        b.type = "button";
        b.className = "hotkey-key";
        b.textContent = k;
        b.onclick = () => onKey(k);
        box.appendChild(b);
    });
}

function buildHotkeyKeys(containerId, inputId) {
    buildKeyChips(containerId, (k) => appendHotkeyKey(inputId, k));
}

/* System-key helper row for the macro-steps editor: inserts into the focused
 * (or last) key/text step input, adding a step when the list is empty. */
let macroActiveInput = null;

function appendMacroToken(token) {
    let input = (macroActiveInput && document.body.contains(macroActiveInput)) ? macroActiveInput : null;
    if (!input) {
        const list = document.querySelectorAll('#macro-steps input[type="text"]');
        input = list.length ? list[list.length - 1] : null;
    }
    if (!input) {
        addMacroStep();
        const list = document.querySelectorAll('#macro-steps input[type="text"]');
        input = list.length ? list[list.length - 1] : null;
    }
    if (!input) return;
    let v = input.value || "";
    if (v.length && !v.endsWith("+")) v += "+";
    input.value = v + token;
    input.dispatchEvent(new Event("input"));
    input.focus();
    macroActiveInput = input;
}

/* Button background picker: device (pre-sized) + SD card backgrounds combined.
 * When the SD card is absent the SD listing is empty, so only device tiles show. */
async function fillBgButtonsPicker(picker, dirs, selected, onPick) {
    if (!picker) return;
    picker.innerHTML = "";
    for (const dir of dirs) {
        const items = await getJson(`/api/images?dir=${encodeURIComponent(dir)}`, []);
        items
            .filter((it) => !it.dir && /\.(png|jpe?g)$/i.test(it.name))
            .map((it) => it.name)
            .sort()
            .forEach((name) => {
                const item = document.createElement("div");
                item.className = "pick-item" + (name === selected ? " selected" : "");
                const img = document.createElement("img");
                img.src = assetUrl(dir, name);
                img.alt = name;
                img.title = name;
                img.loading = "lazy";
                item.appendChild(img);
                item.onclick = () => {
                    picker.querySelectorAll(".pick-item").forEach((el) => el.classList.remove("selected"));
                    item.classList.add("selected");
                    onPick(name);
                };
                picker.appendChild(item);
            });
    }
}

async function loadButtonImages() {
    const picker = $("btn-bg-picker");
    if (!picker) return;
    const page = currentPage();
    const mx = page ? pageMatrix(page) : { rows: 2, cols: 4 };
    const dir = "images/" + buttonBgDir(mx.rows);
    await fillBgButtonsPicker(picker, [dir, "sd:" + dir], state.pickImage, (name) => {
        state.pickImage = name;
        updateButtonPreview();
    });
}

async function saveButton() {
    const page = currentPage();
    if (!page || state.editingButtonIndex < 0) {
        toast(t("messages.select_button_first"));
        return;
    }
    const btn = readButtonFromForm();
    const rbg = btn.background || {};
    if (rbg.image || btn.icon) {
        setSyncNeeded(true);
    }
    btn.id = page.buttons[state.editingButtonIndex].id;
    page.buttons[state.editingButtonIndex] = btn;
    await saveConfig();
    if (window.__clearDirty) window.__clearDirty("buttons");
    renderGridCanvas();
    renderPreview();
    renderPagesList();
    toast(t("messages.button_saved"));
}

async function deleteButton() {
    const page = currentPage();
    if (!page || state.editingButtonIndex < 0) return;
    page.buttons.splice(state.editingButtonIndex, 1);
    state.editingButtonIndex = -1;
    state.selectedCell = null;
    $("button-editor").style.display = "none";
    await saveConfig();
    renderGridCanvas();
    renderPreview();
    renderPagesList();
    toast(t("messages.button_deleted"));
}

/* ---- Copy / paste / duplicate a button ---- */
function matrixSize(page) {
    const m = (page && (page.matrix || page.grid)) || { rows: 2, cols: 4 };
    return { rows: Number(m.rows) || 2, cols: Number(m.cols) || 4 };
}

function cellOccupied(page, row, col) {
    return (page.buttons || []).some((b) => b.position && b.position.row === row && b.position.col === col);
}

function findEmptyCell(page) {
    const m = matrixSize(page);
    for (let r = 0; r < m.rows; r++) {
        for (let c = 0; c < m.cols; c++) {
            if (!cellOccupied(page, r, c)) return { row: r, col: c };
        }
    }
    return null;
}

function cloneButton(btn, position) {
    const clone = JSON.parse(JSON.stringify(btn));
    clone.id = "btn_" + Date.now() + "_" + Math.floor(Math.random() * 1000);
    clone.position = { row: position.row, col: position.col };
    return clone;
}

function copyButton() {
    const btn = getCurrentButton();
    if (!btn) {
        toast(t("messages.select_button_first"));
        return;
    }
    /* Copy the current editor state so unsaved tweaks are included. */
    state.buttonClipboard = JSON.stringify(readButtonFromForm());
    toast(t("messages.button_copied"));
}

async function pasteButton() {
    if (!state.buttonClipboard) {
        toast(t("messages.clipboard_empty"));
        return;
    }
    let data;
    try {
        data = JSON.parse(state.buttonClipboard);
    } catch (e) {
        data = null;
    }
    if (!data || typeof data !== "object" || (!data.action && !data.type)) {
        toast(t("messages.clipboard_bad"));
        return;
    }
    /* Paste replaces the settings of the currently open button; use Duplicate
       to create a new button from the clipboard. */
    const page = currentPage();
    const btn = getCurrentButton();
    if (!page || !btn) {
        toast(t("messages.select_button_first"));
        return;
    }
    const target = JSON.parse(JSON.stringify(data));
    target.id = btn.id;
    target.position = btn.position;
    page.buttons[state.editingButtonIndex] = target;
    loadButtonIntoForm(target);
    await saveConfig();
    renderGridCanvas();
    renderPreview();
    renderPagesList();
    toast(t("messages.button_pasted"));
}

async function duplicateButton() {
    const page = currentPage();
    const btn = getCurrentButton();
    if (!page || !btn) {
        toast(t("messages.select_button_first"));
        return;
    }
    const cell = findEmptyCell(page);
    if (!cell) {
        toast(t("messages.no_free_cell"));
        return;
    }
    const clone = cloneButton(readButtonFromForm(), cell);
    page.buttons.push(clone);
    state.selectedCell = cell;
    state.editingButtonIndex = page.buttons.length - 1;
    loadButtonIntoForm(clone);
    await saveConfig();
    renderGridCanvas();
    renderPreview();
    renderPagesList();
    toast(t("messages.button_duplicated"));
}

/* ------------------------------------------------------------------ */
/* Image library                                                      */
/* ------------------------------------------------------------------ */
function showCategory(cat) {
    state.category = cat;
    document.querySelectorAll(".cat-btn").forEach((b) => b.classList.toggle("active", b.dataset.cat === cat));
    loadLibrary(cat);
}

async function loadLibrary(cat) {
    const grid = $("library-grid");
    if (!grid) return;
    grid.innerHTML = "";
    const dirs = LIBRARY_DIRS[cat] || [];
    for (const dir of dirs) {
        const items = await getJson(`/api/images?dir=${encodeURIComponent(dir)}`, []);
        for (const item of items) {
            if (item.dir) continue;
            const card = document.createElement("div");
            card.className = "library-item";

            const img = document.createElement("img");
            img.src = assetUrl(dir, item.name);
            img.alt = item.name;
            img.loading = "lazy";
            card.appendChild(img);

            const name = document.createElement("div");
            name.className = "item-name";
            name.textContent = item.name;
            card.appendChild(name);

            const del = document.createElement("button");
            del.className = "delete-btn";
            del.textContent = "x";
            del.onclick = (event) => {
                event.stopPropagation();
                deleteImage(dir, item.name);
            };
            card.appendChild(del);

            grid.appendChild(card);
        }
    }
}

async function deleteImage(dir, name) {
    if (!confirm(t("messages.delete_image_confirm", { name }))) return;
    const ok = await postJson(`/api/delete?path=${encodeURIComponent(dir + "/" + name)}`, "");
    if (ok) {
        toast(t("messages.deleted"));
        await buildImageIndex();
        loadLibrary(state.category);
    } else {
        toast(t("messages.delete_failed"));
    }
}

function handleFileSelect(event) {
    uploadFiles(Array.from(event.target.files));
    event.target.value = "";
}

async function uploadFiles(files) {
    if (!files.length) return;
    const dir = (LIBRARY_DIRS[state.category] || ["images"])[0];
    const preview = $("upload-preview");
    for (const file of files) {
        const safeName = file.name.replace(/[^A-Za-z0-9._-]/g, "_");
        preview.textContent = t("messages.uploading", { name: safeName });
        try {
            const res = await fetch(`/api/upload?path=${encodeURIComponent(dir + "/" + safeName)}`, {
                method: "POST",
                headers: { "Content-Type": "application/octet-stream" },
                body: file,
            });
            preview.textContent += res.ok ? " ok" : " " + t("messages.upload_failed");
        } catch (e) {
            preview.textContent += " " + t("messages.error");
        }
    }
    toast(t("messages.upload_finished"));
    await buildImageIndex();
    loadLibrary(state.category);
    setTimeout(() => (preview.textContent = ""), 2500);
}

function bindUploadDrag() {
    const area = $("upload-area");
    if (!area) return;
    ["dragenter", "dragover"].forEach((ev) =>
        area.addEventListener(ev, (e) => {
            e.preventDefault();
            area.classList.add("dragover");
        })
    );
    ["dragleave", "drop"].forEach((ev) =>
        area.addEventListener(ev, (e) => {
            e.preventDefault();
            area.classList.remove("dragover");
        })
    );
    area.addEventListener("drop", (e) => {
        if (e.dataTransfer && e.dataTransfer.files) {
            uploadFiles(Array.from(e.dataTransfer.files));
        }
    });
}

/* ------------------------------------------------------------------ */
/* Device preview                                                     */
/* ------------------------------------------------------------------ */
function previewPageObj() {
    if (state.previewPageIndex === 0) return mainPageObj();
    return state.config.pages[state.previewPageIndex - 1];
}

function renderPreview() {
    const screen = $("device-screen");
    if (!screen) return;
    const page = previewPageObj() || state.config.pages[0];
    if (!page) {
        screen.innerHTML = "";
        return;
    }
    paintMock(screen, page, { editable: false });
    const isMain = state.previewPageIndex === 0;
    $("preview-page-name").textContent = isMain
        ? t("main_page.title")
        : (page.name || page.id || "");
}

function previewPage(direction) {
    const count = state.config.pages.length + 1; /* Main + pages */
    state.previewPageIndex = (state.previewPageIndex + (direction === "next" ? 1 : -1) + count) % count;
    renderPreview();
}

/* ------------------------------------------------------------------ */
/* About device (same sections as the on-device About page)           */
/* ------------------------------------------------------------------ */
function fmtSize(kb) {
    if (!kb) return "0 KB";
    if (kb >= 1048576) return (kb / 1048576).toFixed(2) + " GB";
    if (kb >= 1024) return (kb / 1024).toFixed(1) + " MB";
    return kb + " KB";
}

/* "Sep 25 2026" + "18:27:12" -> "25.09.2026 18:27:12" (same as the device). */
function fmtBuildDate(date, time) {
    const mon = { Jan: "01", Feb: "02", Mar: "03", Apr: "04", May: "05", Jun: "06",
                  Jul: "07", Aug: "08", Sep: "09", Oct: "10", Nov: "11", Dec: "12" };
    const m = String(date || "").match(/^([A-Za-z]{3})\s+(\d{1,2})\s+(\d{4})/);
    if (!m) return `${date || "-"} ${time || ""}`.trim();
    return `${m[2].padStart(2, "0")}.${mon[m[1]] || "01"}.${m[3]} ${time || ""}`.trim();
}

/* The Device tab mirrors the firmware "About" page: Hardware, Versions and
 * System Info blocks with the same rows in the same order. */
async function loadAbout() {
    const hw = $("about-hardware");
    const sysEl = $("about-system");
    const fw = $("about-firmware");
    if (!sysEl) return;

    /* Hardware: identical text to the device (About -> Hardware). */
    if (hw) hw.textContent = t("about.hw_text");

    const d = await getJson("/api/system", {});
    const up = d.uptime_seconds || 0;
    const pad = (n) => String(n).padStart(2, "0");
    const kv = (s) => `<div class="kv">${s}</div>`;
    const kvMono = (s) => `<div class="kv mono">${s}</div>`;
    const resetNames = ["Unknown", "Power-on", "External", "Software", "Panic", "Int-WDT",
                        "Task-WDT", "WDT", "Deepsleep", "Brownout", "SDIO", "USB", "JTAG"];
    const resetReason = resetNames[d.reset_reason] || "Unknown";
    const build = fmtBuildDate(d.build_date, d.build_time);

    let html = "";
    html += kv(`${t("about.temperature")}: ${(d.temperature_c || 0).toFixed(1)} C`);
    html += kv(`${t("about.chip")}: ${d.chip || "-"} rev ${d.chip_revision || 0}, ${d.cores || 0} ${t("about.cores")}`);
    html += kv(`CPU: ${d.cpu_freq_mhz || 0} MHz, Flash: ${d.flash_freq_mhz || 0} MHz`);
    html += kv(`${t("about.uptime")}: ${Math.floor(up / 86400)}d ${pad(Math.floor(up / 3600) % 24)}:${pad(Math.floor(up / 60) % 60)}:${pad(up % 60)}`);
    html += kv(`SRAM:  ${fmtSize(d.sram_free)} free / ${fmtSize(d.sram_total)}`);
    html += kv(`PSRAM: ${fmtSize(d.psram_free)} free / ${fmtSize(d.psram_total)}`);
    html += kv(`Heap:  ${fmtSize(d.heap_free)} free / ${fmtSize(d.heap_total)}`);
    html += kv(`${t("about.fs_device")}: ${fmtSize(d.fs_free)} free / ${fmtSize(d.fs_total)}`);
    if (d.sd_present) {
        html += kv(`SD:    ${fmtSize(d.sd_free)} free / ${fmtSize(d.sd_total)}`);
        html += kv(`${t("about.sd_fs")}: ${d.sd_fs || "-"}`);
    } else {
        html += kv(`SD:    ${t("about.not_present")}`);
    }
    html += kvMono(`WiFi RSSI: ${d.wifi_rssi || 0} dBm, reconnects: ${d.wifi_reconnects || 0}`);
    html += kvMono(`WiFi MAC: ${d.wifi_mac || "-"}`);
    html += kvMono(`BT MAC:   ${d.bt_mac || "-"}`);
    html += kv(`${t("about.reset")}: ${resetReason}`);
    html += kv(`IDF: ${d.idf_version || "-"}`);
    if (Array.isArray(d.tasks) && d.tasks.length) {
        html += `<div class="kv" style="margin-top:6px">${t("about.task_stacks")}:</div>`;
        d.tasks.forEach((tt) => {
            html += kvMono(`${tt.name}: ${tt.stack_free}/${tt.stack_total || 0} B`);
        });
    }
    sysEl.innerHTML = html;

    /* Reset reason is part of the system info block now (like the device). */
    const re = $("about-reset");
    if (re) re.textContent = "";

    /* Versions: same order/content as the device (Version, Build, LVGL, IDF). */
    if (fw) {
        fw.textContent = [
            `${t("about.version")}: ${APP_VERSION}`,
            `${t("about.build")}: ${build}`,
            `LVGL: 8.4.0`,
            `ESP-IDF: ${d.idf_version || "-"}`,
        ].join("\n");
    }
}

/* Version tag of the settings web UI (from the script URL query, e.g. ?v=5.3.8). */
function webVersion() {
    const s = document.querySelector('script[src*="app.js"]');
    const m = s && String(s.getAttribute("src")).match(/[?&]v=([^&]+)/);
    return (m && m[1]) ? m[1] : APP_VERSION;
}

/* ------------------------------------------------------------------ */
/* Main page (home) editor                                            */
/* ------------------------------------------------------------------ */
function mainPageObj() {
    if (!state.config.main_page) {
        state.config.main_page = {
            name: "Main",
            display_name: "Main",
            matrix: { rows: 2, cols: 4 },
            buttons: [],
        };
    }
    const mp = state.config.main_page;
    if (!mp.matrix && mp.grid) {
        mp.matrix = clampMatrix(mp.grid.rows || 2, mp.grid.cols || 4);
    }
    if (!mp.matrix) mp.matrix = { rows: 2, cols: 4 };
    if (!mp.background) mp.background = { type: "image", image: "age_bg_dark.png" };
    if (!mp.buttons) mp.buttons = [];
    return mp;
}

/* ------------------------------------------------------------------ */
/* Button style hierarchy + style manager                             */
/* ------------------------------------------------------------------ */
function fillPresetSelect(sel, inherit) {
    if (!sel) return;
    sel.innerHTML = "";
    if (inherit) {
        const o = document.createElement("option");
        o.value = "";
        o.textContent = t("pages.inherit_global");
        sel.appendChild(o);
    }
    ensureStyles();
    const names = Object.keys(state.config.styles || {});
    (names.length ? names : STYLE_KEYS).forEach((k) => {
        const o = document.createElement("option");
        o.value = k;
        o.textContent = presetLabel(k);
        sel.appendChild(o);
    });
}

function populateGlobalStyleSelect() {
    const sel = $("global-style-preset");
    if (!sel) return;
    if (!sel.options.length) fillPresetSelect(sel, false);
    const g = (state.config.defaults && state.config.defaults.button_style) || { preset: "glass" };
    sel.value = g.preset || "glass";
}

function onGlobalStyleChange() {
    const preset = $("global-style-preset").value;
    if (!state.config.defaults) state.config.defaults = {};
    state.config.defaults.button_style = presetObject(preset);
    state.config.default_style = preset;
    saveConfig();
    if ($("pages-section") && $("pages-section").classList.contains("active")) renderStyleManager();
    renderGridCanvas();    renderPreview();
}

function onPageStyleChange() {
    const page = currentPage();
    if (!page) return;
    const preset = $("page-style-preset").value;
    if (!preset) {
        delete page.button_style;
    } else {
        /* Keep the style *name* on the page so it shows up everywhere. */
        page.button_style = Object.assign(presetObject(preset), { preset });
    }
    saveConfig();
    renderGridCanvas();
    renderPreview();
    updatePageStyleStats();
    if ($("pages-section") && $("pages-section").classList.contains("active")) renderStyleManager();
}

function applyPageStyleToAllButtons() {
    const page = currentPage();
    if (!page) return;
    if (!page.button_style) {
        toast(t("pages.select_preset_first"));
        return;
    }
    (page.buttons || []).forEach((b) => { b.style = null; });
    saveConfig();
    renderGridCanvas();
    renderPreview();
    updatePageStyleStats();
    toast(t("pages.style_applied"));
}

function resetButtonsStyle() {
    const page = currentPage();
    if (!page) return;
    (page.buttons || []).forEach((b) => { b.style = null; });
    saveConfig();
    renderGridCanvas();
    renderPreview();
    updatePageStyleStats();
}

function updatePageStyleStats() {
    const page = currentPage();
    const el = $("page-style-stats");
    if (!page || !el) return;
    const total = (page.buttons || []).length;
    const custom = (page.buttons || []).filter((b) => b.style).length;
    el.textContent = t("pages.custom_count").replace("{n}", custom).replace("{total}", total);
}

/* ---- Button style source: inherit / named / unique ---- */
function onBtnStyleModeChange(skipLoad) {
    const mode = $("btn-style-mode") ? $("btn-style-mode").value : "inherit";
    if ($("btn-style-named")) $("btn-style-named").style.display = mode === "named" ? "block" : "none";
    if ($("btn-style-unique")) $("btn-style-unique").style.display = mode === "unique" ? "block" : "none";
    if (mode === "unique" && !skipLoad) {
        const btn = getCurrentButton() || {};
        loadUniqueStyle((btn.style_unique && btn.style) ? btn.style : null,
                        getEffectiveStyle(btn));
    }
    updateStyleSourceInfo();
    updateButtonPreview();
}

function applyBtnPreset() {
    updateStyleSourceInfo();
    updateButtonPreview();
}

function loadUniqueStyle(st, fallback) {
    ensureFontSelect($("bu-text-size"));
    const base = st || fallback || presetObject("glass");
    $("bu-radius").value = base.radius != null ? base.radius : 12;
    $("bu-border-width").value = base.border_width != null ? base.border_width : 0;
    $("bu-border-color").value = base.border_color || "#ffffff";
    setRangeNum("bu-border-opa", base.border_opa != null ? base.border_opa : 0);
    $("bu-btn-shadow-color").value = base.shadow_color || "#000000";
    $("bu-btn-shadow-size").value = base.shadow_width || 0;
    setRangeNum("bu-btn-shadow-strength", base.shadow_opa != null ? base.shadow_opa : 0);
    if ($("bu-btn-shadow-dir")) {
        const sdir = base.shadow_dir != null ? base.shadow_dir : ((base.shadow_ofs_y || 0) > 0 ? 7 : 4);
        $("bu-btn-shadow-dir").value = String(sdir);
    }
    const tx = base.text || defaultTextStyle();
    if ($("bu-text-size")) $("bu-text-size").value = String(tx.size || 14);
    $("bu-text-bold").checked = !!tx.bold;
    $("bu-text-color").value = tx.color || "#ffffff";
    $("bu-shadow-color").value = tx.shadow_color || "#000000";
    $("bu-shadow-size").value = tx.shadow_size || 0;
    setRangeNum("bu-shadow-strength", tx.shadow_strength != null ? tx.shadow_strength : 0);
    if ($("bu-shadow-dir")) $("bu-shadow-dir").value = String(tx.shadow_dir != null ? tx.shadow_dir : 4);
    const cap = base.caption || defaultCaptionStyle();
    ensureFontSelect($("bu-caption-size"));
    if ($("bu-caption-size")) $("bu-caption-size").value = String(cap.size || 12);
    $("bu-caption-bold").checked = !!cap.bold;
    $("bu-caption-color").value = cap.color || "#ffffff";
    $("bu-caption-shadow-color").value = cap.shadow_color || "#000000";
    $("bu-caption-shadow-size").value = cap.shadow_size || 0;
    setRangeNum("bu-caption-shadow-strength", cap.shadow_strength != null ? cap.shadow_strength : 0);
    if ($("bu-caption-shadow-dir")) $("bu-caption-shadow-dir").value = String(cap.shadow_dir != null ? cap.shadow_dir : 4);
}

function buRead() {
    const base = getCurrentButton() || {};
    const st = (base.style_unique && base.style) ? Object.assign({}, base.style) : presetObject("glass");
    return Object.assign({}, st, {
        preset: st.preset || "glass",
        radius: Number($("bu-radius").value) || 0,
        border_width: Number($("bu-border-width").value) || 0,
        border_color: $("bu-border-color").value,
        border_opa: Number($("bu-border-opa").value) || 0,
        shadow_width: Number($("bu-btn-shadow-size").value) || 0,
        shadow_color: $("bu-btn-shadow-color").value,
        shadow_opa: Number($("bu-btn-shadow-strength").value) || 0,
        shadow_dir: Number($("bu-btn-shadow-dir") ? $("bu-btn-shadow-dir").value : 4),
        text: {
            size: Number($("bu-text-size") ? $("bu-text-size").value : 14) || 14,
            bold: $("bu-text-bold").checked,
            color: $("bu-text-color").value,
            shadow_dir: Number($("bu-shadow-dir") ? $("bu-shadow-dir").value : 4),
            shadow_color: $("bu-shadow-color").value,
            shadow_size: Number($("bu-shadow-size").value) || 0,
            shadow_strength: Number($("bu-shadow-strength").value) || 0,
        },
        caption: {
            size: Number($("bu-caption-size") ? $("bu-caption-size").value : 12) || 12,
            bold: $("bu-caption-bold").checked,
            color: $("bu-caption-color").value,
            shadow_dir: Number($("bu-caption-shadow-dir") ? $("bu-caption-shadow-dir").value : 4),
            shadow_color: $("bu-caption-shadow-color").value,
            shadow_size: Number($("bu-caption-shadow-size").value) || 0,
            shadow_strength: Number($("bu-caption-shadow-strength").value) || 0,
        },
    });
}

function updateStyleSourceInfo() {
    const btn = getCurrentButton();
    const page = currentPage();
    const el = $("style-source-info");
    if (!el) return;
    const mode = $("btn-style-mode") ? $("btn-style-mode").value : "inherit";
    if (mode === "named") {
        const sel = $("btn-style-preset");
        el.textContent = t("buttons.style_named") + ": " + (sel ? presetLabel(sel.value) : "");
    } else if (mode === "unique") {
        el.textContent = t("buttons.style_unique");
    } else if (page && page.button_style) {
        el.textContent = t("buttons.inherits_page");
    } else {
        el.textContent = t("buttons.inherits_global");
    }
}

/* ---- range <-> number helpers ---- */
function setRangeNum(id, val) {
    const r = document.getElementById(id);
    const n = document.getElementById(id + "-num");
    const v = Math.max(0, Math.min(100, Number(val) || 0));
    if (r) r.value = v;
    if (n) n.value = v;
    if (r && window.__fillRange) window.__fillRange(r);
}
function styleSyncNum(id) { styleSyncRange(id, true); }
function styleSyncRange(id, fromRange) {
    const r = document.getElementById(id);
    const n = document.getElementById(id + "-num");
    if (!r || !n) return;
    if (fromRange) n.value = r.value; else r.value = n.value;
    renderStylePreview();
}
function buSyncNum(id) { buSyncRange(id, true); }
function buSyncRange(id, fromRange) {
    const r = document.getElementById(id);
    const n = document.getElementById(id + "-num");
    if (!r || !n) return;
    if (fromRange) n.value = r.value; else r.value = n.value;
    updateButtonPreview();
}
function ensureFontSelect(sel) {
    if (!sel || sel.options.length) return;
    FONT_SIZES.forEach((n) => {
        const o = document.createElement("option");
        o.value = String(n);
        o.textContent = n + " px";
        sel.appendChild(o);
    });
}

/* ---- Style editor: read inputs + live preview ---- */
function readStyleEditor() {
    const prev = (editingStyle && state.config.styles[editingStyle]) || {};
    return {
        preset: prev.preset || "glass",
        radius: Number($("style-radius").value) || 0,
        border_width: Number($("style-border-width").value) || 0,
        border_color: $("style-border-color").value,
        border_opa: Number($("style-border-opa").value) || 0,
        shadow_width: Number($("style-btn-shadow-size").value) || 0,
        shadow_ofs_y: prev.shadow_ofs_y != null ? prev.shadow_ofs_y : 0,
        shadow_color: $("style-btn-shadow-color").value,
        shadow_opa: Number($("style-btn-shadow-strength").value) || 0,
        shadow_dir: Number($("style-btn-shadow-dir") ? $("style-btn-shadow-dir").value : 4),
        text: {
            size: Number($("style-text-size") ? $("style-text-size").value : 14) || 14,
            bold: $("style-text-bold").checked,
            color: $("style-text-color").value,
            shadow_dir: Number($("style-shadow-dir") ? $("style-shadow-dir").value : 4),
            shadow_color: $("style-shadow-color").value,
            shadow_size: Number($("style-shadow-size").value) || 0,
            shadow_strength: Number($("style-shadow-strength").value) || 0,
        },
        caption: {
            size: Number($("style-caption-size") ? $("style-caption-size").value : 12) || 12,
            bold: $("style-caption-bold").checked,
            color: $("style-caption-color").value,
            shadow_dir: Number($("style-caption-shadow-dir") ? $("style-caption-shadow-dir").value : 4),
            shadow_color: $("style-caption-shadow-color").value,
            shadow_size: Number($("style-caption-shadow-size").value) || 0,
            shadow_strength: Number($("style-caption-shadow-strength").value) || 0,
        },
    };
}

function applyPreviewStyle(st) {
    const el = $("style-preview-btn");
    if (!el || !st) return;
    el.style.background = "linear-gradient(180deg,#8fa1bb,#5d6e88)";
    applyStyleCss(el, st);
    const tx = st.text || defaultTextStyle();
    const txt = $("style-preview-text");
    if (txt) {
        txt.textContent = "Aa";
        txt.style.fontSize = (tx.size || 14) + "px";
        txt.style.fontWeight = tx.bold ? "700" : "400";
        txt.style.color = tx.color || "#ffffff";
        txt.style.textShadow = textShadowCss(tx);
    }
    const capEl = $("style-preview-caption");
    if (capEl) {
        capEl.textContent = "Caption";
        applyCaptionCss(capEl, st.caption);
    }
}

function renderStylePreview() {
    applyPreviewStyle(readStyleEditor());
}

/* Apply a caption style block to a caption element. An empty shadow falls back
 * to the element's stylesheet default (readability shadow on the device mock). */
function applyCaptionCss(el, cap) {
    if (!el) return;
    cap = cap || defaultCaptionStyle();
    el.style.fontSize = (cap.size || 12) + "px";
    el.style.fontWeight = cap.bold ? "700" : "400";
    el.style.color = cap.color || "#ffffff";
    /* Always set it explicitly ("none" included): otherwise the element would
     * inherit a default text-shadow from CSS and captions without a shadow
     * (e.g. VS Code) would look bold/black on the preview. */
    const sh = textShadowCss(cap);
    el.style.textShadow = (sh && sh !== "none") ? sh : "none";
}

/* ---- Page background (source: data/images/pages) ---- */
const PAGE_BG_DIRS = ["images/pages", "sd:images/pages"];

function updatePageBgUi() {
    const page = currentPage();
    if (!page) return;
    const bg = page.background || {};
    const type = bg.type === "solid" ? "solid" : (bg.type === "gradient" ? "gradient" : "image");
    if ($("page-bg-type")) $("page-bg-type").value = type;
    if ($("page-bg-color")) $("page-bg-color").value = bg.color || "#1a1a2e";
    renderGradientPresets($("page-bg-grad-presets"), onPageGradPreset, type === "gradient" ? bg : null);
    if ($("page-grad-color1")) $("page-grad-color1").value = (type === "gradient" && bg.color) ? bg.color : "#1E3A8A";
    if ($("page-grad-color2")) $("page-grad-color2").value = (type === "gradient" && bg.color2) ? bg.color2 : "#0EA5E9";
    if ($("page-grad-dir")) $("page-grad-dir").value = bg.direction === "horizontal" ? "horizontal" : "vertical";
    if ($("page-bg-solid-wrap")) $("page-bg-solid-wrap").style.display = type === "solid" ? "block" : "none";
    if ($("page-bg-gradient-wrap")) $("page-bg-gradient-wrap").style.display = type === "gradient" ? "block" : "none";
    if ($("page-bg-image-wrap")) $("page-bg-image-wrap").style.display = type === "image" ? "block" : "none";
}

/* Shared background picker (used by a page and by the main page). */
function fillBgPicker(picker, current, onPick) {
    if (!picker) return;
    const token = String((Number(picker.dataset.token) || 0) + 1);
    picker.dataset.token = token;
    picker.innerHTML = "";
    (async () => {
        for (const dir of PAGE_BG_DIRS) {
            const items = await getJson(`/api/images?dir=${encodeURIComponent(dir)}`, []);
            if (picker.dataset.token !== token) return; /* superseded by a newer call */
            for (const it of items) {
                if (it.dir) continue;
                if (!/\.(png|jpe?g)$/i.test(it.name)) continue;
                const el = document.createElement("div");
                el.className = "pick-item" + (it.name === current ? " selected" : "");
                const img = document.createElement("img");
                img.src = assetUrl(dir, it.name);
                img.title = it.name + (/\.png$/i.test(it.name) ? "" : " (JPG)");
                img.loading = "lazy";
                el.appendChild(img);
                el.onclick = () => onPick(it.name, el);
                picker.appendChild(el);
            }
        }
    })();
}

function populatePageBgPicker() {
    const page = currentPage();
    fillBgPicker($("page-bg-picker"), page && page.background ? page.background.image : "", onPageBgPick);
}

function onPageBgPick(name, el) {
    const page = currentPage();
    if (!page) return;
    page.background = { type: "image", image: name };
    setSyncNeeded(true);
    document.querySelectorAll("#page-bg-picker .pick-item").forEach((n) => n.classList.remove("selected"));
    if (el) el.classList.add("selected");
    if (!/\.png$/i.test(name)) {
        toast(t("pages.png_only_warning"));
    }
    saveConfig();
    renderGridCanvas();
    renderPreview();
}

function onPageBgTypeChange() {
    const page = currentPage();
    if (!page) return;
    const t = $("page-bg-type").value;
    if (t === "solid") {
        page.background = { type: "solid", color: $("page-bg-color").value || "#1a1a2e" };
    } else if (t === "gradient") {
        page.background = {
            type: "gradient",
            color: $("page-grad-color1").value || "#1E3A8A",
            color2: $("page-grad-color2").value || "#0EA5E9",
            direction: $("page-grad-dir").value || "vertical",
        };
    } else {
        page.background = { type: "image", image: (page.background && page.background.image) || "age_bg_dark.png" };
        setSyncNeeded(true);
    }
    updatePageBgUi();
    saveConfig();
    renderGridCanvas();
    renderPreview();
}

function onPageBgColorChange() {
    const page = currentPage();
    if (!page) return;
    if (page.background && page.background.type === "gradient") {
        page.background.color = $("page-grad-color1").value;
        page.background.color2 = $("page-grad-color2").value;
        page.background.direction = $("page-grad-dir").value;
    } else {
        page.background = { type: "solid", color: $("page-bg-color").value };
    }
    saveConfig();
    renderGridCanvas();
    renderPreview();
}

function onPageGradPreset(key) {
    const p = GRADIENT_PRESETS[key];
    if (p) {
        $("page-grad-color1").value = p.color;
        $("page-grad-color2").value = p.color2;
    }
    onPageBgColorChange();
}

/* ---- Style manager ---- */
/* Page object by manager index: -1 = the main page, otherwise pages[i]. */
function pageByIndex(idx) {
    return idx === -1 ? mainPageObj() : state.config.pages[idx];
}

function renderStyleManager() {
    const pagesList = $("manager-pages-list");
    if (!pagesList) return;

    fillPresetSelect($("manager-page-preset"), false);
    fillPresetSelect($("manager-btn-preset"), false);

    const checkedPages = new Set(getSelectedPages());
    const checkedButtons = new Set(getSelectedButtons().map((s) => s.pageIdx + ":" + s.btnIdx));
    pagesList.innerHTML = "";
    /* Every page, including the main page (index -1). */
    const entries = [{ idx: -1, page: mainPageObj() }];
    (state.config.pages || []).forEach((page, i) => entries.push({ idx: i, page }));
    entries.forEach(({ idx, page }) => {
        const styleName = page.button_style ? presetLabel(page.button_style.preset) : t("manager.global");
        const label = idx === -1 ? t("main_page.title") : (page.display_name || page.name || page.id);
        const row = document.createElement("label");
        row.className = "manager-row";
        row.innerHTML =
            `<input type="checkbox" data-page="${idx}" ${checkedPages.has(idx) ? "checked" : ""}>` +
            `<span>${label}</span>` +
            `<span class="manager-style-badge">${styleName}</span>`;
        row.querySelector("input").addEventListener("change", () => updateManagerButtonsList());
        pagesList.appendChild(row);
    });
    updateManagerButtonsList(checkedButtons);
}

function updateManagerButtonsList(checkedButtons) {
    const buttonsList = $("manager-buttons-list");
    if (!buttonsList) return;
    const keep = checkedButtons instanceof Set ? checkedButtons : new Set();
    const selected = [...document.querySelectorAll("#manager-pages-list input:checked")].map((cb) => parseInt(cb.dataset.page, 10));
    buttonsList.innerHTML = "";
    selected.forEach((pageIdx) => {
        const page = pageByIndex(pageIdx);
        if (!page) return;
        (page.buttons || []).forEach((btn, btnIdx) => {
            const styleName = btn.style
                ? presetLabel(btn.style.preset)
                : (page.button_style ? presetLabel(page.button_style.preset) : t("manager.global"));
            const row = document.createElement("label");
            row.className = "manager-row";
            row.innerHTML =
                `<input type="checkbox" data-page="${pageIdx}" data-btn="${btnIdx}" ${keep.has(pageIdx + ":" + btnIdx) ? "checked" : ""}>` +
                `<span>${page.display_name || page.name || page.id} / ${btn.caption || btn.id}</span>` +
                `<span class="manager-style-badge">${styleName}</span>`;
            buttonsList.appendChild(row);
        });
    });
}

function applyStyleToSelectedPages() {
    const preset = $("manager-page-preset").value;
    const selected = getSelectedPages();
    selected.forEach((idx) => {
        const page = pageByIndex(idx);
        if (!page) return;
        page.button_style = Object.assign(presetObject(preset), { preset });
        (page.buttons || []).forEach((b) => { b.style = null; });
    });
    saveConfig();
    renderStyleManager();
    renderGridCanvas();
    renderPreview();
    toast(t("manager.applied_pages").replace("{n}", selected.length));
}

function resetSelectedPages() {
    const selected = getSelectedPages();
    selected.forEach((idx) => {
        const page = pageByIndex(idx);
        if (page) delete page.button_style;
    });
    saveConfig();
    renderStyleManager();
    renderGridCanvas();
    renderPreview();
}

function applyStyleToSelectedButtons() {
    const preset = $("manager-btn-preset").value;
    const selected = getSelectedButtons();
    selected.forEach(({ pageIdx, btnIdx }) => {
        const page = pageByIndex(pageIdx);
        const btn = page && page.buttons[btnIdx];
        if (btn) {
            /* Reference the named style so the button shows it and follows edits. */
            btn.style = { preset: preset };
            btn.style_unique = false;
        }
    });
    saveConfig();
    renderStyleManager();
    renderGridCanvas();
    renderPreview();
    toast(t("manager.applied_buttons").replace("{n}", selected.length));
}

function resetSelectedButtons() {
    const selected = getSelectedButtons();
    selected.forEach(({ pageIdx, btnIdx }) => {
        const page = pageByIndex(pageIdx);
        if (page && page.buttons[btnIdx]) page.buttons[btnIdx].style = null;
    });
    saveConfig();
    renderStyleManager();
    renderGridCanvas();
    renderPreview();
}

function getSelectedPages() {
    return [...document.querySelectorAll("#manager-pages-list input:checked")].map((cb) => parseInt(cb.dataset.page, 10));
}

function getSelectedButtons() {
    return [...document.querySelectorAll("#manager-buttons-list input:checked")].map((cb) => ({
        pageIdx: parseInt(cb.dataset.page, 10),
        btnIdx: parseInt(cb.dataset.btn, 10),
    }));
}

/* ------------------------------------------------------------------ */
/* Boot                                                               */
/* ------------------------------------------------------------------ */
async function init() {
    const savedLang = localStorage.getItem("lang");
    const browserLang = (navigator.language || "en").split("-")[0];
    state.lang = savedLang || (browserLang === "ru" ? "ru" : "en");
    await loadTranslations(state.lang);
    markLangButtons();

    state.settings = await getJson("/api/settings", {});
    state.config = await getJson("/api/config", { pages: [] });
    if (!state.config.pages) state.config.pages = [];
    if (!state.config.defaults) state.config.defaults = {};
    if (!state.config.defaults.button_style) state.config.defaults.button_style = presetObject("glass");
    migrateConfig();

    fillPresetSelect($("page-style-preset"), true);
    fillPresetSelect($("btn-style-preset"), false);
    populateGlobalStyleSelect();

    await buildImageIndex();

    applySettingsToUI();
    applyConfigToUI();
    refreshIssues();
    buildHotkeyKeys("btn-hotkey-keys", "btn-hotkey");
    buildHotkeyKeys("main-btn-keys-keys", "main-btn-keys");
    renderPagesList();
    populatePageSelect();
    bindUploadDrag();
    loadPageButtons();
    renderPreview();    renderStyleManager();

    const mainName = $("main-page-name");
    if (mainName) {
        mainName.onchange = () => {
            const page = mainPageObj();
            page.display_name = mainName.value;
            page.name = mainName.value;
            scheduleSave();
        };
    }

    document.querySelectorAll(".nav-btn").forEach((b) => (b.onclick = () => showSection(b.dataset.section)));
}

window.addEventListener("load", init);

/* ================================================================== */
/* M15-M21: progressive enhancements appended after the core script    */
/* ================================================================== */

/* [M15] two-step confirmation for dangerous actions */
window.ARM_CONFIRM = window.ARM_CONFIRM || "Sure?";
window.arm = function (btn, fn) {
    const t = btn.querySelector(".lbl") || btn;
    if (btn.classList.contains("armed")) {
        btn.classList.remove("armed");
        t.textContent = btn.dataset.origText || "";
        fn();
        return;
    }
    btn.dataset.origText = t.textContent;
    btn.classList.add("armed");
    t.textContent = window.ARM_CONFIRM;
    clearTimeout(btn._t);
    btn._t = setTimeout(function () {
        btn.classList.remove("armed");
        t.textContent = btn.dataset.origText || "";
    }, 2600);
};

/* [M16] dirty marker on tabs with unsaved edits */
(function () {
    const tabs = document.querySelectorAll(".nav-btn");
    window.__clearDirty = function (key) {
        tabs.forEach(function (b) { if (b.dataset.section === key) b.classList.remove("dirty"); });
    };
    function mark(e) {
        const sec = e.target && e.target.closest ? e.target.closest(".settings-section") : null;
        if (!sec) return;
        const key = sec.id.replace("-section", "");
        tabs.forEach(function (b) { if (b.dataset.section === key) b.classList.add("dirty"); });
    }
    document.addEventListener("input", mark, true);
    document.addEventListener("change", mark, true);
})();

/* [M17] fill range tracks */
(function () {
    function fill(el) {
        const min = Number(el.min) || 0;
        const maxRaw = Number(el.max);
        const max = (isFinite(maxRaw) && maxRaw !== min) ? maxRaw : 100;
        const pct = ((Number(el.value) - min) / (max - min)) * 100;
        el.style.setProperty("--p", (isFinite(pct) ? pct : 0) + "%");
    }
    window.__fillRange = fill;
    window.__fillRanges = function () {
        document.querySelectorAll('input[type="range"]').forEach(fill);
    };
    document.addEventListener("input", function (e) {
        if (e.target && e.target.type === "range") fill(e.target);
    }, true);
    window.addEventListener("load", window.__fillRanges);
    window.__fillRanges();
})();

/* [M20] live brightness dim on the preview */
window.setUiBrightness = function (v) {
    document.documentElement.style.setProperty("--bright", (Math.max(1, Number(v)) / 100).toFixed(2));
};

/* [M21] fit-scale 480x320 canvases */
(function () {
    function fit() {
        document.querySelectorAll(".scale-wrap").forEach(function (w) {
            const inner = w.firstElementChild;
            if (inner && w.clientWidth) inner.style.transform = "scale(" + (w.clientWidth / 480) + ")";
        });
    }
    window.__fitCanvases = fit;
    window.addEventListener("resize", fit);
    window.addEventListener("load", fit);
    fit();
})();

