import { Client, loopbackMaster } from "../shared/client.mjs";
import { Assets, assetURL, loadLevel, manifest, type Level } from "./assets";
import { World, type Quality } from "./world";
import { installCollision } from "../shared/collision.mjs";
import { Actors, loadActors, type ActorAssets } from "./actors";
import { FieldAudio } from "./audio";
import { compass, drawFieldMap } from "./fieldMap";
import { Pack } from "./pack";
import { renderOptions } from "./optionsPanel";
import { keyName, matches, saveSettings, settings, text, type Binding } from "./settings";
import type { Clock, Frame, Loot, Manifest, Profile, XYZ } from "./types";

function element<T extends HTMLElement>(id: string): T {
    const found = document.getElementById(id);
    if (!found) throw Error(`Missing UI element: ${id}`);
    return found as T;
}

const login = element<HTMLFormElement>("login");
const loginButton = element<HTMLButtonElement>("login-button");
const account = element<HTMLInputElement>("name");
const password = element<HTMLInputElement>("password");
const pageBuild = document.querySelector<HTMLMetaElement>('meta[name="nextdayz-build"]')?.content ?? "";
const packaged = /^[0-9a-f]{64}$/.test(pageBuild);
let passwordRequired = false;
const server = element<HTMLSelectElement>("server");
const profilePanel = element("profile");
const storage = element("storage");
const maps = element("maps");
const loading = element("loading");
const progress = element<HTMLProgressElement>("progress");
const lobby = element("lobby");
const field = element("field");
const menu = element("menu");
const notice = element("notice");
const controls = element("controls");
const fieldHint = element("field-hint");
const aimPoint = element("aim-point");
const pack = element("pack");
const items = element("items");
const course = element("course");
const range = element("range");
const pen = element("pen");
const mapPanel = element("map-panel");
const mapCanvas = element<HTMLCanvasElement>("map-canvas");
const settingsDialog = element<HTMLDialogElement>("settings-dialog");
const deathPanel = element("death-panel");
const hitMarker = element("hit-marker");
const reviewEnabled = import.meta.env.DEV && import.meta.env.VITE_ND_REVIEW === "1";
const trainingScad = import.meta.env.DEV && import.meta.env.VITE_ND_SCAD_TRAINING === "1";
const deployButton = element<HTMLButtonElement>("deploy");
const weather = { Clear: ["晴朗", "Clear"], Overcast: ["阴天", "Overcast"], Fog: ["雾天", "Fog"] } as const;
let client: Client;
let frame: Frame;
let assets: Manifest;
let actorAssets: ActorAssets;
let loaded: Level | undefined;
let world: World | undefined;
let app: unknown;
let phase = "";
let profile: Profile;
let refreshAt = 0;
let profileRefreshAt = 0;
let latestFrame = 0;
let mapLoading = false;
let viewShown = false;
let lightPreview: Clock | undefined;
let nearby: Loot | null = null;
let inventoryPanel: Pack;
let audio: FieldAudio;
let translations: Record<string, string> = {};
let mapImage: HTMLImageElement | undefined;
let groundAt = 0;
const hotkey = (binding: Binding) => keyName(settings.keys[binding]);
const itemName = (source: string) => settings.language === "en" ? source : translations[source] || source;
const bring: HTMLInputElement[] = [];

function state(message: string) {
    const translated = itemName(message);
    notice.textContent = translated;
    if (message === "Web client version changed. Refresh the page before deploying.") {
        notice.textContent = text("Web 客户端已更新，请刷新页面后再出击。", message);
    }
    const local = element("pack-notice");
    local.hidden = !translated;
    local.textContent = translated;
}

function updateBring() {
    let total = 0;
    for (const input of bring) {
        if (input.disabled) continue;
        const count = Math.max(0, Math.min(Number(input.max), Math.floor(Number(input.value) || 0)));
        input.value = String(count);
        total += count;
    }
    element("bring-count").textContent = total ? text(`带出 ${total} 件`, `Bringing ${total} items`) : "";
}

