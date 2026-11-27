import { Client, loopbackMaster } from "../shared/client.mjs";
import { Assets, assetURL, loadLevel, manifest, type Level } from "./assets";
import { World, type Quality } from "./world";
import { installCollision } from "../shared/collision.mjs";
import { Actors, loadActors, type ActorAssets } from "./actors";
import { FieldAudio } from "./audio";
import { compass, drawFieldMap } from "./fieldMap";
import { LobbyScene } from "./lobbyScene";
import { Pack } from "./pack";
import { renderOptions } from "./optionsPanel";
import { keyName, matches, saveSettings, settings, tr, useTranslations, type Binding } from "./settings";
import { WorldAtlas, mapLocationMeta } from "./worldAtlas";
import type { Clock, Economy, EconomyContract, Frame, Loot, Manifest, Profile, XYZ } from "./types";

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
const reviewBar = element("review-bar");
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
const atlasCanvas = element<HTMLCanvasElement>("atlas-canvas");
const diagnosticsBar = element("diagnostics-bar");
const settingsDialog = element<HTMLDialogElement>("settings-dialog");
const deathPanel = element("death-panel");
const hitMarker = element("hit-marker");
const reviewEnabled = import.meta.env.DEV && import.meta.env.VITE_ND_REVIEW === "1";
const trainingScad = import.meta.env.DEV && import.meta.env.VITE_ND_SCAD_TRAINING === "1";
const deployButton = element<HTMLButtonElement>("deploy");
const opsDeployButton = element<HTMLButtonElement>("ops-deploy");
const loadoutDeployButton = element<HTMLButtonElement>("loadout-deploy");

let client: Client;
let frame: Frame;
let assets: Manifest;
let actorAssets: ActorAssets;
let lobbyScene: LobbyScene | undefined;
let worldAtlas: WorldAtlas | undefined;
let loaded: Level | undefined;
let scadFallback: string | undefined;
let world: World | undefined;
let app: unknown;
let phase = "";
let profile: Profile;
let activeChannel: "cabin" | "ops" | "loadout" | "trade" = "cabin";
let activeTradeTab: "contracts" | "trader" = "contracts";
let stashFilter = "all";
let refreshAt = 0;
let profileRefreshAt = 0;
let latestFrame = 0;
let mapLoading = false;
let viewShown = false;
let lightPreview: Clock | undefined;
let nearby: Loot | null = null;
let inventoryPanel: Pack;
let audio: FieldAudio;
let mapImage: HTMLImageElement | undefined;
let groundAt = 0;

const hotkey = (binding: Binding) => keyName(settings.keys[binding]);
const itemName = (source: string) => tr(source);
const mapTitle = (map: { name: string; localName?: string }) => settings.language === "en" ? map.name : map.localName || map.name;
const bring: HTMLInputElement[] = [];

function classifyItem(id: string): "weapon" | "ammo" | "gear" | "supply" {
    if (id.startsWith("ammo_")) return "ammo";
    if (/(^|_)(ak|svd|mosin|shotgun|makarov|knife|crowbar|axe|bizon|stechkin|svt|toz|flare|optic|scope|grip|mag|suppressor|sight|flashlight)/.test(id))
        return "weapon";
    if (/(helmet|hat|cap|beanie|jacket|hoodie|vest|armor|rig|pants|jeans|trousers|pack|bag|rucksack|anorak|shell|telnyashka)/.test(id))
        return "gear";
    return "supply";
}

function setChannel(next: "cabin" | "ops" | "loadout" | "trade") {
    activeChannel = next;
    profilePanel.dataset.tab = next;
    for (const tab of profilePanel.querySelectorAll<HTMLButtonElement>("[data-nav]"))
        tab.classList.toggle("active", tab.dataset.nav === next);
    for (const panel of profilePanel.querySelectorAll<HTMLElement>("[data-panel]"))
        panel.hidden = panel.dataset.panel !== next;
    if (next === "ops") worldAtlas?.draw();
}

function setTradeTab(next: "contracts" | "trader") {
    activeTradeTab = next;
    for (const btn of profilePanel.querySelectorAll<HTMLButtonElement>("[data-trade-tab]"))
        btn.classList.toggle("active", btn.dataset.tradeTab === next);
    for (const view of profilePanel.querySelectorAll<HTMLElement>("[data-trade-view]"))
        view.hidden = view.dataset.tradeView !== next;
}

function state(message: string) {
    const translated = itemName(message);
    notice.textContent = translated;
    const local = element("pack-notice");
    local.hidden = !translated;
    local.textContent = translated;
}

