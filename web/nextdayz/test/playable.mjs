import assert from "node:assert/strict";
import { spawn } from "node:child_process";
import { mkdir, readFile, writeFile } from "node:fs/promises";
import { chromium } from "playwright";
import { resolve } from "node:path";
import { fileURLToPath } from "node:url";

const repo = fileURLToPath(new URL("../../..", import.meta.url));
const evidence = resolve(repo, "out/web/nextdayz/w5-evidence");
const site = process.env.ND_TEST_SITE ?? "http://127.0.0.1:4181";
const master = process.env.ND_TEST_MASTER ?? "http://127.0.0.1:7795";
const preset = process.env.ND_TEST_PRESET ?? "macos-arm64";
const browser = await chromium.launch({ channel: "chrome", args: ["--enable-webgl", "--ignore-gpu-blocklist"] });
const map = "training_ground";
const run = Date.now();
const recoveredBrowser = process.env.ND_TEST_EQUIPPED_WEB_NAME;
const recoveredDesktop = process.env.ND_TEST_EQUIPPED_DESKTOP_NAME;
const files = resolve(repo, "out/web/nextdayz/assets");
const { maps } = JSON.parse(await readFile(resolve(files, "manifest.json"), "utf8"));
const { anchors } = JSON.parse(await readFile(resolve(files, maps[map].files["anchors.json"].path), "utf8"));
const results = {};
const errors = [];
await mkdir(evidence, { recursive: true });

