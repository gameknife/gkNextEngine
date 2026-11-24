// Leave, return, extract: a browser survivor picks up a rifle, leaves the map, comes back
// with it and extracts it into storage. Also checks the shared client rules (design 5.3):
// a picked-up weapon waits in the bag, and a survivor returning with one has it drawn.
import assert from "node:assert/strict";
import { writeFile } from "node:fs/promises";
import { resolve } from "node:path";
import { equip, evidence as evidenceDir, launchBrowser, openPage, site } from "./harness.mjs";

const evidence = await evidenceDir("out/web/nextdayz/e2e-evidence");
const map = "training_ground";
const browser = await launchBrowser();
const result = {};

try {
    const page = await openPage(browser, { viewport: { width: 1280, height: 720 } });
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
    const deploy = async () => {
        await page.locator(`input[name=map][value=${map}]`).check();
        await page.locator("#deploy").click();
        await page.locator("#field").waitFor({ timeout: 120000 });
        await wait(() => window.nextdayzReview().frame.lootReady && window.nextdayzReview().frame.snapshots > 0);
    };

    await page.goto(site);
    await page.locator("#name").fill(`E2E Return ${Date.now()}`);
    await page.locator("#login-button").click();
    await deploy();

    // The range's rifle, beside the entrance.
    await place([57.875, 11.45, 58.45], Math.PI);
    await capture();
    await page.locator("#field-hint").waitFor();
    await page.keyboard.press("e");
    await wait(() => window.nextdayzReview().cargo.items.some(item => item.id === "ak"));
    await release();
    const picked = await review();
    assert.equal(picked.frame.combat.weapon, "", "a picked-up weapon is not drawn by itself");
    assert(picked.cargo.items.some(item => item.id === "ak" && item.weaponSlot < 0));
    await equip(page, "ak");
    assert.equal((await review()).frame.combat.weapon, "AK-74");
    result.pickup = { drawnByItself: false, equipped: true };

    // Leave: the Master keeps the survivor in the field with what it carried.
    await page.locator("#leave").click();
    await page.locator("#profile").waitFor({ timeout: 30000 });
    await wait(() => window.nextdayzReview().profile.character?.storage.some(item => item.id === "ak") &&
        !window.nextdayzReview().profile.playing, undefined, 60000);
    result.leave = { keptInField: true };

    // Return: the same survivor, gear restored, every weapon in its usual slot.
    await deploy();
    const returned = await review();
    assert(returned.cargo.items.some(item => item.id === "ak" && item.weaponSlot === 0), "the rifle is back in the primary slot");
    assert.equal(returned.frame.combat.weapon, "AK-74");
    result.return = { rifleInPrimarySlot: true };

    // Extract at the helipad: what the survivor carries goes into storage.
    await place([200, 11.4, 150], Math.PI);
    await wait(() => window.nextdayzReview().frame.nearExtraction);
    await capture();
    // Holding the point can be broken off (an infected wanders over); start it again, as a player would.
    result.extractAttempts = 0;
    for (;;) {
        ++result.extractAttempts;
        await page.keyboard.press("g");
        await wait(() => window.nextdayzReview().frame.extractSeconds > 0);
        const outcome = await page.waitForFunction(() => {
            const { frame } = window.nextdayzReview();
            return frame.phase === "profile" ? "done" : frame.extractBroken ? "broken" : null;
        }, null, { timeout: 40000 }).then(handle => handle.jsonValue());
        if (outcome === "done") break;
        result.extractBroken = { message: (await review()).frame.message, health: (await review()).frame.body.health };
        assert(result.extractAttempts < 3, `extraction broken off three times: ${JSON.stringify(result.extractBroken)}`);
        await page.waitForTimeout(1200);
    }
    await page.locator("#profile").waitFor({ timeout: 40000 });
    await wait(() => window.nextdayzReview().profile.extractions === 1, undefined, 20000);
    const profile = (await review()).profile;
    assert(!profile.character, "no survivor left in the field");
    assert(profile.storage.some(item => item.id === "ak"), "the rifle reached storage");
    result.extract = { extractions: profile.extractions, rifleInStorage: true };
    await page.screenshot({ path: resolve(evidence, "return-extracted.png") });

    assert.deepEqual(errors, []);
    await writeFile(resolve(evidence, "return.json"), JSON.stringify(result, null, 2));
    console.log(JSON.stringify(result));
} finally {
    await browser.close();
}
