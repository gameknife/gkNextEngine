import { readFile } from "node:fs/promises";
import { resolve } from "node:path";
import { fileURLToPath, pathToFileURL } from "node:url";
import { setTimeout as sleep } from "node:timers/promises";
import { RTCPeerConnection } from "node-datachannel/polyfill";
import { Client, loopbackMaster } from "./client.mjs";

const root = fileURLToPath(new URL("../../..", import.meta.url));
const assets = resolve(root, "out/web/nextdayz/assets");
const manifest = JSON.parse(await readFile(resolve(assets, "manifest.json"), "utf8"));
const createModule = (await import(pathToFileURL(resolve(root, "out/web/nextdayz/core/ndcore.mjs")).href)).default;
const get = (flag, fallback) => {
    const index = process.argv.indexOf(flag);
    return index < 0 ? fallback : process.argv[index + 1];
};
const count = Number(get("--bots", 1));
const seconds = Number(get("--seconds", 180));
const latency = Number(get("--latency", 0));
const loss = Number(get("--loss", 0));
const master = get("--master", "http://127.0.0.1:7784");
const mapID = get("--map", "training_ground");
const prefix = get("--name-prefix", `web-${Date.now()}`);
const duration = seconds * 1000;
if (!Number.isInteger(count) || count < 1 || !Number.isFinite(duration) || duration <= 0) {
    throw Error("Invalid bot count or duration");
}

const distance = (a, b) => Math.hypot(...a.map((value, i) => value - b[i]));
const mapFiles = new Map();
function loadMap(scene) {
    if (!mapFiles.has(scene)) {
        const bundle = Object.values(manifest.maps).find(bundle => bundle.map.scene === scene);
        if (!bundle) throw Error(`Map not baked: ${scene}`);
        mapFiles.set(scene, readFile(resolve(assets, bundle.files["anchors.json"].path), "utf8").then(text => {
            const { anchors } = JSON.parse(text);
            const spawn = anchors.find(a => a.type === "playerSafe").position;
            const extractions = anchors.filter(a => a.type === "extraction").map(a => a.position);
            const ration = anchors.find(a => a.item === "water_bottle");
            const staging = ration ? [
                ration.position[0] + (spawn[0] > ration.position[0] ? 9 : -9),
                ration.position[1], ration.position[2] + (spawn[2] > ration.position[2] ? 9 : -9),
            ] : spawn;
            return { spawn, extractions, staging };
        }));
    }
    return mapFiles.get(scene);
}

const bots = await Promise.all(Array.from({ length: count }, async (_, index) => {
    const name = `${prefix}-${index + 1}`;
    const wasm = await createModule();
    const bot = { name, phase: "login", lastAction: 0, takes: 0, result: null, closes: 0 };
    bot.client = new Client(wasm, {
        ...loopbackMaster(master), loadMap,
        rtc: { PeerConnection: RTCPeerConnection, latency, loss },
        onClose: () => { bot.closes++; },
    });
    return bot;
}));