try {
    const page = await browser.newPage({ viewport: { width: 1920, height: 1080 } });
    page.on("pageerror", error => errors.push(error.message));
    const frame = () => page.evaluate(() => window.nextdayzReview().frame);
    const wait = (predicate, arg) => page.waitForFunction(predicate, arg, { timeout: 20000 });
    const capture = async () => {
        await page.locator("#capture").click();
        await wait(() => Boolean(document.pointerLockElement));
        await page.waitForTimeout(170);
    };
    const release = () => page.evaluate(() => document.exitPointerLock());
    const place = ([x, y, z], yaw = 0) => page.evaluate(data => window.nextdayzReviewPlace(data.position, data.yaw),
        { position: [x, y, z], yaw });
    await page.goto(site);
    await page.locator("#name").fill(recoveredBrowser ?? `W5 Playable ${run}`);
    await page.locator("#login-button").click();
    await page.locator(`input[name=map][value=${map}]`).check();
    await page.locator("#deploy").click();
    await page.locator("#field").waitFor({ state: "visible", timeout: 120000 });
    await wait(() => {
        const frame = window.nextdayzReview().frame;
        return frame.lootReady && frame.snapshots >= 5 && frame.pose?.matrixOffset >= 0;
    });
    await page.evaluate(() => window.nextdayzLightPreview(540, 0));

    await page.locator("#course").click();
    await wait(() => Math.abs(window.nextdayzReview().frame.feet[0] + 270) < 1);
    await capture();
    await page.keyboard.down("w");
    await wait(() => window.nextdayzReview().frame.pose.gait === "run");
    await page.keyboard.down("x");
    await wait(() => window.nextdayzReview().frame.pose.gait === "walk");
    await page.keyboard.up("x");
    await page.keyboard.down("Shift");
    await wait(() => window.nextdayzReview().frame.pose.gait === "sprint");
    await page.keyboard.up("Shift");
    await page.keyboard.up("w");
    await page.keyboard.press("c");
    await wait(() => window.nextdayzReview().frame.pose.stance === "crouched");
    await page.keyboard.press("c");
    await wait(() => window.nextdayzReview().frame.pose.stance === "standing");
    await page.keyboard.press("v");
    await wait(() => window.nextdayzReview().frame.camera.firstPerson);
    await page.keyboard.press("v");
    await wait(() => !window.nextdayzReview().frame.camera.firstPerson);

    // Thin 1.2 m board at the first station. The same physical capsule first
    // encounters a wall, then the contextual action moves it over the board.
    await release();
    await place([-270, 12, 73], Math.PI);
    await wait(() => Math.abs(window.nextdayzReview().frame.feet[2] - 73) < .25);
    await capture();
    await page.keyboard.down("w");
    await page.waitForTimeout(950);
    const blocked = await frame();
    assert(blocked.feet[2] > 70.95 && blocked.feet[2] < 71.9, JSON.stringify(blocked.feet));
    await page.keyboard.press("Space");
    await wait(() => window.nextdayzReview().frame.pose.traversal !== "none");
    const traversal = await frame();
    await page.screenshot({ path: resolve(evidence, "training-traversal.png") });
    await wait(() => window.nextdayzReview().frame.feet[2] < 70.5);
    await page.keyboard.up("w");
    results.course = { blocked: blocked.feet, action: traversal.pose.traversal, cleared: (await frame()).feet };
    await release();

    const gear = [
        { id: "jacket", name: /jacket/i, shift: [.65, -.45], aim: 0 },
        { id: "jeans", name: /jeans/i, shift: [-.65, -.25], aim: 0 },
        { id: "ak", name: /AK-74/i, shift: [0, 1], aim: Math.PI },
    ];
    for (const { id, name, shift, aim } of recoveredBrowser ? [] : gear) {
        const anchor = anchors.find(entry => entry.item === id);
        const [x, y, z] = anchor.position;
        await place([x + shift[0], y + .6, z + shift[1]], aim);
        await wait(expected => {
            const frame = window.nextdayzReview().frame;
            return Math.hypot(frame.feet[0] - expected[0], frame.feet[2] - expected[1]) < 1;
        }, [x + shift[0], z + shift[1]]);
        await capture();
        await page.locator("#field-hint").waitFor({ state: "visible", timeout: 4000 });
        assert(name.test((await page.locator("#field-hint").textContent()) ?? ""), await page.locator("#field-hint").textContent());
        const before = (await frame()).takes;
        await page.keyboard.press("e");
        await wait(takes => window.nextdayzReview().frame.takes > takes, before);
        await page.keyboard.press("Tab");
        const garment = page.locator(`#items [data-item="${id}"]`).first();
        await garment.waitFor();
        const hasItem = await page.evaluate(item => window.nextdayzReview().cargo.items.some(row => row.id === item && row.worn), id);
        if (!hasItem) {
            await garment.click();
            await page.locator(".item-detail").getByRole("button", { name: "装备", exact: true }).click();
        }
        await wait(item => window.nextdayzReview().cargo.items.some(row => row.id === item && row.worn), id);
        await page.getByRole("button", { name: "关闭", exact: true }).click();
    }
    const dressed = await frame();
    assert.equal(dressed.pose.weapon, "ak");
    assert(dressed.pose.clothing.includes("jacket") && dressed.pose.clothing.includes("jeans"));
    await page.locator("#course").click();
    await capture();
    await page.mouse.down({ button: "right" });
    await wait(() => window.nextdayzReview().frame.pose.aim > 0);
    await page.screenshot({ path: resolve(evidence, "training-dressed.png") });
    await page.mouse.up({ button: "right" });
    await page.waitForTimeout(440);
    if (!(await frame()).camera.firstPerson) await page.keyboard.press("v");
    await wait(() => window.nextdayzReview().frame.pose.viewModelArms);
    await page.screenshot({ path: resolve(evidence, "training-first-person.png") });
    await page.keyboard.press("v");
    await wait(() => !window.nextdayzReview().frame.camera.firstPerson);
    await release();
    results.equipment = { clothing: dressed.pose.clothing, weapon: dressed.pose.weapon, takes: dressed.takes };

    if (process.env.ND_TEST_DESKTOP === "1") {
        const name = "nextdayz-web-playable-observer";
        const path = resolve(evidence, name + ".agentscript.json");
        const [x, , z] = (await frame()).feet;
        const cvar = (key, set) => ({ type: "cvar", name: `nextdayz.validation.${key}`, set });
        const until = (query, op, value) => ({ type: "wait-until", query: `game.${query}`, op, value, timeoutMs: 90000, fatal: true });
        const script = {
            name, target: "NextDayz", viewport: { width: 1440, height: 900 },
            args: ["--cvar", "nextdayz.net.mode client", "--cvar", `nextdayz.net.master ${master}`,
                "--cvar", `nextdayz.net.name ${recoveredDesktop ?? "W5 Observer " + run}`, "--cvar", "nextdayz.net.autoDeploy true",
                "--cvar", `nextdayz.net.autoMap ${map}`],
            steps: [until("netLobby", "eq", "playing"), cvar("teleport", `${x},${-(z + 5)}`),
                cvar("lookYaw", 180), until("netVisibleRemotes", "ge", 1),
                ...(recoveredDesktop ? [cvar("equip", "1:ak"), until("equippedWeapon", "eq", "ak")] : []),
                { type: "wait-ms", ms: 500 },
                { type: "screenshot", out: "screenshots/w5-web-survivor", ui: false },
                { type: "key", code: "W", action: "down" }, { type: "wait-ms", ms: 350 },
                { type: "key", code: "W", action: "up" }, { type: "wait-ms", ms: 1200 }, { type: "quit" }],
        };
        await writeFile(path, JSON.stringify(script));
        const child = spawn(resolve(repo, "gnb.sh"), ["validate", "--script", path], { cwd: repo, stdio: ["ignore", "pipe", "pipe"] });
        let log = "";
        child.stdout.on("data", chunk => { log += chunk; });
        child.stderr.on("data", chunk => { log += chunk; });
        const exited = new Promise((done, reject) => {
            child.once("error", reject);
            child.once("exit", done);
        });
        const seen = [];
        const watch = setInterval(() => {
            void frame().then(state => seen.push(...state.remotes)).catch(() => {});
        }, 80);
        await place([x, 12, z - 1], 0);
        try {
            await wait(() => window.nextdayzReview().frame.remotes.some(remote => remote.alive && remote.matrixOffset >= 0));
            await page.screenshot({ path: resolve(evidence, "web-desktop-survivor.png") });
            await exited;
            const report = JSON.parse(await readFile(resolve(repo, `out/build/${preset}/agent_reports/${name}.json`), "utf8"));
            assert(report.passed, log.slice(-1600));
            assert(seen.some(remote => remote.pose.gait === "run"));
            if (recoveredDesktop) assert(seen.some(remote => remote.pose.weapon === "ak" && remote.pose.clothing.includes("jacket")));
            results.desktop = { observed: seen.find(remote => remote.pose.weapon && remote.pose.clothing.length) ?? seen.at(-1), gearSeen: seen.some(remote => remote.pose.weapon && remote.pose.clothing.length),
                run: log.match(/validation run: (\S+)/)?.[1] };
        } finally {
            clearInterval(watch);
            await writeFile(resolve(evidence, "desktop.log"), log);
            if (child.exitCode === null && child.signalCode === null) child.kill("SIGINT");
        }
    }

    await place([-227, 12, 31], Math.PI);
    await wait(() => Math.abs(window.nextdayzReview().frame.feet[0] + 227) < 1);
    await page.evaluate(() => window.nextdayzReviewCrowd(48));
    await wait(() => window.nextdayzReview().frame.zombies.filter(zombie => zombie.matrixOffset >= 0).length === 48);
    await page.waitForTimeout(500);
    const timings = await page.evaluate(() => new Promise(done => {
        const durations = [];
        let last = 0;
        function sample(timestamp) {
            if (last) durations.push(timestamp - last);
            last = timestamp;
            if (durations.length === 240) done(durations);
            else requestAnimationFrame(sample);
        }
        requestAnimationFrame(sample);
    }));
    timings.sort((a, b) => a - b);
    const { drawCalls, triangles } = await page.evaluate(() => window.nextdayzReview());
    assert(drawCalls > 0 && drawCalls <= 1500);
    results.crowd = { infected: 48, viewport: page.viewportSize(), frameMedianMs: timings[120], frameP95Ms: timings[227], drawCalls, triangles };
    await page.screenshot({ path: resolve(evidence, "training-48-infected.png") });
    await page.evaluate(() => window.nextdayzReviewCrowd(0));

    assert.deepEqual(errors, []);
    await page.locator("#leave").click();
    await page.locator("#profile").waitFor();
    await writeFile(resolve(evidence, "playable.json"), JSON.stringify(results, null, 2));
    console.log(JSON.stringify(results));
} finally {
    await browser.close();
}
