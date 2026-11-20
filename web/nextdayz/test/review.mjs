import assert from "node:assert/strict";
import { spawn } from "node:child_process";
import { mkdir, readFile, writeFile } from "node:fs/promises";
import { fileURLToPath } from "node:url";
import { resolve } from "node:path";
import { chromium } from "playwright";

const repo = fileURLToPath(new URL("../../..", import.meta.url));
const evidence = resolve(repo, "out/web/nextdayz/w4-evidence");
const site = process.env.ND_TEST_SITE ?? "http://127.0.0.1:4179";
const master = process.env.ND_TEST_MASTER ?? "http://127.0.0.1:7785";
const preset = process.env.ND_TEST_PRESET ?? "macos-arm64";
const map = "cz_loket_riverbend";
const scene = "assets/scad/proc/nextdayz/cz_loket_riverbend/published/v8/scene.scad";
const run = Date.now();
const name = `W4 Web ${run}`;
const frame = page => page.evaluate(() => window.nextdayzReview());
const wait = (page, selector) => page.locator(selector).waitFor({ state: "visible", timeout: 120000 });
await mkdir(evidence, { recursive: true });

async function login(page, player) {
    await page.goto(site);
    await page.locator("#name").fill(player);
    await page.locator("#login-button").click();
    await wait(page, "#profile");
}

async function deploy(page) {
    await page.locator(`input[name=map][value=${map}]`).check();
    const start = Date.now();
    await page.locator("#deploy").click();
    await wait(page, "#field");
    await page.waitForFunction(() => window.nextdayzReview().frame.lootReady && window.nextdayzReview().frame.snapshots >= 4);
    const result = await frame(page);
    assert.equal(result.frame.scene, scene);
    assert.equal(result.frame.lootCount, 421);
    assert(result.frame.id > 0);
    return { ...result, seconds: (Date.now() - start) / 1000 };
}

async function observeFromDesktop(page, web) {
    const [x, , z] = web.frame.feet;
    const reportName = "nextdayz-web-desktop-observer";
    const scriptPath = resolve(evidence, reportName + ".agentscript.json");
    const test = (query, op, value) => ({ type: "assert", query: `game.${query}`, op, value });
    const waitFor = (query, op, value) => ({ ...test(query, op, value), type: "wait-until", timeoutMs: 120000, fatal: true });
    const script = {
        name: reportName, target: "NextDayz",
        viewport: { width: 1440, height: 900 },
        args: ["--cvar", "nextdayz.net.mode client", "--cvar", `nextdayz.net.master ${master}`,
            "--cvar", `nextdayz.net.name W4 Observer ${run}`, "--cvar", "nextdayz.net.autoDeploy true", "--cvar", `nextdayz.net.autoMap ${map}`],
        steps: [
            waitFor("netLobby", "eq", "playing"),
            waitFor("netLootSynced", "eq", true),
            { type: "cvar", name: "nextdayz.validation.teleport", set: `${x},${-(z - 4)}` },
            { type: "cvar", name: "nextdayz.validation.lookYaw", set: 0 },
            waitFor("netVisibleRemotes", "ge", 1),
            { type: "wait-ms", ms: 1000 },
            test("netSurvivorId", "ge", 2),
            test("netVisibleRemotes", "ge", 1),
            { type: "screenshot", out: "screenshots/nextdayz_web_desktop_observer", ui: true },
            test("netClockWeather", "ne", "Unknown"),
            test("netClockMinutes", "ge", 0),
            { type: "wait-ms", ms: 600 },
            { type: "quit" },
        ],
    };
    await writeFile(scriptPath, JSON.stringify(script));
    const child = spawn(resolve(repo, "gnb.sh"), ["validate", "--script", scriptPath], { cwd: repo, stdio: ["ignore", "pipe", "pipe"] });
    let logs = "";
    child.stdout.on("data", chunk => { logs += chunk.toString(); });
    child.stderr.on("data", chunk => { logs += chunk.toString(); });
    const exited = new Promise((done, reject) => {
        child.once("error", reject);
        child.once("exit", done);
    });
    let lastSeen;
    const observer = setInterval(async () => {
        try {
            const result = await frame(page);
            if (result.frame.remotes.length) lastSeen = result.frame;
        } catch {
            // The polling callback stops before the browser is closed.
        }
    }, 220);
    try {
        assert.equal(await exited, 0, logs.slice(-1100));
        const report = JSON.parse(await readFile(resolve(repo, `out/build/${preset}/agent_reports/${reportName}.json`), "utf8"));
        assert(report.passed);
        const actual = query => report.steps.find(step => step.query === `game.${query}`)?.actual ??
            report.steps[script.steps.findIndex(step => step.query === `game.${query}` && step.type === "assert")].actual;
        assert(lastSeen);
        assert(lastSeen.remotes.some(remote => remote.id === Number(actual("netSurvivorId"))));
        assert.equal(lastSeen.clock.weather, actual("netClockWeather"));
        const clockDelta = Math.abs(lastSeen.clock.minutes - Number(actual("netClockMinutes")));
        assert(clockDelta < 2.3, `desktop/browser time divergence ${clockDelta} game minutes`);
        return { desktopId: actual("netSurvivorId"), webId: web.frame.id, visibleOnDesktop: actual("netVisibleRemotes"),
            clockDelta, weather: actual("netClockWeather"), desktopReport: report };
    } finally {
        clearInterval(observer);
        await writeFile(resolve(evidence, "desktop-observer.log"), logs);
        if (child.exitCode === null) child.kill("SIGINT");
    }
}