function showProfile(data: Profile) {
    profile = data;
    element("profile-name").textContent = profile.name;
    element("profile-activity").textContent = text(`出击 ${profile.sorties} · 撤离 ${profile.extractions} · 死亡 ${profile.deaths}`, `Sorties ${profile.sorties} · Extractions ${profile.extractions} · Deaths ${profile.deaths}`);
    storage.replaceChildren();
    bring.length = 0;
    const catalog = assets.shared?.files ?? {};
    if (!profile.storage.length) {
        const empty = document.createElement("p");
        empty.textContent = text("仓库暂无物品", "Storage is empty");
        storage.append(empty);
    }
    for (const item of profile.storage) {
        const row = document.createElement("div");
        row.className = "row";
        const icon = document.createElement("div");
        const file = catalog[`icons/${item.id}.webp`];
        if (file) {
            const image = document.createElement("img");
            image.src = assetURL(file.path);
            image.alt = "";
            image.loading = "lazy";
            icon.append(image);
        }
        const label = document.createElement("div");
        const name = document.createElement("span");
        name.textContent = itemName(item.name);
        const count = document.createElement("small");
        count.textContent = text(`库存 ${item.count}`, `Stored ${item.count}`);
        label.append(name, count);
        if (item.description) label.title = itemName(item.description);
        const input = document.createElement("input");
        input.type = "number";
        input.min = "0";
        input.max = String(item.count);
        input.value = "0";
        input.disabled = Boolean(profile.character);
        input.setAttribute("aria-label", text(`带出 ${itemName(item.name)} 的数量`, `Quantity to bring for ${item.name}`));
        input.addEventListener("change", updateBring);
        bring.push(input);
        row.append(icon, label, input);
        storage.append(row);
    }
    maps.replaceChildren();
    let selected = false;
    for (const map of profile.maps) {
        const entry = assets.maps[map.id];
        const label = document.createElement("label");
        const radio = document.createElement("input");
        radio.type = "radio";
        radio.name = "map";
        radio.value = map.id;
        radio.disabled = !entry || entry.map.scene !== map.scene || (!!profile.character && profile.character.map !== map.id);
        radio.checked = !radio.disabled && !selected;
        selected ||= radio.checked;
        const title = document.createElement("span");
        title.textContent = text(entry?.map.localName || map.name, map.name);
        const subtitle = document.createElement("small");
        subtitle.textContent = entry ? `${map.name} · v${entry.map.mapVersion}` : text("Web 地图资产待发布", "Web assets pending");
        title.append(subtitle);
        label.append(radio, title);
        maps.append(label);
    }
    deployButton.disabled = !selected || Boolean(profile.playing);
    deployButton.textContent = profile.character ? text("回到野外", "Resume expedition") : text("出击", "Deploy");
    element("carry-hint").textContent = profile.playing
        ? text("幸存者仍由 DS 保管，正在刷新状态", "The map server is saving your survivor")
        : profile.character ? text("当前幸存者将带着原有装备返回地图", "Resume with your current equipment")
            : text("按需从仓库选择带出数量", "Choose what to bring from storage");
    updateBring();
}

