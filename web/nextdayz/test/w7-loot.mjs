import assert from "node:assert/strict";
import { chromium } from "playwright";
import { mkdir, writeFile } from "node:fs/promises";
import { resolve } from "node:path";
import { fileURLToPath } from "node:url";

const repo = fileURLToPath(new URL("../../..", import.meta.url));
const evidence = resolve(repo, "out/web/nextdayz/w7-evidence");
const site = process.env.ND_TEST_SITE ?? "http://127.0.0.1:4182";
const browser = await chromium.launch({
    channel: "chrome", args: ["--enable-webgl", "--ignore-gpu-blocklist", "--disable-background-timer-throttling"],
});
const names = [`W7 Race A ${Date.now()}`, `W7 Race B ${Date.now()}`];
const review = page => page.evaluate(() => window.nextdayzReview());
const wait = (page, predicate, arg, timeout = 20000) => page.waitForFunction(predicate, arg, { timeout });
const place = (page, position, yaw) => page.evaluate(data => window.nextdayzReviewPlace(data.position, data.yaw), { position, yaw });

try {
    await mkdir(evidence, { recursive: true });
    const pages = await Promise.all(names.map(() => browser.newPage({ viewport: { width: 1280, height: 720 } })));
    await Promise.all(pages.map(async (page, index) => {
        await page.goto(site);
        await page.locator("#name").fill(names[index]);
        await page.locator("#login-button").click();
        await page.locator("input[name=map][value=training_ground]").check();
        await page.locator("#deploy").click();
        await page.locator("#field").waitFor({ timeout: 120000 });
        await wait(page, () => window.nextdayzReview().frame.lootReady);
        await place(page, [-166.82, 11.57, 99.79], 0);
        await wait(page, () => Math.abs(window.nextdayzReview().frame.feet[0] + 166.82) < 1);
        await page.locator("#pack-button").click();
        await page.locator("#items").getByRole("button", { name: "拾取", exact: true }).waitFor();
    }));
    const [left, right] = await Promise.all(pages.map(page => page.locator(".pack-equipment section").last().innerText()));
    assert.equal(left, right, "both survivors target the same ground entry");

    await Promise.all(pages.map(page => page.locator("#items").getByRole("button", { name: "拾取", exact: true }).click()));
    await pages[0].waitForTimeout(1700);
    const after = await Promise.all(pages.map(review));
    const counts = after.map(result => result.frame.takes);
    assert.deepEqual([...counts].sort(), [0, 1], "exactly one survivor got the item");
    const winner = pages[counts.indexOf(1)];
    await winner.locator("#close-pack").click();
    await place(winner, [200, 11.4, 150], Math.PI);
    await wait(winner, () => window.nextdayzReview().frame.nearExtraction);
    await winner.locator("#capture").click();
    await wait(winner, () => Boolean(document.pointerLockElement));
    await winner.waitForTimeout(150);
    await winner.keyboard.press("g");
    await wait(winner, () => window.nextdayzReview().frame.extractSeconds > 0);
    await winner.screenshot({ path: resolve(evidence, "extraction-countdown.png") });
    await winner.locator("#profile").waitFor({ timeout: 30000 });
    const profile = (await review(winner)).profile;
    assert.equal(profile.extractions, 1);
    assert(profile.storage.length > 0);
    const result = { spot: left, takes: counts, extractions: profile.extractions, storageItems: profile.storage.length };
    await writeFile(resolve(evidence, "loot-extraction.json"), JSON.stringify(result, null, 2));
    console.log(JSON.stringify(result));
} finally {
    await browser.close();
}
