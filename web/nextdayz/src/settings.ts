export const bindings = {
    forward: "KeyW", back: "KeyS", left: "KeyA", right: "KeyD",
    walk: "KeyX", sprint: "ShiftLeft", freeLook: "AltLeft", jump: "Space",
    crouch: "KeyC", view: "KeyV", reload: "KeyR",
    slot1: "Digit1", slot2: "Digit2", slot3: "Digit3", previous: "KeyQ",
    interact: "KeyE", inventory: "Tab", map: "KeyM", extract: "KeyG",
};
export type Binding = keyof typeof bindings;
type Language = "zh-CN" | "en";

export interface Settings {
    language: Language;
    sensitivity: number;
    volume: number;
    quality: "balanced" | "shadows";
    keys: Record<Binding, string>;
}

const storageKey = "nextdayz.settings.v1";

function readSettings(): Settings {
    const defaults: Settings = { language: "zh-CN", sensitivity: 1, volume: .72, quality: "balanced", keys: { ...bindings } };
    try {
        const prior = JSON.parse(localStorage.getItem(storageKey) ?? "{}") as Partial<Settings>;
        if (prior.language === "en") defaults.language = "en";
        if (prior.quality === "shadows") defaults.quality = "shadows";
        if (typeof prior.sensitivity === "number" && prior.sensitivity >= .15 && prior.sensitivity <= 3)
            defaults.sensitivity = prior.sensitivity;
        if (typeof prior.volume === "number" && prior.volume >= 0 && prior.volume <= 1) defaults.volume = prior.volume;
        for (const key of Object.keys(bindings) as Binding[]) {
            const code = prior.keys?.[key];
            if (typeof code === "string" && code !== "Escape") defaults.keys[key] = code;
        }
    } catch { /* Corrupt settings fall back to defaults. */ }
    return defaults;
}

export const settings = readSettings();

export function saveSettings() {
    localStorage.setItem(storageKey, JSON.stringify(settings));
    document.documentElement.lang = settings.language;
}

// UI text comes from the translation table baked from the desktop's UI/Translations.inl and
// is keyed by its English source. `{}` placeholders are filled in order, as on desktop. Until
// the table has loaded, the English source is shown.
let translations: Record<string, string> = {};
export function useTranslations(table: Record<string, string>) { translations = table; }
export function tr(source: string, ...args: (string | number)[]) {
    const translated = settings.language === "en" ? source : translations[source] || source;
    return translated.split("{}").reduce((out, part, index) => out + (index ? String(args[index - 1] ?? "") : "") + part, "");
}

export function keyName(code: string) {
    if (code === "Space") return tr("Space");
    return code.replace(/^Key|^Digit/, "").replace(/Left$|Right$/, "");
}

export function matches(binding: Binding, code: string) {
    const key = settings.keys[binding];
    return key === code || ((binding === "sprint" || binding === "freeLook") && key === bindings[binding] &&
        code === key.replace("Left", "Right"));
}

export function translateBindings() {
    const names: Record<Binding, string> = {
        forward: tr("Move forward"), back: tr("Move back"), left: tr("Move left"), right: tr("Move right"),
        walk: tr("Walk"), sprint: tr("Sprint"), jump: tr("Jump / vault"),
        freeLook: tr("Free look"), crouch: tr("Crouch"), view: tr("Toggle view"),
        reload: tr("Reload"), slot1: tr("Primary"), slot2: tr("Secondary"),
        slot3: tr("Sidearm"), previous: tr("Previous weapon"),
        interact: tr("Interact"), inventory: tr("Inventory"), map: tr("Map"),
        extract: tr("Extract"),
    };
    return names;
}