async function prepare(scene: string) {
    const bundle = Object.values(assets.maps).find(entry => entry.map.scene === scene);
    if (!bundle) throw Error(text("这张地图的 Web 资产尚未发布", "Web assets for this map are missing"));
    mapLoading = true;
    const title = text(bundle.map.localName || bundle.map.name, bundle.map.name);
    element("loading-title").textContent = title;
    element("loading-detail").textContent = text("正在读取地图清单", "Reading the map manifest");
    progress.removeAttribute("value");
    try {
        if (trainingScad && bundle.map.id === "training_ground") {
            element("loading-detail").textContent = text("正在读取 SCAD 源文件", "Reading SCAD sources");
            const { loadScadLevel } = await import("./scadLevel");
            loaded = await loadScadLevel(scene, () => {
                element("loading-detail").textContent = text("正在生成地图、材质与碰撞", "Generating terrain, materials and collision");
            });
        } else {
            loaded = await loadLevel(bundle, (done, total) => {
                progress.max = total;
                progress.value = done;
                element("loading-detail").textContent = text(`正在下载并验证资源 · ${done} / ${total}`, `Downloading and checking assets · ${done} / ${total}`);
            });
        }
        (world ??= new World(element("viewport"), actorAssets, locked => {
            menu.hidden = locked;
            controls.hidden = !locked;
            aimPoint.hidden = !locked;
            if (locked) {
                pack.hidden = true;
                mapPanel.hidden = true;
                void audio?.unlock().catch(() => {});
            }
        })).setLevel(loaded);
        world.setQuality(settings.quality);
        mapImage = undefined;
        element("loading-detail").textContent = text("地图就绪，正在加入 DS", "Map ready. Joining the server");
        const sites = loaded.anchors.filter(anchor => anchor.type === "well" || anchor.type === "extraction");
        return { spawn: loaded.spawn, collision: loaded.collision, sites };
    } finally {
        mapLoading = false;
    }
}

function command(input: Record<string, unknown>) {
    try {
        client.command(input);
    } catch (error) {
        state(error instanceof Error ? error.message : String(error));
    }
}

function session() {
    client?.destroy();
    const transport = loopbackMaster("", "/direct");
    client = new Client(app, {
        ...transport,
        fetchMaster(operation, body, token) {
            if (packaged && operation === "deploy") body = JSON.stringify({ ...JSON.parse(body), webBuild: pageBuild });
            return transport.fetchMaster(operation, body, token);
        }, loadMap: prepare, installCollision,
        present(event) { world?.present(event); audio?.event(event); },
    });
    Actors.loadKernel(client, actorAssets);
    frame = client.read();
    phase = "";
}

login.addEventListener("submit", event => {
    event.preventDefault();
    if (!account.value.trim()) return;
    localStorage.setItem("nextdayz.survivor", account.value.trim());
    command({ type: server.value === "direct" ? "direct" : "login", name: account.value.trim(), password: password.value });
    password.value = "";
});
server.addEventListener("change", () => {
    const showPassword = server.value === "master";
    password.hidden = element("password-label").hidden = !showPassword;
    password.required = showPassword && passwordRequired;
    loginButton.textContent = server.value === "direct" ? text("连接地图服务器", "Join map server") : text("进入大厅", "Enter lobby");
});
element("logout").addEventListener("click", () => {
    state("");
    session();
});
deployButton.addEventListener("click", () => {
    const chosen = maps.querySelector<HTMLInputElement>('input[name="map"]:checked');
    if (!chosen) return;
    updateBring();
    command({ type: "deploy", map: chosen.value,
        bring: bring.flatMap((input, index) => !input.disabled && Number(input.value) > 0 ? [{ index, count: Number(input.value) }] : []) });
});
element("capture").addEventListener("click", () => {
    void audio?.unlock().catch(() => {});
    void world?.lock();
});
element("spawn").addEventListener("click", () => world?.setView("survivor", frame));
element("detail").addEventListener("click", () => world?.setView("detail", frame));
element("overview").addEventListener("click", () => world?.setView("overview", frame));
element("quality").addEventListener("change", event => {
    settings.quality = (event.target as HTMLSelectElement).value as Quality;
    world?.setQuality(settings.quality);
    saveSettings();
});
element("leave").addEventListener("click", () => command({ type: "leave" }));

function showPack(show: boolean) {
    pack.hidden = !show;
    if (show) {
        mapPanel.hidden = true;
        world?.release();
        inventoryPanel.invalidate();
        inventoryPanel.refresh(client.read("cargo"), client.read("nearby"));
    }
}

function showMap(show: boolean) {
    mapPanel.hidden = !show;
    if (!show || !loaded) return;
    showPack(false);
    world?.release();
    mapImage ??= world?.mapImage();
    drawFieldMap(mapCanvas, loaded, frame, mapImage);
}