function act(bot, timestamp, soakStarted) {
    const client = bot.client;
    const db = client.read();
    bot.frame = db;
    if (db.fixes) throw Error(`${bot.name} ledger corrections=${db.fixes} ${db.lastFix}`);
    if (bot.phase === "login" && db.phase === "profile") {
        bot.before = client.read("profile");
        client.command({ type: "deploy", map: mapID, bring: [] });
        bot.phase = "join";
    }
    if (bot.phase === "join" && db.phase === "playing" && db.lootReady && db.snapshots > 0) {
        bot.phase = "collect";
        bot.spawn = db.feet;
        bot.lootAttempts = new Map();
        bot.initialItems = new Map(client.read("inventory").map(item => [item.id, item.count]));
        bot.joinedId = db.id;
    }
    if (["collect", "soak", "travel"].includes(bot.phase) && db.phase !== "playing") {
        throw Error(`${bot.name} left during ${bot.phase}: ${db.phase} ${db.message}`);
    }
    if (db.phase === "playing" && !db.alive) throw Error(`${bot.name} died`);
    if (bot.phase === "collect" && !db.moving && timestamp - bot.lastAction >= 850) {
        const loot = client.read("loot");
        const inventory = client.read("inventory");
        if (db.takes >= 2 && db.drops >= 1 && bot.sawPile) {
            bot.phase = "soak";
            loadMap(db.scene).then(({ staging }) => {
                bot.staging = staging;
                client.command({ type: "target", position: staging });
            }).catch(error => { bot.error = error; });
            bot.retained = inventory.filter(item => !item.hydration && item.count > (bot.initialItems.get(item.id) ?? 0));
            if (!bot.retained.length) throw Error(`${bot.name} has no item to place in storage`);
        } else if (db.takes > 0 && db.drops === 0 && inventory.length) {
            const item = inventory.find(item => item.count > (bot.initialItems.get(item.id) ?? 0));
            if (item) {
                bot.dropped = item.id;
                client.command({ type: "drop", instance: item.instance });
                bot.dropAt = db.feet;
            }
        } else {
            if (bot.dropAt && loot.some(entry => entry.id.startsWith("pile:") &&
                distance(entry.position, bot.dropAt) < 4 && entry.contents.some(item => item.id === bot.dropped))) {
                bot.sawPile = true;
            }
            const target = loot.filter(entry => !entry.id.startsWith("pile:") &&
                (bot.lootAttempts.get(entry.id) ?? 0) + 2500 < timestamp)
                .filter(entry => entry.contents.some(item => item.area <= 2 && !item.hydration && !item.nutrition))
                .sort((a, b) => distance(a.position, db.feet) - distance(b.position, db.feet))[0];
            if (target) {
                if (distance(target.position, db.feet) < 0.75) {
                    client.command({ type: "take", id: target.id });
                    bot.lootAttempts.set(target.id, timestamp);
                } else client.command({ type: "target", position: target.position });
            }
        }
        bot.lastAction = timestamp;
    }
    if (bot.phase === "soak" && (!bot.supply || timestamp - soakStarted >= duration) && !db.moving && soakStarted && timestamp - soakStarted >= duration) {
        bot.phase = "travel";
        bot.plan = loadMap(db.scene).then(({ extractions }) => {
            const target = [...extractions].sort((a, b) => distance(a, db.feet) - distance(b, db.feet))[0];
            if (!target) throw Error("No extraction point");
            client.command({ type: "target", position: target });
            bot.arriving = true;
            bot.lastAction = performance.now();
        }).catch(error => { bot.error = error; });
    }
    if (bot.error) throw bot.error;
    if (bot.phase === "travel" && bot.arriving && !db.moving && timestamp - bot.lastAction > 1200) {
        client.command({ type: "extract" });
        bot.phase = "extract";
        bot.lastAction = timestamp;
    }
    if (bot.phase === "extract") {
        if (db.extractBroken || timestamp - bot.lastAction > 45000)
            throw Error(`${bot.name} extraction did not finish: ${db.message}`);
        if (db.phase === "profile" && db.extracted) {
            const profile = client.read("profile");
            const itemsReturned = bot.retained.some(item => {
                const before = bot.before.storage.filter(row => row.id === item.id).reduce((n, row) => n + row.count, 0);
                const after = profile.storage.filter(row => row.id === item.id).reduce((n, row) => n + row.count, 0);
                return after > before;
            });
            if (!profile.character && profile.extractions > bot.before.extractions && itemsReturned) {
                bot.result = { id: bot.joinedId, takes: db.takes, drops: db.drops, extractions: profile.extractions, closes: bot.closes };
                bot.phase = "done";
            } else if (timestamp - bot.lastAction > 14000 && timestamp - (bot.lastProfile ?? 0) > 1000) {
                bot.lastProfile = timestamp;
                client.command({ type: "profile" });
            }
        }
    }
}