function syncCarryPreview() {
    const preview = element("carry-preview");
    preview.replaceChildren();
    const carriedIds: string[] = [];
    let total = 0;

    if (profile?.character) {
        for (const item of profile.character.storage ?? []) carriedIds.push(item.id);
    }
    for (let i = 0; i < bring.length; ++i) {
        const input = bring[i];
        if (input.disabled) continue;
        const count = Math.max(0, Math.min(Number(input.max), Math.floor(Number(input.value) || 0)));
        input.value = String(count);
        total += count;
        if (count > 0 && profile?.storage[i]) {
            const item = profile.storage[i];
            carriedIds.push(item.id);
            const row = document.createElement("div");
            row.className = "carry-row";
            const name = document.createElement("span");
            name.textContent = itemName(item.name);
            const qty = document.createElement("b");
            qty.textContent = `×${count}`;
            row.append(name, qty);
            preview.append(row);
        }
    }
    if (!preview.childElementCount) {
        const empty = document.createElement("p");
        empty.className = "muted";
        empty.textContent = tr("Nothing selected. Pick items from storage or deploy empty-handed.");
        preview.append(empty);
    }
    const label = total ? tr("Bringing {} items", total) : tr("Empty-handed sortie");
    element("bring-count").textContent = total ? tr("Bringing {} items", total) : "";
    element("bring-summary").textContent = label;
    element("carry-pane-count").textContent = label;
    lobbyScene?.setLoadout(carriedIds);
}

function updateBring() {
    syncCarryPreview();
}

function syncSelectedMapDossier() {
    if (!profile) return;
    const chosen = maps.querySelector<HTMLInputElement>('input[name="map"]:checked')?.value ?? "";
    const selectedMap = profile.maps.find(m => m.id === chosen) ?? profile.maps[0];
    const opsList = element("ops-map-list");
    for (const card of opsList.querySelectorAll<HTMLButtonElement>("[data-map-id]"))
        card.classList.toggle("active", card.dataset.mapId === chosen);

    if (selectedMap) {
        const entry = assets.maps[selectedMap.id];
        const meta = mapLocationMeta(selectedMap.id);
        element("dossier-region").textContent = meta ? meta.region : selectedMap.training ? tr("Training") : tr("World Atlas");
        element("dossier-name").textContent = entry ? mapTitle(entry.map) : selectedMap.name;
        element("dossier-meta").textContent = meta
            ? `${meta.lat.toFixed(3)}°, ${meta.lon.toFixed(3)}° · 1000 × 1000 m`
            : tr("Training");
        element("dossier-desc").textContent = entry
            ? (selectedMap.training ? tr("Training is practice only. Your survivor is back in the safehouse with the gear they carried.")
                : tr("Search. Survive. Bring it home."))
            : tr("Web assets pending");
    }

    const atlasLocs = profile.maps.map(m => {
        const entry = assets.maps[m.id];
        const meta = mapLocationMeta(m.id);
        return {
            id: m.id,
            name: entry ? mapTitle(entry.map) : m.name,
            region: meta?.region ?? "",
            lat: meta?.lat ?? 0,
            lon: meta?.lon ?? 0,
            training: m.training || !meta,
            available: Boolean(entry && entry.map.scene === m.scene && (!profile.character || profile.character.map === m.id)),
        };
    });
    worldAtlas?.update(atlasLocs, chosen);
}

function selectMapById(mapId: string) {
    const radio = maps.querySelector<HTMLInputElement>(`input[name="map"][value="${CSS.escape(mapId)}"]`);
    if (radio && !radio.disabled) {
        radio.checked = true;
        syncSelectedMapDossier();
    }
}

function triggerDeploy() {
    const chosen = maps.querySelector<HTMLInputElement>('input[name="map"]:checked');
    if (!chosen) return;
    updateBring();
    command({
        type: "deploy",
        map: chosen.value,
        bring: bring.flatMap((input, index) => !input.disabled && Number(input.value) > 0 ? [{ index, count: Number(input.value) }] : []),
    });
}

let economyPending = false;
function economy(op: string, fields: Record<string, unknown> = {}) {
    economyPending = true;
    command({ type: "economy", op, ...fields });
}

function button(label: string, disabled: boolean, onClick: () => void, primary = false) {
    const node = document.createElement("button");
    node.type = "button";
    node.className = primary ? "primary-cta" : "quiet";
    node.textContent = label;
    node.disabled = disabled;
    node.addEventListener("click", onClick);
    return node;
}