function localize() {
    saveSettings();
    element("network-label").textContent = phase === "playing" ? text("已连接 · WebRTC", "Connected · WebRTC") :
        phase === "profile" ? text("Master 已登录", "Signed into Master") : text("NextDayz 联机", "NextDayz online");
    const copies: Record<string, [string, string]> = {
        "settings-button": ["设置", "Settings"], "settings-title": ["设置", "Settings"],
        "close-settings": ["关闭", "Close"], "capture": ["进入操控", "Take control"],
        "pack-button": ["随身装备", "Inventory"], "pack-title": ["随身装备", "Inventory"], "close-pack": ["关闭", "Close"],
        "map-button": ["地图", "Map"], "map-title": ["行动地图", "Field map"], "close-map": ["关闭", "Close"],
        "course": ["前往障碍场", "Go to obstacle course"], "range": ["前往靶场", "Go to firing range"],
        "pen": ["前往感染者围栏", "Go to infected pen"], "spawn": ["幸存者视角", "Survivor view"],
        "overview": ["俯瞰地图", "Overview"], "detail": ["核心区", "Central area"],
        "leave": ["返回大厅", "Return to lobby"], "logout": ["切换幸存者", "Switch survivor"],
        "death-title": ["幸存者已死亡", "Survivor died"], "respawn": ["返回大厅并重新出击", "Return and deploy again"],
        "map-legend": ["浅蓝 当前位置 · 绿色 撤离点 · 蓝色 水源", "Light blue: you · Green: extraction · Blue: water"],
    };
    for (const [id, pair] of Object.entries(copies)) element(id).textContent = text(...pair);
    element("lobby-title").textContent = text("联机大厅", "Multiplayer lobby");
    element("name-label").textContent = text("幸存者昵称", "Survivor name");
    element("password-label").textContent = text("账号口令", "Account password");
    element("server-label").textContent = text("服务器", "Server");
    server.options[0].text = text("当前服务器", "Current server");
    server.options[1].text = text("开发直连", "Direct connection");
    element("storage-title").textContent = text("仓库", "Storage");
    element("deploy-title").textContent = text("出击地图", "Deployment map");
    server.dispatchEvent(new Event("change"));
    const quality = element<HTMLSelectElement>("quality");
    quality.options[0].text = text("均衡", "Balanced");
    quality.options[1].text = text("级联阴影", "Cascaded shadows");
    quality.value = settings.quality;
    quality.setAttribute("aria-label", text("画质", "Graphics"));
    controls.textContent = text(
        `${hotkey("forward")}${hotkey("left")}${hotkey("back")}${hotkey("right")} 移动 · 左键 开火 · 右键 瞄准 · ${hotkey("reload")} 换弹 · ${hotkey("jump")} 跳跃 / 翻越 · ${hotkey("inventory")} 背包 · ${hotkey("map")} 地图 · Esc 菜单`,
        `${hotkey("forward")}${hotkey("left")}${hotkey("back")}${hotkey("right")} Move · LMB Fire · RMB Aim · ${hotkey("reload")} Reload · ${hotkey("jump")} Jump / vault · ${hotkey("inventory")} Inventory · ${hotkey("map")} Map · Esc Menu`);
}

function applySettings() {
    localize();
    audio?.volume();
    world?.setQuality(settings.quality);
    inventoryPanel?.invalidate();
    if (phase === "profile") showProfile(client.read("profile"));
    if (settingsDialog.open) renderOptions(element("settings-content"), applySettings);
    if (client) refresh(performance.now());
}

element("settings-button").addEventListener("click", () => {
    world?.release();
    settingsDialog.showModal();
    renderOptions(element("settings-content"), applySettings);
});
element("pack-button").addEventListener("click", () => showPack(pack.hidden));
element("close-pack").addEventListener("click", () => showPack(false));
element("map-button").addEventListener("click", () => showMap(mapPanel.hidden));
element("close-map").addEventListener("click", () => showMap(false));
element("respawn").addEventListener("click", () => command({ type: "leave" }));
window.addEventListener("keydown", event => {
    if (!viewShown || event.repeat || settingsDialog.open || event.ctrlKey || event.metaKey) return;
    if (event.code === "Escape" && !world?.locked) {
        showPack(false);
        showMap(false);
    } else if (matches("inventory", event.code) && (world?.locked || !pack.hidden)) {
        event.preventDefault();
        showPack(pack.hidden);
    } else if (matches("map", event.code)) {
        event.preventDefault();
        showMap(mapPanel.hidden);
    } else if (world?.locked && matches("interact", event.code)) {
        event.preventDefault();
        command({ type: "interact" });
    } else if (world?.locked && matches("extract", event.code)) {
        event.preventDefault();
        command({ type: "extract", begin: !frame.extractSeconds });
    } else if (!pack.hidden && event.code === "KeyR") {
        event.preventDefault();
        inventoryPanel.rotate();
    }
});

