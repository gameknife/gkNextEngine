import assert from "node:assert/strict";
import { spawn, execFileSync } from "node:child_process";
import { mkdir, readFile, writeFile } from "node:fs/promises";
import { chromium } from "playwright";
import { resolve } from "node:path";
import { fileURLToPath } from "node:url";

const repo = fileURLToPath(new URL("../../..", import.meta.url));
const evidence = resolve(repo, "out/web/nextdayz/w7-evidence");
const site = process.env.ND_TEST_SITE ?? "http://127.0.0.1:4182";
const master = process.env.ND_TEST_MASTER ?? "http://127.0.0.1:7797";
const name = process.env.ND_TEST_RESUME_NAME ?? `W7 Mixed Browser ${Date.now()}`;
const preset = process.env.ND_TEST_PRESET ?? "macos-arm64";
const gnb = resolve(repo, "gnb.sh");
const map = "training_ground";
const browser = await chromium.launch({ channel: "chrome", args: ["--enable-webgl", "--ignore-gpu-blocklist"] });
const readMaster = (...args) => JSON.parse(execFileSync(gnb, ["master", ...args, "--url", master], { cwd: repo, encoding: "utf8" }));
const pvp = (server, on) => execFileSync(gnb, ["master", "pvp", server, `--on=${on}`, "--url", master], { cwd: repo });
const result = {};
await mkdir(evidence, { recursive: true });
let serverId;
let observer;
let log = "";