function issuerName(issuer: string) {
    return tr(issuer === "medical" ? "MEDICAL STATION" : issuer === "quartermaster" ? "QUARTERMASTER"
        : issuer === "collector" ? "COLLECTOR" : "RADIO STATION");
}

function showEconomy(data: Economy) {
    const section = element("economy");
    section.hidden = !data.available;
    const tierChip = element("profile-tier");
    tierChip.hidden = !data.available;
    if (data.available) tierChip.textContent = `T${data.tier}`;

    const contractsPill = element("cabin-contracts-pill");
    contractsPill.hidden = !data.available;
    if (data.available) {
        element("cabin-contracts-text").textContent = data.contracts.length
            ? tr("Active contracts: {}", data.contracts.length)
            : tr("CONTRACT BOARD  ({} / {})", data.board.length, data.slots);
    }

    if (!data.available) return;
    const inField = Boolean(profile.character);
    const status = element("economy-status");
    status.classList.toggle("over", data.over);
    status.replaceChildren(`${tr("Gold: {}", profile.gold)} · ${tr("Reputation: {}  (tier {})", data.reputation, data.tier)} · `
        + tr("WAREHOUSE  {} / {} stacks", data.used, data.capacity));
    if (data.nextCapacity)
        status.append(" ", button(tr("Expand to {} stacks ({} gold)", data.nextCapacity, data.nextPrice),
            profile.gold < data.nextPrice, () => economy("upgrade")));
    if (data.lastCompleted) {
        const report = document.createElement("small");
        report.textContent = tr("Last report: contracts completed: {} (+{} gold, reputation {:+}).", data.lastCompleted, data.lastGold)
            .replace("{:+}", `${data.lastReputation >= 0 ? "+" : ""}${data.lastReputation}`);
        status.append(report);
    }
    element("stash-capacity").textContent = tr("WAREHOUSE  {} / {} stacks", data.used, data.capacity);

    const card = (contract: EconomyContract, accepted: boolean) => {
        const entry = document.createElement("div");
        entry.className = "contract" + ((accepted || contract.canHandIn) ? " ready" : "");
        const issuer = document.createElement("small");
        issuer.textContent = `${issuerName(contract.issuer)} · ${tr("Tier {}", contract.tier)}`;
        const title = document.createElement("strong");
        title.textContent = tr(contract.title);
        entry.append(issuer, title);
        for (const item of contract.items) {
            const line = document.createElement("small");
            line.textContent = tr("Deliver {} x{}", itemName(item.name), item.count);
            entry.append(line);
        }
        if (contract.targets.length) {
            const line = document.createElement("small");
            line.textContent = tr("Visit {} ({}), then extract.", contract.targets.join(", "), contract.map);
            entry.append(line);
        }
        const reward = document.createElement("small");
        reward.textContent = tr("Reward: {} gold, +{} reputation", contract.reward, contract.reputation);
        const left = document.createElement("small");
        left.textContent = accepted ? tr("Expires after {} more expeditions", contract.sortiesLeft)
            : tr("Leaves the board after {} more expeditions", contract.sortiesLeft);
        entry.append(reward, left);
        if (accepted) entry.append(button(tr("Abandon"), inField, () => economy("abandon", { contractId: contract.id })));
        else if (contract.type === "deliver")
            entry.append(button(tr("Hand in"), !contract.canHandIn, () => economy("handin", { contractId: contract.id }), contract.canHandIn));
        else if (!contract.online) {
            const note = document.createElement("small");
            note.textContent = tr("Offline campaign only for now.");
            entry.append(note);
        } else entry.append(button(tr("Accept"), inField || data.contracts.length >= data.maxActive,
            () => economy("accept", { contractId: contract.id })));
        return entry;
    };
    element("board-title").textContent = tr("CONTRACT BOARD  ({} / {})", data.board.length, data.slots);
    element("contract-board").replaceChildren(...data.board.map(contract => card(contract, false)));
    element("active-title").textContent = tr("ACTIVE CONTRACTS  ({} / {})", data.contracts.length, data.maxActive);
    element("contract-active").replaceChildren(...data.contracts.map(contract => card(contract, true)));
    element("trader").replaceChildren(...data.trader.map(shelf => {
        const row = document.createElement("div");
        row.className = shelf.locked ? "shelf locked" : "shelf";
        const top = document.createElement("div");
        const title = document.createElement("strong");
        title.textContent = `${itemName(shelf.name)} ×${shelf.pack}`;
        const detail = document.createElement("small");
        detail.textContent = shelf.locked ? tr("Tier {}", shelf.tier) : `${tr("Left")} ${shelf.left}`;
        top.append(title, detail);

        const foot = document.createElement("div");
        foot.className = "shelf-foot";
        const price = document.createElement("span");
        price.className = "shelf-price";
        price.textContent = String(shelf.price);
        foot.append(price, shelf.locked ? document.createTextNode("")
            : button(tr("Buy"), shelf.left <= 0 || data.over || profile.gold < shelf.price, () => economy("buy", { item: shelf.item })));
        row.append(top, foot);
        return row;
    }));
}