const browser = await chromium.launch({
    ...(process.env.ND_TEST_CHROME === "0" ? {} : { channel: "chrome" }),
    args: ["--enable-webgl", "--ignore-gpu-blocklist"],
});
try {
    const context = await browser.newContext({ viewport: { width: 1920, height: 1080 } });
    const page = await context.newPage();
    const errors = [];
    page.on("pageerror", error => errors.push(error.message));
    if (process.env.ND_TEST_STORAGE_NAME) {
        await login(page, process.env.ND_TEST_STORAGE_NAME);
        const stock = (await frame(page)).profile.storage;
        assert(stock.length > 0);
        await page.screenshot({ path: resolve(evidence, "lobby-storage.png") });
    }
    await login(page, name);
    const cold = await deploy(page);
    assert.equal(cold.cacheHits, 0);
    await page.locator("#detail").click();
    await page.screenshot({ path: resolve(evidence, "loket-live.png") });

    const mixed = process.env.ND_TEST_DESKTOP === "1" ? await observeFromDesktop(page, cold) : undefined;
    const lighting = {};
    for (const [kind, minutes, mode] of [["day", 720, 0], ["dusk", 1065, 0], ["night", 1380, 0], ["fog", 720, 2]]) {
        await page.evaluate(([time, weather]) => window.nextdayzLightPreview(time, weather), [minutes, mode]);
        await page.waitForFunction(() => document.getElementById("world-clock").textContent.includes("光照对照"));
        await page.evaluate(() => new Promise(done => requestAnimationFrame(() => requestAnimationFrame(done))));
        await page.screenshot({ path: resolve(evidence, `loket-${kind}.png`) });
        lighting[kind] = await page.locator("#world-clock").textContent();
    }
    await page.evaluate(() => window.nextdayzLightPreview());
    await page.locator("#capture").click();
    await page.waitForFunction(() => Boolean(document.pointerLockElement), { timeout: 10000 });
    await page.keyboard.down("w");
    await page.waitForTimeout(350);
    await page.keyboard.up("w");
    await page.evaluate(() => document.exitPointerLock());
    await wait(page, "#menu");
    await page.locator("#leave").click();
    await wait(page, "#profile");
    await page.waitForFunction(() => window.nextdayzReview().profile.character && !window.nextdayzReview().profile.playing);
    await login(page, name);
    const warm = await deploy(page);
    assert(warm.cacheHits > 100);
    assert.deepEqual(errors, []);
    await page.locator("#leave").click();
    await wait(page, "#profile");

    const result = {
        cold: { seconds: cold.seconds, drawCalls: cold.drawCalls, triangles: cold.triangles, cacheHits: cold.cacheHits },
        warm: { seconds: warm.seconds, cacheHits: warm.cacheHits },
        lighting, mixed,
    };
    await writeFile(resolve(evidence, "verification.json"), JSON.stringify(result, null, 2));
    console.log(JSON.stringify({ ...result, mixed: mixed ? { ...mixed, desktopReport: undefined } : undefined }, null, 2));
} finally {
    await browser.close();
}