try {
    const page = await browser.newPage({ viewport: { width: 1440, height: 900 } });
    const errors = [];
    page.on("pageerror", error => errors.push(error.message));
    const review = () => page.evaluate(() => window.nextdayzReview());
    const wait = (probe, args, timeout = 20000) => page.waitForFunction(probe, args, { timeout });
    const place = (position, yaw) => page.evaluate(data => window.nextdayzReviewPlace(data.position, data.yaw), { position, yaw });
    const capture = async () => {
        await page.locator("#capture").click();
        await wait(() => Boolean(document.pointerLockElement));
        await page.waitForTimeout(170);
    };
    const release = async () => {
        await page.evaluate(() => document.exitPointerLock());
        await wait(() => !document.pointerLockElement);
    };
    const shot = async duration => {
        await page.mouse.down();
        await page.waitForTimeout(duration);
        await page.mouse.up();
    };
    await page.goto(site);
    await page.locator("#name").fill(name);
    await page.locator("#login-button").click();
    await page.locator(`input[name=map][value=${map}]`).check();
    await page.locator("#deploy").click();
    await page.locator("#field").waitFor({ timeout: 120000 });
    await page.evaluate(() => window.nextdayzLightPreview(540, 0));
    await wait(() => window.nextdayzReview().frame.snapshots >= 8);
    const status = readMaster("status");
    const account = status.accounts.find(entry => entry.name === name);
    serverId = status.servers.find(server => server.players.includes(account?.id))?.id;
    assert(serverId);
    pvp(serverId, false);
    const take = async (id, position) => {
        if ((await review()).cargo.items.some(item => item.id === id)) return;
        await place(position, Math.PI);
        await capture();
        await page.locator("#field-hint").waitFor();
        await page.keyboard.press("e");
        await wait(item => window.nextdayzReview().cargo.items.some(row => row.id === item), id);
        await release();
    };
    await take("ak", [57.875, 11.45, 58.45]);
    await take("ammo_545", [58.8, 11.45, 61.45]);
    assert.equal((await review()).frame.combat.weapon, "AK-74");

    const cvar = (key, set) => ({ type: "cvar", name: `nextdayz.validation.${key}`, set });
    const until = (query, op, value, timeoutMs = 60000) => ({
        type: "wait-until", query: `game.${query}`, op, value, timeoutMs, fatal: true,
    });
    const check = (query, op, value) => ({ type: "assert", query: `game.${query}`, op, value });
    const pause = ms => ({ type: "wait-ms", ms });
    const desktopTest = "nextdayz-w7-mixed-desktop";
    const script = {
        name: desktopTest, target: "NextDayz", viewport: { width: 1280, height: 720 },
        args: ["--cvar", "nextdayz.net.mode client", "--cvar", `nextdayz.net.master ${master}`,
            "--cvar", `nextdayz.net.name W7 Desktop ${Date.now()}`, "--cvar", "nextdayz.net.autoDeploy true",
            "--cvar", `nextdayz.net.autoMap ${map}`],
        steps: [
            until("netLobby", "eq", "playing"), cvar("teleport", "-270,-81"), cvar("lookYaw", 180),
            until("netVisibleRemotes", "ge", 1),
            cvar("give", "bandage:5"), until("netInventoryFixes", "ge", 1, 10000),
            { type: "screenshot", out: "screenshots/web-and-desktop-combat", ui: true },
            until("netPvp", "eq", true),
            check("health", "eq", 100), until("health", "le", 0, 15000),
            pause(1000), { type: "quit" },
        ],
    };
    const desktopPath = resolve(evidence, desktopTest + ".agentscript.json");
    await writeFile(desktopPath, JSON.stringify(script));
    observer = spawn(gnb, ["validate", "--script", desktopPath], { cwd: repo, stdio: ["ignore", "pipe", "pipe"] });
    observer.stdout.on("data", chunk => { log += chunk; });
    observer.stderr.on("data", chunk => { log += chunk; });
    const exited = new Promise(done => observer.once("exit", done));

    await place([-270, 11, 76.5], 0);
    await wait(() => window.nextdayzReview().frame.remotes.some(remote => Math.abs(remote.position[0] + 270) < 1), undefined, 90000);
    const peer = (await review()).frame.remotes.find(remote => Math.abs(remote.position[0] + 270) < 1);
    await wait(() => Math.abs(window.nextdayzReview().frame.feet[0] + 270) < .2);
    await capture();
    await page.keyboard.press("v");
    if ((await review()).frame.combat.magazine < 8) {
        await page.keyboard.press("r");
        await page.waitForTimeout(2500);
    }
    await shot(260);
    assert.equal((await review()).frame.pvp, false);
    await page.waitForTimeout(450);
    pvp(serverId, true);
    await wait(() => window.nextdayzReview().frame.pvp);
    // Aim at the relatively stationary desktop survivor, who is 4.5 m in
    // front of the browser, during the dedicated verification pause.
    await shot(1750);
    await wait(id => window.nextdayzReview().frame.remotes.some(remote => remote.id === id && !remote.alive), peer.id, 10000);
    result.pvp = { desktopId: peer.id, deathReplicated: true, toggled: true };
    await page.screenshot({ path: resolve(evidence, "friendly-fire.png") });
    pvp(serverId, false);
    await release();
    await exited;
    await writeFile(resolve(evidence, "mixed-desktop.log"), log);
    const report = JSON.parse(await readFile(resolve(repo, `out/build/${preset}/agent_reports/${desktopTest}.json`), "utf8"));
    assert(report.passed, log.slice(-1800));
    result.desktop = { survivedFriendlyFireOff: true, downedWithFriendlyFireOn: true, inventoryCorrected: true,
        run: log.match(/validation run: (\S+)/)?.[1] };
    await page.locator("#leave").click();
    await page.locator("#profile").waitFor();
    assert.deepEqual(errors, []);
    await writeFile(resolve(evidence, "mixed.json"), JSON.stringify(result, null, 2));
    console.log(JSON.stringify(result));
} finally {
    if (serverId) {
        try { pvp(serverId, false); } catch { /* Server may already be idle. */ }
    }
    if (observer?.exitCode === null && observer.signalCode === null) observer.kill("SIGINT");
    if (log) await writeFile(resolve(evidence, "mixed-desktop.log"), log);
    await browser.close();
}