function renderStashFilters() {
    const bar = element("stash-filters");
    const defs: [string, string][] = [
        ["all", "All"], ["weapon", "Weapons"], ["ammo", "Ammo"], ["gear", "Gear"], ["supply", "Supplies"],
    ];
    bar.replaceChildren(...defs.map(([id, label]) => {
        const pill = document.createElement("button");
        pill.type = "button";
        pill.className = "filter-pill" + (stashFilter === id ? " active" : "");
        pill.textContent = tr(label);
        pill.addEventListener("click", () => {
            stashFilter = id;
            renderStashFilters();
            for (const row of storage.querySelectorAll<HTMLElement>(".row"))
                row.hidden = stashFilter !== "all" && row.dataset.kind !== stashFilter;
        });
        return pill;
    }));
}

function showProfile(data: Profile) {
    profile = data;
    element("profile-name").textContent = profile.name;
    element("profile-gold").textContent = tr("Gold: {}", profile.gold);
    element("profile-activity").textContent = tr("Sorties {} · Extractions {} · Deaths {}", profile.sorties, profile.extractions, profile.deaths);
    storage.replaceChildren();
    bring.length = 0;
    renderStashFilters();
    const catalog = assets.shared?.files ?? {};

    if (!profile.storage.length) {
        const empty = document.createElement("p");
        empty.className = "muted";
        empty.textContent = tr("Storage is empty");
        storage.append(empty);
    }

    profile.storage.forEach((item, index) => {
        const kind = classifyItem(item.id);
        const row = document.createElement("div");
        row.className = "row";
        row.dataset.kind = kind;
        row.hidden = stashFilter !== "all" && kind !== stashFilter;

        const icon = document.createElement("div");
        icon.className = "icon-box";
        const file = catalog[`icons/${item.id}.webp`];
        if (file) {
            const image = document.createElement("img");
            image.src = assetURL(file.path);
            image.alt = "";
            image.loading = "lazy";
            icon.append(image);
        }

        const label = document.createElement("div");
        label.className = "item-meta";
        const name = document.createElement("span");
        name.textContent = itemName(item.name);
        const count = document.createElement("small");
        count.textContent = tr("Stored {}", item.count);
        label.append(name, count);
        if (item.description) label.title = itemName(item.description);

        const controlsRow = document.createElement("div");
        controlsRow.className = "row-controls";

        const value = profile.economy.available ? profile.economy.sellValues[index] ?? 0 : 0;
        if (value) {
            controlsRow.append(button(tr("Sell 1 (+{} gold)", value), false,
                () => economy("sell", { index, item: item.id, count: 1 })));
        } else {
            controlsRow.append(document.createElement("span"));
        }

        const stepper = document.createElement("div");
        stepper.className = "stepper";
        const input = document.createElement("input");
        input.type = "number";
        input.min = "0";
        input.max = String(item.count);
        input.value = "0";
        input.disabled = Boolean(profile.character);
        input.setAttribute("aria-label", tr("Quantity to bring for {}", itemName(item.name)));
        input.addEventListener("change", updateBring);

        const dec = button("-", input.disabled, () => {
            input.value = String(Math.max(0, (Number(input.value) || 0) - 1));
            updateBring();
        });
        const inc = button("+", input.disabled, () => {
            input.value = String(Math.min(item.count, (Number(input.value) || 0) + 1));
            updateBring();
        });
        stepper.append(dec, input, inc);
        controlsRow.append(stepper);
        bring.push(input);
        row.append(icon, label, controlsRow);
        storage.append(row);
    });

    maps.replaceChildren();
    const opsList = element("ops-map-list");
    opsList.replaceChildren();
    let selected = false;
    for (const map of profile.maps) {
        const entry = assets.maps[map.id];
        const disabled = !entry || entry.map.scene !== map.scene || (!!profile.character && profile.character.map !== map.id);
        const isChecked: boolean = !disabled && !selected;
        selected = selected || isChecked;

        const label = document.createElement("label");
        const radio = document.createElement("input");
        radio.type = "radio";
        radio.name = "map";
        radio.value = map.id;
        radio.disabled = disabled;
        radio.checked = isChecked;
        radio.addEventListener("change", syncSelectedMapDossier);
        const title = document.createElement("span");
        title.textContent = entry ? mapTitle(entry.map) : map.name;
        const subtitle = document.createElement("small");
        const meta = mapLocationMeta(map.id);
        subtitle.textContent = entry ? (meta ? meta.region : tr("Training")) : tr("Web assets pending");
        title.append(subtitle);
        label.append(radio, title);
        maps.append(label);

        const card = document.createElement("button");
        card.type = "button";
        card.className = "ops-map-card" + (isChecked ? " active" : "");
        card.dataset.mapId = map.id;
        card.disabled = disabled;
        const cTitle = document.createElement("strong");
        cTitle.textContent = entry ? mapTitle(entry.map) : map.name;
        const cSub = document.createElement("small");
        cSub.textContent = entry ? (meta ? meta.region : tr("Training")) : tr("Web assets pending");
        card.append(cTitle, cSub);
        card.addEventListener("click", () => selectMapById(map.id));
        opsList.append(card);
    }

    const canDeploy = selected && !profile.playing;
    const deployLabel = profile.character ? tr("Resume expedition") : tr("Deploy");
    for (const btn of [deployButton, opsDeployButton, loadoutDeployButton]) {
        btn.disabled = !canDeploy;
        btn.textContent = deployLabel;
    }
    element("carry-hint").textContent = profile.playing
        ? tr("The map server is saving your survivor")
        : profile.character ? tr("Resume with your current equipment")
            : tr("Choose what to bring from storage");
    updateBring();
    showEconomy(profile.economy);
    syncSelectedMapDossier();
}