const place = (position: XYZ, yaw: number) => command({ type: "reviewPlace", position, yaw });
course.addEventListener("click", () => {
    if (reviewEnabled) {
        showPack(false);
        place([-270, 40, 77], Math.PI);
        world?.setView("survivor", frame);
    }
});

range.addEventListener("click", () => {
    if (reviewEnabled) {
        showPack(false);
        showMap(false);
        place([57.875, 11.45, 58.45], Math.PI);
    }
});
pen.addEventListener("click", () => {
    if (reviewEnabled) {
        showPack(false);
        showMap(false);
        const live = frame.zombies.find(zombie => zombie.state !== 7);
        if (live) {
            const [x, y, z] = live.position;
            place([x, y + 1.1, z + 7], Math.PI);
        } else place([290, 12, -173], 0);
    }
});

function updateField() {
    const c = compass(frame.yaw);
    const direction = element("compass");
    const w = document.createElement("span");
    const m = document.createElement("b");
    const e = document.createElement("span");
    w.textContent = c.left;
    m.textContent = `${c.label} ${c.heading}°`;
    e.textContent = c.right;
    direction.replaceChildren(w, m, e);
    const { combat, body } = frame;
    element("weapon-name").textContent = itemName(combat.weapon) || text("徒手", "Unarmed");
    element("ammo").textContent = combat.capacity ? `${combat.magazine} / ${combat.reserve}` : "";
    const action = [[], ["换弹", "Reloading"], ["切换武器", "Switching"], ["近战", "Melee"]][combat.action];
    element("weapon-action").textContent = action?.length ? `${text(action[0], action[1])} ${Math.round(combat.progress * 100)}%` : "";
    hitMarker.hidden = !combat.hitMarker;
    hitMarker.classList.toggle("head", combat.headshot);
    hitMarker.classList.toggle("kill", combat.kill);
    element("damage-hud").style.opacity = String(combat.hurt * .9);
    element("pvp-status").textContent = frame.pvp ? text("友伤开启", "Friendly fire on") : text("友伤关闭", "Friendly fire off");
    deathPanel.hidden = frame.alive;
    if (!frame.alive) {
        world?.release();
        showPack(false);
        showMap(false);
        element("death-detail").textContent = itemName(body.cause) || text("可以返回大厅重新派遣。", "Return to the lobby to redeploy.");
    }
}

