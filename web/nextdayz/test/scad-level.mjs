import assert from "node:assert/strict";
import { mkdir, readFile, writeFile } from "node:fs/promises";
import { resolve } from "node:path";
import { fileURLToPath } from "node:url";
import { chromium } from "playwright";
import { openPage } from "./harness.mjs";

const repo = fileURLToPath(new URL("../../..", import.meta.url));
const evidence = resolve(repo, "out/web/nextdayz/scad-level");
const site = new URL(process.env.ND_TEST_SITE ?? "http://127.0.0.1:4185");
const baked = resolve(repo, "out/web/nextdayz/assets");
const manifest = JSON.parse(await readFile(resolve(baked, "manifest.json"), "utf8"));
const map = manifest.maps.training_ground;
const reference = JSON.parse(await readFile(resolve(baked, map.files["bake.json"].path), "utf8"));
await mkdir(evidence, { recursive: true });

const browser = await chromium.launch({ channel: "chrome", args: ["--enable-webgl", "--ignore-gpu-blocklist"] });
try {
    const page = await openPage(browser, { viewport: { width: 1390, height: 900 } });
    const errors = [];
    const requests = [];
    page.on("pageerror", error => errors.push(error.message));
    page.on("response", response => {
        const url = new URL(response.url());
        if (url.origin !== site.origin) return;
        const headers = response.headers();
        requests.push({
            path: url.pathname, status: response.status(), bytes: Number(headers["content-length"] ?? 0),
            encoding: headers["content-encoding"] ?? "identity",
        });
    });
    const read = () => page.evaluate(() => window.nextdayzReview());
    const wait = (query, value, timeout = 20000) => page.waitForFunction(query, value, { timeout });
    const place = (position, yaw) => page.evaluate(({ position, yaw }) => window.nextdayzReviewPlace(position, yaw), { position, yaw });
    const capture = async () => {
        await page.locator("#capture").click();
        await wait(() => Boolean(document.pointerLockElement));
    };
    const release = async () => {
        await page.evaluate(() => document.exitPointerLock());
        await wait(() => !document.pointerLockElement);
    };

    const name = `SCAD Training ${Date.now()}`;
    await page.goto(site.href);
    await page.locator("#name").fill(name);
    await page.locator("#login-button").click();
    await page.locator('input[name=map][value="training_ground"]').check();
    const began = Date.now();
    await page.locator("#deploy").click();
    await page.locator("#field").waitFor({ timeout: 120000 });
    await wait(() => {
        const { frame } = window.nextdayzReview();
        return frame.snapshots >= 8 && frame.lootReady && frame.pose?.grounded;
    });
    const firstJoinMs = Date.now() - began;
    const { frame, scad } = await read();
    assert.equal(frame.scene, map.map.scene);
    assert.equal(scad.evaluatedTriangles, reference.sourceTriangles);
    assert.equal(scad.triangles, reference.desktopTriangles);
    assert.equal(scad.collisionTriangles, reference.rayTriangles);
    assert.equal(scad.collisionMeshes, reference.collisionMeshes);
    assert.equal(scad.collisionInstances, reference.collisionInstances);
    assert.deepEqual(scad.bounds, reference.bounds);
    assert.equal(scad.anchors, Object.values(reference.anchorCounts).reduce((sum, count) => sum + count, 0));
    // Item by item, not just the count: type, label, profile, and position to 0.1 mm.
    const bakedAnchors = JSON.parse(await readFile(resolve(baked, map.files["anchors.json"].path), "utf8")).anchors;
    const generatedAnchors = await page.evaluate(() => window.nextdayzReviewAnchors());
    const anchorKey = anchor => `${anchor.type}|${anchor.label ?? ""}|${anchor.profile ?? ""}|${anchor.position.map(value => value.toFixed(4)).join(",")}`;
    assert.deepEqual(generatedAnchors.map(anchorKey).sort(), bakedAnchors.map(anchorKey).sort());
    assert.equal(scad.terrainCount, 1);
    assert.equal(scad.warningCount, 0);
    const scadRequests = requests.filter(request => request.path.startsWith("/scad-probe/"));
    assert(scadRequests.every(request => request.status === 200));
    assert.equal(scadRequests.reduce((sum, request) => sum + request.bytes, 0), scad.evaluatorBytes + scad.sourceBytes);
    assert(!requests.some(request => request.path.startsWith(`/game-assets/${map.directory}/`)));

    await page.evaluate(() => window.nextdayzLightPreview(540, 0));
    await capture();
    await page.keyboard.down("w");
    await wait(spawn => {
        const { frame } = window.nextdayzReview();
        return frame.pose?.grounded && Math.hypot(frame.feet[0] - spawn[0], frame.feet[2] - spawn[2]) > 1.2;
    }, frame.feet);
    await page.keyboard.up("w");
    await release();
    const moved = (await read()).frame.feet;

    await place([-60, 40, 269], Math.PI);
    await wait(() => {
        const { frame } = window.nextdayzReview();
        return Math.hypot(frame.feet[0] + 60, frame.feet[2] - 269) < 0.5 && frame.pose?.grounded;
    });
    await page.screenshot({ path: resolve(evidence, "browser-gate.png") });

    await place([-70.4, 40, 262.5], Math.PI);
    await wait(() => Math.abs(window.nextdayzReview().frame.feet[2] - 262.5) < 0.2);
    await capture();
    await page.keyboard.down("w");
    await wait(() => window.nextdayzReview().frame.feet[2] < 261.2);
    await page.waitForTimeout(1500);
    await page.keyboard.up("w");
    const wall = (await read()).frame.feet;
    assert(wall[2] > 259.6 && wall[2] < 261.1, `guardhouse wall blocked the capsule at ${wall}`);
    await release();

    await place([200, 40, 150], Math.PI);
    await wait(() => window.nextdayzReview().frame.nearExtraction);
    await page.locator("#map-button").click();
    await page.locator("#map-canvas").waitFor();
    await page.screenshot({ path: resolve(evidence, "browser-map.png") });
    await page.locator("#close-map").click();
    await capture();
    await page.keyboard.press("g");
    await wait(() => window.nextdayzReview().frame.extractSeconds > 0);
    await page.locator("#profile").waitFor({ timeout: 40000 });
    const { profile } = await read();
    assert.equal(profile.extractions, 1);
    assert.deepEqual(errors, []);
    // Design D2: an evaluation that fails leaves the player on the baked map, not on an error.
    const fallback = await browser.newContext({ viewport: { width: 1390, height: 900 } });
    const other = await openPage(fallback);
    let fallbackReport;
    try {
        const otherErrors = [];
        const baked = [];
        other.on("pageerror", error => otherErrors.push(error.message));
        other.on("response", response => {
            if (new URL(response.url()).pathname.startsWith(`/game-assets/${map.directory}/`)) baked.push(response.url());
        });
        await other.route(/\/scad-probe\/ndscad\.wasm/, route => route.abort());
        await other.goto(site.href);
        await other.locator("#name").fill(`SCAD Fallback ${Date.now()}`);
        await other.locator("#login-button").click();
        await other.locator('input[name=map][value="training_ground"]').check();
        const fallbackBegan = Date.now();
        await other.locator("#deploy").click();
        await other.locator("#field").waitFor({ timeout: 120000 });
        await other.waitForFunction(() => window.nextdayzReview().frame.snapshots >= 4 && window.nextdayzReview().frame.lootReady,
            undefined, { timeout: 30000 });
        const state = await other.evaluate(() => window.nextdayzReview());
        assert(state.scadFallback, "the blocked evaluator was reported");
        assert.equal(state.scad, undefined, "the level came from the baked assets");
        assert(baked.length > 0, "the baked map was downloaded");
        assert.deepEqual(otherErrors, []);
        fallbackReport = { reason: state.scadFallback, joinMs: Date.now() - fallbackBegan, bakedFiles: baked.length };
        await other.locator("#leave").click();
        await other.locator("#profile").waitFor({ timeout: 30000 });
    } catch (error) {
        await other.screenshot({ path: resolve(evidence, "fallback-failure.png") });
        throw error;
    } finally {
        await fallback.close();
    }

    const report = { browser: browser.version(), name, firstJoinMs, scad, spawn: frame.feet, moved, wall,
        extractions: profile.extractions, fallback: fallbackReport, requests, errors };
    await writeFile(resolve(evidence, "browser.json"), JSON.stringify(report, null, 2) + "\n");
    console.log(JSON.stringify({ firstJoinMs, scad, wall, extractions: profile.extractions, fallback: fallbackReport }));
} finally {
    await browser.close();
}