async function prepare(scene: string) {
    const bundle = Object.values(assets.maps).find(entry => entry.map.scene === scene);
    if (!bundle) throw Error(tr("Web assets for this map are missing"));
    mapLoading = true;
    const title = mapTitle(bundle.map);
    element("loading-title").textContent = title;
    element("loading-detail").textContent = tr("Reading the map manifest");
    progress.removeAttribute("value");
    try {
        let generated: Level | undefined;
        scadFallback = undefined;
        if (trainingScad && bundle.map.id === "training_ground") {
            element("loading-detail").textContent = tr("Reading SCAD sources");
            try {
                const { loadScadLevel } = await import("./scadLevel");
                generated = await loadScadLevel(scene, () => {
                    element("loading-detail").textContent = tr("Generating terrain, materials and collision");
                });
            } catch (error) {
                scadFallback = error instanceof Error ? error.message : String(error);
                console.warn(`SCAD evaluation failed, loading the baked map: ${scadFallback}`);
                element("loading-detail").textContent = tr("SCAD evaluation failed. Loading the baked map");
            }
        }
        loaded = generated ?? await loadLevel(bundle, (done, total) => {
            progress.max = total;
            progress.value = done;
            element("loading-detail").textContent = tr("Downloading and checking assets · {} / {}", done, total);
        });
        (world ??= new World(element("viewport"), actorAssets, locked => {
            menu.hidden = locked;
            aimPoint.hidden = !locked;
            if (locked) {
                pack.hidden = true;
                mapPanel.hidden = true;
                void audio?.unlock().catch(() => {});
            }
        })).setLevel(loaded);
        world.setQuality(settings.quality);
        mapImage = undefined;
        element("loading-detail").textContent = tr("Map ready. Joining the server");
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
    if (new TextEncoder().encode(account.value.trim()).length > 32) {
        state(tr("Enter a survivor name of 1-32 bytes."));
        return;
    }
    localStorage.setItem("nextdayz.survivor", account.value.trim());
    command({ type: server.value === "direct" ? "direct" : "login", name: account.value.trim(), password: password.value });
    password.value = "";
});

server.addEventListener("change", () => {
    const showPassword = server.value === "master";
    password.hidden = element("password-label").hidden = !showPassword;
    password.required = showPassword && passwordRequired;
    loginButton.textContent = server.value === "direct" ? tr("Join map server") : tr("Enter safehouse");
});

element("logout").addEventListener("click", () => {
    state("");
    setChannel("cabin");
    session();
});

deployButton.addEventListener("click", triggerDeploy);
opsDeployButton.addEventListener("click", triggerDeploy);
loadoutDeployButton.addEventListener("click", triggerDeploy);

