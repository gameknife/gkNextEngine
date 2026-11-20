import assert from "node:assert/strict";
import { chromium } from "playwright";
import { mkdir, writeFile } from "node:fs/promises";
import { fileURLToPath } from "node:url";
import { resolve } from "node:path";

const repo = fileURLToPath(new URL("../../..", import.meta.url));
const evidence = resolve(repo, "out/web/nextdayz/w7-evidence");
const site = process.env.ND_TEST_SITE ?? "http://127.0.0.1:4182";
const browser = await chromium.launch({ channel: "chrome", args: ["--enable-webgl", "--ignore-gpu-blocklist"] });

try {
    await mkdir(evidence, { recursive: true });
    const page = await browser.newPage({ viewport: { width: 1280, height: 720 } });
    const review = () => page.evaluate(() => window.nextdayzReview());
    const deploy = async () => {
        await page.locator("input[name=map][value=training_ground]").check();
        await page.locator("#deploy").click();
        await page.locator("#field").waitFor({ timeout: 120000 });
    };
    await page.goto(site);
    await page.locator("#name").fill(`W7 Respawn ${Date.now()}`);
    await page.locator("#login-button").click();
    await deploy();
    await page.evaluate(() => window.nextdayzLightPreview(540, 0));
    await page.locator("#pen").click();
    await page.waitForFunction(() => window.nextdayzReview().frame.zombies.some(zombie => zombie.state !== 7),
        null, { timeout: 40000 });
    const { frame } = await review();
    const infected = frame.zombies.find(zombie => zombie.state !== 7);
    await page.evaluate(p => window.nextdayzReviewPlace([p[0], p[1] + 1.1, p[2] + 1.5], Math.PI), infected.position);
    await page.locator("#death-panel").waitFor({ timeout: 50000 });
    const death = (await review()).frame;
    assert.equal(death.body.health, 0);
    await page.screenshot({ path: resolve(evidence, "death-panel.png") });
    await page.locator("#respawn").click();
    await page.locator("#profile").waitFor();
    assert.equal((await review()).profile.deaths, 1);
    await deploy();
    const next = (await review()).frame;
    assert.equal(next.alive, true);
    assert.equal(next.body.health, 100);
    await page.locator("#leave").click();
    await page.locator("#profile").waitFor();
    const result = { deadId: death.id, cause: death.body.cause, newId: next.id, health: next.body.health, deaths: 1 };
    await writeFile(resolve(evidence, "respawn.json"), JSON.stringify(result, null, 2));
    console.log(JSON.stringify(result));
} finally {
    await browser.close();
}