function refresh(timestamp: number) {
    frame = client.read();
    const playing = frame.phase === "playing";
    if (phase !== frame.phase) {
        phase = frame.phase;
        field.hidden = !playing;
        lobby.hidden = playing;
        login.hidden = phase !== "login";
        profilePanel.hidden = phase !== "profile";
        loading.hidden = !["waiting", "deploying", "joining"].includes(phase);
        if (phase === "profile") showProfile(client.read("profile"));
        if (phase === "waiting" || phase === "deploying") {
            element("loading-title").textContent = phase === "deploying" ? text("正在派遣", "Deploying") : profile ? text("正在联系 Master", "Contacting Master") : text("正在登录", "Signing in");
            element("loading-detail").textContent = phase === "deploying" ? text("正在等待地图服务器就绪", "Waiting for map server") : text("正在同步幸存者档案", "Syncing survivor profile");
            progress.removeAttribute("value");
        }
        if (phase === "joining" && !mapLoading) {
            element("loading-title").textContent = text("正在加入地图服务器", "Joining map server");
            element("loading-detail").textContent = text("正在建立 WebRTC 连接", "Connecting WebRTC");
        }
        element("network-label").textContent = playing ? text("已连接 · WebRTC", "Connected · WebRTC")
            : phase === "profile" ? text("Master 已登录", "Signed into Master") : text("NextDayz 联机", "NextDayz online");
    }
    if (playing && world && loaded) {
        if (!viewShown) {
            world.enter(frame);
            groundAt = 0;
            viewShown = true;
        }
        const bundle = Object.values(assets.maps).find(entry => entry.map.scene === frame.scene);
        element("map-name").textContent = bundle ? `${text(bundle.map.localName || bundle.map.name, bundle.map.name)} · v${bundle.map.mapVersion}` : frame.scene;
        course.hidden = range.hidden = pen.hidden = !reviewEnabled || bundle?.map.id !== "training_ground" || !frame.alive;
        const { health, hunger, hydration, bleeding, cold } = frame.body;
        const conditions = [bleeding ? text(`流血 ${bleeding}`, `Bleeding ${bleeding}`) : "",
            cold > .55 ? text("寒冷", "Cold") : ""].filter(Boolean);
        element("vitals").textContent = text(
            `生命 ${Math.ceil(health)} · 饱食 ${Math.ceil(hunger)} · 水分 ${Math.ceil(hydration)}`,
            `HP ${Math.ceil(health)} · Food ${Math.ceil(hunger)} · Water ${Math.ceil(hydration)}`) +
            (conditions.length ? " · " + conditions.join(" · ") : "");
        nearby = frame.alive ? client.read("nearby") : null;
        const verb = hotkey("interact");
        const hint = frame.extractSeconds > 0 ? text(`撤离中 · ${frame.extractSeconds.toFixed(1)} 秒`, `Extracting · ${frame.extractSeconds.toFixed(1)} s`)
            : frame.nearExtraction && world.locked ? text(`${hotkey("extract")} 开始撤离`, `${hotkey("extract")} Extract`)
            : nearby && world.locked ? text(`${verb} 拾取`, `${verb} Take`) + " · " + nearby.contents.slice(0, 3).map(item => itemName(item.name)).join(text("、", ", "))
            : frame.nearWell && world.locked ? text(`${verb} 喝井水`, `${verb} Drink from well`) : "";
        fieldHint.hidden = !hint || !frame.alive;
        fieldHint.textContent = hint;
        if (!pack.hidden) inventoryPanel.refresh(client.read("cargo"), nearby);
        if (!mapPanel.hidden) drawFieldMap(mapCanvas, loaded, frame, mapImage);
        if (timestamp >= groundAt) {
            world.groundItems(client.read("ground"));
            groundAt = timestamp + 220;
        }
        audio.update(frame);
        updateField();
        const clock = lightPreview ?? frame.clock;
        const minutes = Math.floor(clock.minutes % 1440);
        const time = [Math.floor(minutes / 60), minutes % 60].map(value => String(value).padStart(2, "0")).join(":");
        element("world-clock").textContent = `${time} · ${text(weather[clock.weather][0], weather[clock.weather][1])}${lightPreview ? text(" · 光照对照", " · Lighting preview") : ""}`;
        const peers = frame.remotes.map(remote => `#${remote.id}`).join("、");
        element("identity").textContent = text(`幸存者 #${frame.id}${peers ? ` · 同服 ${peers}` : ""}`, `Survivor #${frame.id}${peers ? ` · Online ${peers}` : ""}`);
        element("sync").textContent = text(`快照 ${frame.snapshots.toLocaleString()} · 物资 ${frame.lootReady ? frame.lootCount : "同步中"}`, `Snapshots ${frame.snapshots.toLocaleString()} · Loot ${frame.lootReady ? frame.lootCount : "syncing"}`);
    } else if (viewShown) {
        world?.leave();
        audio.stop();
        showMap(false);
        showPack(false);
        viewShown = false;
    }
    if (phase === "profile" && profile.playing) {
        const refreshedProfile = client.read("profile");
        if (refreshedProfile.playing !== profile.playing) showProfile(refreshedProfile);
        else if (timestamp >= profileRefreshAt) {
            profileRefreshAt = timestamp + 2000;
            command({ type: "profile" });
        }
    }
    state(frame.message);
}