element("clear-bring").addEventListener("click", () => {
    for (const input of bring) if (!input.disabled) input.value = "0";
    updateBring();
});
element("bring-all").addEventListener("click", () => {
    for (const input of bring) if (!input.disabled) input.value = input.max;
    updateBring();
});

for (const navBtn of profilePanel.querySelectorAll<HTMLButtonElement>("[data-nav]")) {
    navBtn.addEventListener("click", () => {
        const target = navBtn.dataset.nav as "cabin" | "ops" | "loadout" | "trade";
        setChannel(target === activeChannel && target !== "cabin" ? "cabin" : target);
    });
}
for (const jumpBtn of profilePanel.querySelectorAll<HTMLButtonElement>("[data-jump]")) {
    jumpBtn.addEventListener("click", () => setChannel(jumpBtn.dataset.jump as "cabin" | "ops" | "loadout" | "trade"));
}
for (const tradeBtn of profilePanel.querySelectorAll<HTMLButtonElement>("[data-trade-tab]")) {
    tradeBtn.addEventListener("click", () => setTradeTab(tradeBtn.dataset.tradeTab as "contracts" | "trader"));
}

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

function openSettings() {
    world?.release();
    settingsDialog.showModal();
    renderOptions(element("settings-content"), applySettings);
}
element("settings-button").addEventListener("click", openSettings);
element("entry-settings-button").addEventListener("click", openSettings);
element("pause-settings-button").addEventListener("click", openSettings);

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
    const netText = phase === "playing" ? tr("Connected · WebRTC")
        : phase === "profile" ? tr("Signed into Master") : tr("NextDayz online");
    element("network-label").textContent = netText;
    element("login-network").textContent = netText;
    document.title = tr("NextDayz · Multiplayer lobby");
    for (const node of document.querySelectorAll<HTMLElement>("[data-i18n]")) node.textContent = tr(node.dataset.i18n ?? "");
    for (const node of document.querySelectorAll<HTMLElement>("[data-i18n-label]"))
        node.setAttribute("aria-label", tr(node.dataset.i18nLabel ?? ""));
    server.dispatchEvent(new Event("change"));
    element<HTMLSelectElement>("quality").value = settings.quality;
    controls.textContent = tr("{}{}{}{} Move · LMB Fire · RMB Aim · {} Reload · {} Jump / vault · {} Inventory · {} Map · Esc Menu",
        hotkey("forward"), hotkey("left"), hotkey("back"), hotkey("right"), hotkey("reload"), hotkey("jump"),
        hotkey("inventory"), hotkey("map"));
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

element("pack-button").addEventListener("click", () => showPack(pack.hidden));
element("close-pack").addEventListener("click", () => showPack(false));
element("map-button").addEventListener("click", () => showMap(mapPanel.hidden));
element("close-map").addEventListener("click", () => showMap(false));
element("respawn").addEventListener("click", () => command({ type: "leave" }));

