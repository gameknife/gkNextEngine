import { assetURL } from "./assets";
import { text } from "./settings";
import type { AssetFile, Cargo, CargoBox, CargoItem, Item, Loot } from "./types";

type Command = (input: Record<string, unknown>) => void;
type Translate = (source: string) => string;
const wornContainer = 0xffffffff;
const attachNames = ["", "Red Dot Sight", "Holographic Sight", "4x Scope", "Vertical Foregrip",
    "Quickdraw Magazine", "Extended Magazine", "Suppressor", "Tactical Flashlight"];

function button(label: string, action?: () => void) {
    const node = document.createElement("button");
    node.type = "button";
    node.className = "quiet";
    node.textContent = label;
    if (action) node.addEventListener("click", action);
    return node;
}

export class Pack {
    private cargo?: Cargo;
    private ground?: Loot | null;
    private selected = 0;
    private dragging = 0;
    private groundDrag = "";
    private rotated = false;
    private current = "";
    private details = document.createElement("aside");

    constructor(private root: HTMLElement, private files: Record<string, AssetFile>,
                private send: Command, private name: Translate) {}

    refresh(cargo: Cargo, ground: Loot | null) {
        this.cargo = cargo;
        this.ground = ground;
        if (this.dragging || this.groundDrag) return;
        const key = JSON.stringify([text("zh", "en"), this.selected, this.rotated, cargo, ground]);
        if (key === this.current) return;
        this.current = key;
        this.draw();
    }

    invalidate() { this.current = ""; }

    rotate() {
        if (!this.selected) return;
        this.rotated = !this.rotated;
        this.invalidate();
        this.draw();
    }

    private show(item: CargoItem) {
        if (this.selected !== item.instance) this.rotated = item.rotated;
        this.selected = item.instance;
        this.invalidate();
        this.draw();
    }

    private change(edit: string, instance: number, extra: Record<string, unknown> = {}) {
        this.send({ type: "inventory", edit, instance, ...extra });
        this.invalidate();
    }

    private put(box: CargoBox, x: number, y: number) {
        if (this.groundDrag) {
            this.send({ type: "take", id: this.groundDrag, container: box.id, x, y, rotated: false });
            return;
        }
        const instance = this.dragging || this.selected;
        if (instance) this.change("place", instance, { container: box.id, x, y, rotated: this.rotated });
    }

    private image(item: Item) {
        const file = this.files[`icons/${item.id}.webp`];
        if (!file) return;
        const image = document.createElement("img");
        image.src = assetURL(file.path);
        image.alt = "";
        image.draggable = false;
        return image;
    }

    private tile(item: CargoItem, equipped = false) {
        const chosen = this.selected === item.instance;
        const node = button(this.name(item.name), () => this.show(item));
        node.classList.add(equipped ? "equip-item" : "pack-item");
        node.dataset.item = item.id;
        node.classList.toggle("selected", chosen);
        node.setAttribute("aria-label", this.name(item.name));
        node.setAttribute("aria-pressed", String(chosen));
        node.title = this.name(item.name);
        const img = this.image(item);
        if (img) {
            node.replaceChildren(img);
            const label = document.createElement("span");
            label.textContent = this.name(item.name);
            node.append(label);
        }
        if (item.count > 1) {
            const count = document.createElement("b");
            count.textContent = String(item.count);
            node.append(count);
        }
        node.draggable = true;
        node.addEventListener("dragstart", event => {
            if (this.selected !== item.instance) this.rotated = item.rotated;
            this.selected = this.dragging = item.instance;
            event.dataTransfer?.setData("text/plain", String(item.instance));
            if (event.dataTransfer) event.dataTransfer.effectAllowed = "move";
            this.root.classList.add("dragging");
            this.describe();
        });
        node.addEventListener("dragend", () => {
            this.dragging = 0;
            this.root.classList.remove("dragging");
            this.invalidate();
        });
        node.addEventListener("dragover", event => event.preventDefault());
        node.addEventListener("drop", event => {
            event.preventDefault();
            event.stopPropagation();
            const source = this.cargo?.items.find(row => row.instance === this.dragging);
            if (!source || source.instance === item.instance) return;
            if (source.attachable && item.wieldable) this.change("attach", source.instance, { target: item.instance });
            else if (source.id === item.id) this.change("merge", source.instance, { target: item.instance });
            else if (item.container !== wornContainer)
                this.change("place", source.instance, { container: item.container, x: item.x, y: item.y, rotated: this.rotated });
        });
        return node;
    }

    private section(title: string) {
        const section = document.createElement("section");
        const label = document.createElement("h3");
        label.textContent = title;
        section.append(label);
        return section;
    }

    private grid(box: CargoBox, cargo: Cargo) {
        const section = this.section(`${this.name(box.name)} · ${box.used}/${box.width * box.height}`);
        const grid = document.createElement("div");
        grid.className = "cargo-grid";
        grid.style.setProperty("--columns", String(box.width));
        grid.dataset.container = String(box.id);
        for (let y = 0; y < box.height; ++y) {
            for (let x = 0; x < box.width; ++x) {
                const cell = button("", () => this.put(box, x, y));
                cell.classList.add("pack-cell");
                cell.style.gridArea = `${y + 1} / ${x + 1}`;
                cell.dataset.x = String(x);
                cell.dataset.y = String(y);
                cell.setAttribute("aria-label", `${this.name(box.name)} ${x + 1},${y + 1}`);
                cell.addEventListener("dragover", event => event.preventDefault());
                cell.addEventListener("drop", event => { event.preventDefault(); this.put(box, x, y); });
                grid.append(cell);
            }
        }
        for (const item of cargo.items) {
            if (item.container !== box.id || item.x < 0 || item.y < 0) continue;
            const tile = this.tile(item);
            tile.style.gridArea = `${item.y + 1} / ${item.x + 1} / span ${item.height} / span ${item.width}`;
            grid.append(tile);
        }
        section.append(grid);
        return section;
    }

