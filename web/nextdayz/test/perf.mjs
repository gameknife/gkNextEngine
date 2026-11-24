// W8-5 measurement: each browser, each graphics tier, the training ground with 48 infected
// in view (the scene the design's budget table uses). Needs the review site and a Master
// (gnb web test --e2e --scenario perf, or the manual setup in the README).
//   ND_PERF_BROWSERS  comma list of chrome, msedge, chromium, firefox, webkit (default: all five)
import { writeFile } from "node:fs/promises";
import { resolve } from "node:path";
import { chromium, firefox, webkit } from "playwright";
import { evidence as evidenceDir, openPage, site } from "./harness.mjs";

const evidence = await evidenceDir("out/web/nextdayz/perf-evidence");
const browsers = (process.env.ND_PERF_BROWSERS ?? "chrome,msedge,chromium,firefox,webkit").split(",").filter(Boolean);
const tiers = ["low", "balanced", "shadows"];
const viewport = { width: 1920, height: 1080 };
const percentile = (values, p) => {
    const sorted = [...values].sort((a, b) => a - b);
    return sorted.length ? Math.round(sorted[Math.min(sorted.length - 1, Math.floor(p * sorted.length))] * 100) / 100 : null;
};

function launch(name) {
    const args = ["--enable-webgl", "--ignore-gpu-blocklist"];
    if (name === "firefox") return firefox.launch();
    if (name === "webkit") return webkit.launch();
    return chromium.launch({ ...(name === "chromium" ? {} : { channel: name }), args });
}

async function measure(name) {
    let browser;
    try {
        browser = await launch(name);
    } catch (error) {
        // A browser this machine lacks is a gap in the table, not a failed measurement.
        return { browser: name, skipped: error.message.split("\n")[0] };
    }
    const report = { browser: name, version: browser.version() };
    let page;
    try {
        page = await openPage(browser, { viewport });
        const errors = [];
        page.on("pageerror", error => errors.push(error.message));
        const review = () => page.evaluate(() => window.nextdayzReview());
        const wait = (probe, args, timeout = 30000) => page.waitForFunction(probe, args, { timeout });
        const started = Date.now();
        await page.goto(site);
        // The map server is reached over WebRTC. Playwright's Windows WebKit has none (it is
        // not Safari); such a browser cannot play, so it is a gap, not a slow measurement.
        if (!(await page.evaluate(() => typeof RTCPeerConnection === "function"))) {
            return { ...report, skipped: "no WebRTC (RTCPeerConnection) in this browser build" };
        }
        await page.locator("#name").fill(`Perf ${name} ${Date.now()}`);
        await page.locator("#login-button").click();
        await page.locator("input[name=map][value=training_ground]").check();
        await page.locator("#deploy").click();
        await page.locator("#field").waitFor({ timeout: 180000 });
        report.loadToFieldMs = Date.now() - started;
        await wait(() => window.nextdayzReview().frame.snapshots > 4);
        await page.evaluate(() => window.nextdayzReviewPlace([-227, 12, 31], Math.PI));
        await wait(() => Math.abs(window.nextdayzReview().frame.feet[0] + 227) < 1);
        await page.evaluate(() => window.nextdayzReviewCrowd(48));
        await wait(() => window.nextdayzReview().frame.zombies.filter(zombie => zombie.matrixOffset >= 0).length === 48);
        report.tiers = {};
        for (const tier of tiers) {
            await page.selectOption("#quality", tier);
            await page.waitForTimeout(1500);
            const intervals = await page.evaluate(() => new Promise(done => {
                const durations = [];
                let last = 0;
                const sample = timestamp => {
                    if (last) durations.push(timestamp - last);
                    last = timestamp;
                    if (durations.length === 240) done(durations);
                    else requestAnimationFrame(sample);
                };
                requestAnimationFrame(sample);
            }));
            const state = await review();
            report.tiers[tier] = {
                frameMedianMs: percentile(intervals, .5), frameP95Ms: percentile(intervals, .95),
                workMedianMs: percentile(state.workMs, .5), workP95Ms: percentile(state.workMs, .95),
                drawCalls: state.drawCalls, triangles: state.triangles,
            };
            await page.screenshot({ path: resolve(evidence, `${name}-${tier}.jpg`), quality: 70, type: "jpeg" });
        }
        const state = await review();
        report.wasmMiB = Math.round(state.wasmBytes / 1048576 * 10) / 10;
        report.jsHeapMiB = await page.evaluate(() => performance.memory ? Math.round(performance.memory.usedJSHeapSize / 104857.6) / 10 : null);
        report.errors = errors;
        await page.evaluate(() => window.nextdayzReviewCrowd(0));
        await page.locator("#leave").click();
        await page.locator("#profile").waitFor({ timeout: 30000 });
    } catch (error) {
        report.failure = error.message.split("\n")[0];
        await page?.screenshot({ path: resolve(evidence, `${name}-failure.jpg`), quality: 70, type: "jpeg" }).catch(() => {});
    } finally {
        await browser.close();
    }
    return report;
}

const results = [];
for (const name of browsers) {
    const report = await measure(name);
    results.push(report);
    console.log(JSON.stringify(report));
}
await writeFile(resolve(evidence, "perf.json"), JSON.stringify({ viewport, infected: 48, results }, null, 2));
// A gap is reported, not failed; a run that measured nothing is not a pass.
if (results.some(report => report.failure) || !results.some(report => report.tiers)) process.exitCode = 1;