async function init() {
    account.value = localStorage.getItem("nextdayz.survivor") ?? "";
    if (!globalThis.isSecureContext) {
        throw Error(text("请通过 HTTPS 打开此游戏。", "Open this game over HTTPS."));
    }
    if (packaged) {
        const response = await fetch("/api/web/manifest", { cache: "no-store" });
        if (!response.ok) throw Error(text("无法读取服务器版本。", "Cannot read the server version."));
        const release = await response.json();
        if (release.build !== pageBuild) throw Error("Web client version changed. Refresh the page before deploying.");
        passwordRequired = release.passwordRequired;
        server.hidden = element("server-label").hidden = true;
    }
    const coreUrl = "/ndcore/ndcore.mjs";
    const [config, core] = await Promise.all([manifest(), import(/* @vite-ignore */ coreUrl)]);
    assets = config;
    const shared = assets.shared?.files ?? {};
    audio = new FieldAudio(shared);
    const locale = shared["i18n/zh-CN.json"];
    [app, actorAssets, translations] = await Promise.all([
        core.default(), loadActors(assets), locale ? new Assets().json<Record<string, string>>(locale) : {},
    ]);
    inventoryPanel = new Pack(items, shared, command, itemName);
    localize();
    session();
    if (client.protocol !== assets.protocol) throw Error("Web 地图和内核的协议版本不一致，请重新烘焙");
    loginButton.disabled = false;

    let hiddenAt: number | undefined;
    const hiddenLimit = 120_000;
    const background = new Worker(new URL("./background.ts", import.meta.url), { type: "module" });
    const checkBackground = () => {
        if (hiddenAt === undefined) return;
        if (Date.now() - hiddenAt >= hiddenLimit) command({ type: "leave" });
        else client.tick(.15);
    };
    background.onmessage = checkBackground;
    const visibilityChanged = () => {
        if (document.hidden) {
            hiddenAt ??= Date.now();
            world?.release();
            client.controls(0, 0, 0);
            background.postMessage(true);
        } else {
            checkBackground();
            hiddenAt = undefined;
            latestFrame = 0;
            background.postMessage(false);
        }
    };
    document.addEventListener("visibilitychange", visibilityChanged);
    visibilityChanged();

    const animate = (timestamp: number) => {
        if (document.hidden) {
            requestAnimationFrame(animate);
            return;
        }
        const dt = latestFrame ? Math.min((timestamp - latestFrame) / 1000, .15) : 0;
        latestFrame = timestamp;
        if (viewShown && world) world.sendControls((flags, dx, dy) => client.controls(flags, dx, dy));
        client.tick(dt);
        if (timestamp >= refreshAt) {
            refresh(timestamp);
            refreshAt = Math.max(refreshAt, timestamp + 105);
        }
        if (viewShown && world) world.render(client.read(), client.poseMatrices(), lightPreview);
        requestAnimationFrame(animate);
    };
    if (import.meta.env.DEV) {
        if (reviewEnabled) Object.assign(window, {
            nextdayzReviewPlace: place,
            nextdayzReviewCrowd: (count: number) => command({ type: "reviewCrowd", count }),
        });
        Object.assign(window, {
            nextdayzReview: () => ({
                frame: client.read(), profile: client.read("profile"), cargo: client.read("cargo"),
                cacheHits: loaded?.cacheHits ?? 0, drawCalls: world?.drawCalls ?? 0, triangles: world?.triangles ?? 0,
                scad: loaded?.scad,
            }),
            nextdayzLightPreview: (minutes?: number, mode = 0) => {
                lightPreview = minutes === undefined ? undefined : client.previewClock(minutes, mode);
            },
        });
    }
    requestAnimationFrame(animate);
}
void init().catch(error => state(error.message));