    private describe() {
        this.details.replaceChildren();
        const item = this.cargo?.items.find(row => row.instance === this.selected);
        if (!item) {
            this.details.textContent = text("选择物品查看详情。拖到空格移动，R 旋转。", "Select an item for details. Drag to place it, R to rotate.");
            return;
        }
        const heading = document.createElement("h3");
        heading.textContent = this.name(item.name);
        const description = document.createElement("p");
        description.textContent = this.name(item.description);
        this.details.append(heading, description);
        const actions = document.createElement("div");
        actions.className = "pack-actions";
        const run = (type: string, extra: Record<string, unknown> = {}) => this.send({ type, instance: item.instance, ...extra });
        if (item.wearable) actions.append(button(item.worn ? text("卸下", "Unequip") : text("装备", "Equip"),
            () => run(item.worn ? "unequip" : "equip")));
        if (item.usable) actions.append(button(text("使用", "Use"), () => run("use")));
        actions.append(button(text("旋转", "Rotate") + (this.rotated ? " ↻" : ""), () => this.rotate()));
        if (item.count > 1) actions.append(button(text("拆分一半", "Split in half"),
            () => this.change("split", item.instance, { count: Math.floor(item.count / 2) })));
        actions.append(button(text("丢弃", "Drop"), () => run("drop")));
        this.details.append(actions);
        if (item.wieldable) {
            item.attachments.forEach((type, slot) => {
                if (type) this.details.append(button(text("卸下", "Detach") + " " + this.name(attachNames[type] ?? ""),
                    () => this.change("detach", item.instance, { slot })));
            });
        } else if (item.attachable) {
            for (const weapon of this.cargo?.items.filter(row => row.wieldable) ?? []) {
                this.details.append(button(text("安装到", "Mount on") + " " + this.name(weapon.name),
                    () => this.change("attach", item.instance, { target: weapon.instance })));
            }
        }
    }

    private draw() {
        const cargo = this.cargo;
        if (!cargo) return;
        if (!cargo.items.some(item => item.instance === this.selected)) this.selected = 0;
        const wear = this.section(text("装备槽", "Equipment"));
        const equipment = [
            [1, text("头部", "Head")], [2, text("上衣", "Torso")], [3, text("背心", "Vest")],
            [4, text("腿部", "Legs")], [5, text("背包", "Back")],
            [6, text("主武器", "Primary")], [7, text("副武器", "Secondary")], [9, text("手枪", "Sidearm")],
        ] as const;
        const rack = document.createElement("div");
        rack.className = "equip-rack";
        for (const [slot, label] of equipment) {
            const weaponSlot = slot === 9 ? 2 : slot - 6;
            const weapon = slot >= 6;
            const item = cargo.items.find(row => weapon ? row.weaponSlot === weaponSlot :
                row.container === wornContainer && row.equipSlot === slot);
            const pocket = document.createElement("div");
            const name = document.createElement("small");
            name.textContent = label;
            pocket.append(name);
            if (item) pocket.append(this.tile(item, true));
            else pocket.append(button(text("空", "Empty")));
            pocket.addEventListener("dragover", event => event.preventDefault());
            pocket.addEventListener("drop", event => {
                event.preventDefault();
                if (this.groundDrag && !weapon) {
                    this.send({ type: "take", id: this.groundDrag, container: wornContainer, slot });
                    return;
                }
                const incoming = cargo.items.find(row => row.instance === this.dragging);
                if (incoming && (weapon ? incoming.wieldable : incoming.equipSlot === slot))
                    this.send({ type: "equip", instance: incoming.instance, ...(weapon ? { slot: weaponSlot } : {}) });
            });
            rack.append(pocket);
        }
        wear.append(rack);
        const ground = this.section(text("附近物资", "Nearby loot"));
        if (this.ground) {
            for (const item of this.ground.contents) {
                const row = document.createElement("p");
                row.textContent = `${this.name(item.name)} ×${item.count}`;
                ground.append(row);
            }
            const id = this.ground.id;
            const take = button(text("拾取", "Take"), () => this.send({ type: "take", id }));
            take.draggable = true;
            take.addEventListener("dragstart", event => {
                this.groundDrag = id;
                event.dataTransfer?.setData("text/plain", id);
            });
            take.addEventListener("dragend", () => { this.groundDrag = ""; this.invalidate(); });
            ground.append(take);
        } else ground.append(document.createTextNode(text("附近没有可拾取的物资", "Nothing within reach")));
        const containers = document.createElement("div");
        containers.className = "cargo-containers";
        for (const box of cargo.containers) containers.append(this.grid(box, cargo));
        this.details.className = "item-detail";
        this.describe();
        const left = document.createElement("div");
        left.className = "pack-equipment";
        left.append(wear, ground);
        this.root.replaceChildren(left, containers, this.details);
    }
}
