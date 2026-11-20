import { bindings, keyName, saveSettings, settings, text, translateBindings, type Binding } from "./settings";

export function renderOptions(panel: HTMLElement, changed: () => void) {
    const general = document.createElement("div");
    general.className = "settings-general";
    const entry = (label: string, control: HTMLElement) => {
        const row = document.createElement("label");
        row.append(label, control);
        general.append(row);
    };
    const select = (items: [string, string][], value: string, choose: (next: string) => void) => {
        const node = document.createElement("select");
        for (const [id, name] of items) node.append(new Option(name, id, false, id === value));
        node.addEventListener("change", () => { choose(node.value); changed(); });
        return node;
    };
    const slider = (value: number, lower: number, upper: number, update: (next: number) => void) => {
        const node = document.createElement("input");
        node.type = "range";
        node.min = String(lower);
        node.max = String(upper);
        node.step = "0.01";
        node.value = String(value);
        node.addEventListener("input", () => { update(Number(node.value)); saveSettings(); });
        node.addEventListener("change", changed);
        return node;
    };
    entry(text("语言", "Language"), select([["zh-CN", "简体中文"], ["en", "English"]], settings.language,
        value => { settings.language = value === "en" ? "en" : "zh-CN"; }));
    entry(text("画质", "Graphics"), select([["balanced", text("均衡", "Balanced")], ["shadows", text("级联阴影", "Cascaded shadows")]],
        settings.quality, value => { settings.quality = value === "shadows" ? "shadows" : "balanced"; }));
    entry(text("鼠标灵敏度", "Mouse sensitivity"), slider(settings.sensitivity, .15, 3, value => { settings.sensitivity = value; }));
    entry(text("音量", "Volume"), slider(settings.volume, 0, 1, value => { settings.volume = value; }));

    const keys = document.createElement("div");
    keys.className = "key-bindings";
    const names = translateBindings();
    for (const key of Object.keys(bindings) as Binding[]) {
        const row = document.createElement("div");
        const label = document.createElement("span");
        label.textContent = names[key];
        const button = document.createElement("button");
        button.type = "button";
        button.className = "quiet";
        button.textContent = keyName(settings.keys[key]);
        button.setAttribute("aria-label", names[key]);
        button.addEventListener("click", () => {
            button.dataset.waiting = "yes";
            button.textContent = text("按下新键", "Press a key");
        });
        button.addEventListener("blur", () => {
            delete button.dataset.waiting;
            button.textContent = keyName(settings.keys[key]);
        });
        button.addEventListener("keydown", event => {
            if (!button.dataset.waiting) return;
            event.preventDefault();
            event.stopPropagation();
            if (event.code !== "Escape" && event.code !== "Unidentified") {
                const old = settings.keys[key];
                const duplicate = (Object.keys(bindings) as Binding[]).find(name => name !== key && settings.keys[name] === event.code);
                if (duplicate) settings.keys[duplicate] = old;
                settings.keys[key] = event.code;
            }
            changed();
        });
        row.append(label, button);
        keys.append(row);
    }
    const reset = document.createElement("button");
    reset.type = "button";
    reset.className = "quiet";
    reset.textContent = text("恢复默认键位", "Reset key bindings");
    reset.addEventListener("click", () => { settings.keys = { ...bindings }; changed(); });
    panel.replaceChildren(general, keys, reset);
}