window.addEventListener("keydown", event => {
    if (event.repeat || settingsDialog.open || event.ctrlKey || event.metaKey) return;
    const typing = document.activeElement instanceof HTMLInputElement || document.activeElement instanceof HTMLSelectElement;
    if (!viewShown) {
        if (phase === "profile" && !typing) {
            if (event.code === "Escape") setChannel("cabin");
            else if (event.code === "KeyM") { event.preventDefault(); setChannel(activeChannel === "ops" ? "cabin" : "ops"); }
            else if (event.code === "Tab") { event.preventDefault(); setChannel(activeChannel === "loadout" ? "cabin" : "loadout"); }
            else if (event.code === "KeyT") { event.preventDefault(); setChannel(activeChannel === "trade" ? "cabin" : "trade"); }
        }
        return;
    }
    if (event.code === "F3") {
        event.preventDefault();
        diagnosticsBar.hidden = !diagnosticsBar.hidden;
    } else if (event.code === "Escape" && !world?.locked) {
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

const place = (position: XYZ, yaw: number, pitch = 0) => command({ type: "reviewPlace", position, yaw, pitch });
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
    element("weapon-name").textContent = itemName(combat.weapon) || tr("Unarmed");
    element("ammo").textContent = combat.capacity ? `${combat.magazine} / ${combat.reserve}` : "";
    const action = ["", "Reloading", "Switching", "Melee"][combat.action];
    element("weapon-action").textContent = action ? `${tr(action)} ${Math.round(combat.progress * 100)}%` : "";

    const activeSlot = frame.pose?.activeSlot ?? 0;
    for (let s = 0; s < 3; ++s) {
        element(`slot-dot-${s}`).classList.toggle("active", Boolean(combat.weapon) && activeSlot === s);
    }

    // Frameless visual vitals bars + conditional pills
    const hp = Math.max(0, Math.min(100, Math.ceil(body.health)));
    const food = Math.max(0, Math.min(100, Math.ceil(body.hunger)));
    const water = Math.max(0, Math.min(100, Math.ceil(body.hydration)));
    element("vital-hp-bar").style.width = `${hp}%`;
    element("vital-hp-val").textContent = String(hp);
    element("vital-hp-row").classList.toggle("low", hp < 28);
    element("vital-food-bar").style.width = `${food}%`;
    element("vital-food-val").textContent = String(food);
    element("vital-food-row").classList.toggle("low", food < 22);
    element("vital-water-bar").style.width = `${water}%`;
    element("vital-water-val").textContent = String(water);
    element("vital-water-row").classList.toggle("low", water < 22);

    const conds = element("conditions-hud");
    const pills: HTMLElement[] = [];
    if (body.bleeding) {
        const pill = document.createElement("span");
        pill.className = "cond-pill";
        pill.textContent = tr("Bleeding {}", body.bleeding);
        pills.push(pill);
    }
    if (body.cold > .55) {
        const pill = document.createElement("span");
        pill.className = "cond-pill cold";
        pill.textContent = tr("Cold");
        pills.push(pill);
    }
    if (body.healing && !body.bleeding) {
        const pill = document.createElement("span");
        pill.className = "cond-pill heal";
        pill.textContent = tr("HEALING");
        pills.push(pill);
    }
    conds.replaceChildren(...pills);

    hitMarker.hidden = !combat.hitMarker;
    hitMarker.classList.toggle("head", combat.headshot);
    hitMarker.classList.toggle("kill", combat.kill);
    element("damage-hud").style.opacity = String(combat.hurt * .9);

    const pvpEl = element("pvp-status");
    pvpEl.hidden = !frame.pvp;
    pvpEl.textContent = tr("Friendly fire");

    deathPanel.hidden = frame.alive;
    if (!frame.alive) {
        world?.release();
        showPack(false);
        showMap(false);
        element("death-detail").textContent = itemName(body.cause) || tr("Return to the lobby to redeploy.");
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
        lobbyScene?.setActive(!playing);
        if (phase === "profile") {
            setChannel("cabin");
            showProfile(client.read("profile"));
        }
        if (phase === "waiting" || phase === "deploying") {
            element("loading-title").textContent = phase === "deploying" ? tr("Deploying") : profile ? tr("Contacting Master") : tr("Signing in");
            element("loading-detail").textContent = phase === "deploying" ? tr("Waiting for map server") : tr("Syncing survivor profile");
            progress.removeAttribute("value");
        }
        if (phase === "joining" && !mapLoading) {
            element("loading-title").textContent = tr("Joining map server");
            element("loading-detail").textContent = tr("Connecting WebRTC");
        }
        const netText = playing ? tr("Connected · WebRTC")
            : phase === "profile" ? tr("Signed into Master") : tr("NextDayz online");
        element("network-label").textContent = netText;
        element("login-network").textContent = netText;
    }
    if (playing && world && loaded) {
        if (!viewShown) {
            world.enter(frame);
            groundAt = 0;
            viewShown = true;
            reviewBar.hidden = !import.meta.env.DEV;
        }
        const bundle = Object.values(assets.maps).find(entry => entry.map.scene === frame.scene);
        const title = bundle ? mapTitle(bundle.map) : frame.scene;
        element("map-name").textContent = bundle ? `${title} · v${bundle.map.mapVersion}` : frame.scene;
        element("pause-map-title").textContent = title;
        course.hidden = range.hidden = pen.hidden = !reviewEnabled || bundle?.map.id !== "training_ground" || !frame.alive;

        const { health, hunger, hydration, bleeding, cold } = frame.body;
        const conditions = [bleeding ? tr("Bleeding {}", bleeding) : "",
            cold > .55 ? tr("Cold") : ""].filter(Boolean);
        element("vitals").textContent = tr("HP {} · Food {} · Water {}", Math.ceil(health), Math.ceil(hunger), Math.ceil(hydration)) +
            (conditions.length ? " · " + conditions.join(" · ") : "");

        nearby = frame.alive ? client.read("nearby") : null;
        const verb = hotkey("interact");
        const hint = frame.extractSeconds > 0 ? tr("Extracting · {} s", frame.extractSeconds.toFixed(1))
            : frame.nearExtraction && world.locked ? tr("{} Extract", hotkey("extract"))
            : nearby && world.locked ? tr("{} Take", verb) + " · " + nearby.contents.slice(0, 3).map(item => itemName(item.name)).join(tr(", "))
            : frame.nearWell && world.locked ? tr("{} Drink from well", verb) : "";
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
        element("world-clock").textContent = `${time} · ${tr(clock.weather)}${lightPreview ? tr(" · Lighting preview") : ""}`;
        const peers = frame.remotes.map(remote => `#${remote.id}`).join(tr(", "));
        element("identity").textContent = tr("Survivor #{}", frame.id) + (peers ? tr(" · Online {}", peers) : "");
        element("sync").textContent = tr("Snapshots {} · Loot {}", frame.snapshots.toLocaleString(), frame.lootReady ? frame.lootCount : tr("syncing"));
    } else if (viewShown) {
        world?.leave();
        audio.stop();
        showMap(false);
        showPack(false);
        viewShown = false;
    }
    if (phase === "profile" && economyPending) {
        const refreshed = client.read("profile");
        if (refreshed.revision !== profile.revision) {
            economyPending = false;
            showProfile(refreshed);
        }
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
        throw Error(tr("Open this game over HTTPS."));
    }
    if (packaged) {
        const response = await fetch("/api/web/manifest", { cache: "no-store" });
        if (!response.ok) throw Error(tr("Cannot read the server version."));
        const release = await response.json();
        if (release.build !== pageBuild) throw Error(tr("Web client version changed. Refresh the page before deploying."));
        passwordRequired = release.passwordRequired;
        server.hidden = element("server-label").hidden = true;
    }
    const coreUrl = reviewEnabled ? "/ndcore/ndcore-review.mjs" : "/ndcore/ndcore.mjs";
    const [config, core] = await Promise.all([manifest(), import(/* @vite-ignore */ coreUrl)]);
    assets = config;
    const shared = assets.shared?.files ?? {};
    audio = new FieldAudio(shared);
    const locale = shared["i18n/zh-CN.json"];
    let table: Record<string, string>;
    [app, actorAssets, table] = await Promise.all([
        core.default(), loadActors(assets), locale ? new Assets().json<Record<string, string>>(locale) : {},
    ]);
    useTranslations(table);
    inventoryPanel = new Pack(items, shared, command, itemName);
    worldAtlas = new WorldAtlas(atlasCanvas, selectMapById);
    try {
        lobbyScene = new LobbyScene(element("lobby-stage"), actorAssets);
        lobbyScene.setActive(true);
    } catch {
        // Fallback smoothly to CSS ambient cabin gradient if WebGL context is unavailable.
    }
    localize();
    session();
    if (client.protocol !== assets.protocol) throw Error(tr("The Web maps and the kernel use different protocol versions. Bake again."));
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

    const work: number[] = [];
    const animate = (timestamp: number) => {
        if (document.hidden) {
            requestAnimationFrame(animate);
            return;
        }
        const begin = performance.now();
        const dt = latestFrame ? Math.min((timestamp - latestFrame) / 1000, .15) : 0;
        latestFrame = timestamp;
        if (viewShown && world) world.sendControls((flags, dx, dy) => client.controls(flags, dx, dy));
        client.tick(dt);
        if (timestamp >= refreshAt) {
            refresh(timestamp);
            refreshAt = Math.max(refreshAt, timestamp + 105);
        }
        if (viewShown && world) world.render(client.read(), client.poseMatrices(), lightPreview);
        work.push(performance.now() - begin);
        if (work.length > 240) work.shift();
        requestAnimationFrame(animate);
    };
    if (import.meta.env.DEV) {
        if (reviewEnabled) Object.assign(window, {
            nextdayzReviewPlace: place,
            nextdayzReviewCrowd: (count: number) => command({ type: "reviewCrowd", count }),
            nextdayzReviewAnchors: () => loaded?.anchors,
        });
        Object.assign(window, {
            nextdayzReview: () => ({
                frame: client.read(), profile: client.read("profile"), cargo: client.read("cargo"),
                cacheHits: loaded?.cacheHits ?? 0, drawCalls: world?.drawCalls ?? 0, triangles: world?.triangles ?? 0,
                workMs: [...work], wasmBytes: (app as { HEAPU8?: Uint8Array })?.HEAPU8?.length ?? 0,
                scad: loaded?.scad, scadFallback,
            }),
            nextdayzLightPreview: (minutes?: number, mode = 0) => {
                lightPreview = minutes === undefined ? undefined : client.previewClock(minutes, mode);
            },
        });
    }
    requestAnimationFrame(animate);
}
void init().catch(error => state(error.message));