// One runner at a time visits the ration tables. Training displays restock
// when the previous visitor steps away, so the others wait in a staging area.
let supplier;
function resupply(timestamp) {
    if (!supplier) {
        supplier = bots.filter(bot => bot.phase === "soak" && bot.staging && !bot.frame.moving &&
            (bot.frame.body.hydration < 60 || bot.frame.body.hunger < 50))
            .sort((a, b) => Math.min(a.frame.body.hydration - 10, a.frame.body.hunger) -
                            Math.min(b.frame.body.hydration - 10, b.frame.body.hunger))[0];
        if (!supplier) return;
        supplier.supply = { kind: supplier.frame.body.hydration - 10 <= supplier.frame.body.hunger ? "hydration" : "nutrition" };
    }
    const bot = supplier;
    const { client, frame: db, supply } = bot;
    if (db.moving || timestamp - bot.lastAction < 1200) return;
    if (supply.returning) {
        bot.supply = undefined;
        supplier = undefined;
        return;
    }
    const eligible = item => item[supply.kind] >= (supply.kind === "hydration" ? 20 : 9);
    const carried = client.read("inventory");
    const prepared = carried.filter(eligible).sort((a, b) => b[supply.kind] - a[supply.kind])[0];
    if (prepared) {
        client.command({ type: "use", instance: prepared.instance });
        client.command({ type: "target", position: bot.staging });
        supply.returning = true;
        bot.lastAction = timestamp;
        return;
    }

    if (supply.requested && timestamp - supply.requested > 2400 && db.takes === supply.takes && !supply.cleared) {
        // Recover a grid cell if the body could not stow a ration.
        const empty = carried.find(item => item.id === "water_bottle_empty");
        if (empty) client.command({ type: "drop", instance: empty.instance });
        supply.cleared = true;
    }
    const food = client.read("loot").filter(entry => !entry.id.startsWith("pile:") && entry.contents.some(eligible))
        .sort((a, b) => distance(a.position, db.feet) - distance(b.position, db.feet))[0];
    if (food) {
        if (distance(food.position, db.feet) < 0.75) {
            client.command({ type: "take", id: food.id });
            supply.requested = timestamp;
            supply.takes = db.takes;
        } else client.command({ type: "target", position: food.position });
    } else if (distance(bot.staging, db.feet) > 0.75) {
        client.command({ type: "target", position: bot.staging });
    }
    bot.lastAction = timestamp;
}

let started = 0;
try {
    for (const bot of bots) bot.client.command({ type: "login", name: bot.name, password: process.env.ND_BOT_PASSWORD });
    const start = performance.now();
    let previous = start, lastLog = start;
    while (bots.some(bot => !bot.result)) {
        const now = performance.now();
        const dt = (now - previous) / 1000;
        for (const bot of bots) {
            if (bot.result) continue;
            bot.client.tick(dt);
            act(bot, now, started);
        }
        if (started && now - started < duration) resupply(now);
        if (!started && bots.every(bot => bot.phase === "soak" && bot.staging && !bot.frame.moving)) {
            started = now;
            console.log(`Soak started: ${count}/${count} online, ${latency} ms delay, ${loss}% loss, ${seconds}s`);
        }
        if (now - lastLog >= 10000) {
            const phases = Object.groupBy(bots, bot => bot.phase);
            console.log(Math.round((now - start) / 1000), "s", Object.fromEntries(Object.entries(phases).map(([k, v]) => [k, v.length])),
                "snapshots", Math.min(...bots.filter(bot => !bot.result).map(bot => bot.frame.snapshots)));
            lastLog = now;
        }
        if (now - start > duration + 1200000) throw Error("Full-flow deadline expired");
        previous = now;
        await sleep(16);
    }
    console.log(`RESULT ok online=${count}/${count} takes=${bots.reduce((n, b) => n + b.result.takes, 0)} drops=${bots.reduce((n, b) => n + b.result.drops, 0)} extracted=${count} refusals=0`);
    console.log(JSON.stringify(bots.map(b => ({ name: b.name, ...b.result }))));
} catch (error) {
    console.error(error);
    for (const bot of bots) console.error(bot.name, bot.phase, bot.frame);
    process.exitCode = 1;
} finally {
    for (const bot of bots) bot.client.destroy();
}
