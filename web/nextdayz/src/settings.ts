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

export function text(zh: string, en: string) { return settings.language === "en" ? en : zh; }

export function keyName(code: string) {
    if (code === "Space") return text("空格", "Space");
    return code.replace(/^Key|^Digit/, "").replace(/Left$|Right$/, "");
}

export function matches(binding: Binding, code: string) {
    const key = settings.keys[binding];
    return key === code || ((binding === "sprint" || binding === "freeLook") && key === bindings[binding] &&
        code === key.replace("Left", "Right"));
}

export function translateBindings() {
    const names: Record<Binding, string> = {
        forward: text("前进", "Forward"), back: text("后退", "Back"), left: text("左移", "Left"), right: text("右移", "Right"),
        walk: text("慢走", "Walk"), sprint: text("冲刺", "Sprint"), jump: text("跳跃 / 翻越", "Jump / vault"),
        freeLook: text("自由观察", "Free look"), crouch: text("蹲伏", "Crouch"), view: text("切换视角", "Camera"),
        reload: text("换弹", "Reload"), slot1: text("主武器", "Primary"), slot2: text("副武器", "Secondary"),
        slot3: text("手枪", "Sidearm"), previous: text("切回上一把", "Previous weapon"),
        interact: text("交互", "Interact"), inventory: text("背包", "Inventory"), map: text("地图", "Map"),
        extract: text("撤离", "Extract"),
    };
    return names;
}
