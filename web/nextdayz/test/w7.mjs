import assert from "node:assert/strict";
import { readFile, writeFile } from "node:fs/promises";
import { resolve } from "node:path";
import { equip, evidence as evidenceDir, launchBrowser, openPage, repo, site } from "./harness.mjs";

const evidence = await evidenceDir("out/web/nextdayz/w7-evidence");
const map = "training_ground";
const manifestRoot = resolve(repo, "out/web/nextdayz/assets");
const manifest = JSON.parse(await readFile(resolve(manifestRoot, "manifest.json"), "utf8"));
const { anchors } = JSON.parse(await readFile(resolve(manifestRoot, manifest.maps[map].files["anchors.json"].path), "utf8"));
const browser = await launchBrowser();
const result = {};

try {
    const page = await openPage(browser, { viewport: { width: 1280, height: 720 } });
    const errors = [];
    page.on("pageerror", error => errors.push(error.message));
    const read = () => page.evaluate(() => window.nextdayzReview());
    const wait = (probe, args, timeout = 20000) => page.waitForFunction(probe, args, { timeout });
    const place = (position, yaw = 0) => page.evaluate(data => window.nextdayzReviewPlace(data.position, data.yaw),
        { position, yaw });
    const capture = async () => {
        await page.locator("#capture").click();
        await wait(() => Boolean(document.pointerLockElement));
        await page.waitForTimeout(150);
    };
    const release = async () => {
        await page.evaluate(() => document.exitPointerLock());
        await wait(() => !document.pointerLockElement);
    };
    const take = async (id, [dx, dz], candidate = 0) => {
        const [x, y, z] = anchors.filter(anchor => anchor.item === id)[candidate].position;
        await place([x + dx, y + .58, z + dz], Math.PI);
        await capture();
        await page.locator("#field-hint").waitFor();
        await page.keyboard.press("e");
        await wait(expected => window.nextdayzReview().cargo.items.some(item => item.id === expected), id);
        await release();
    };
    const press = async key => {
        await page.keyboard.press(key);
        await page.waitForTimeout(200);
    };

    await page.goto(site);
    await page.locator("#name").fill(process.env.ND_TEST_RESUME_NAME ?? `W7 Survivor ${Date.now()}`);
    await page.locator("#login-button").click();
    await page.locator(`input[name=map][value=${map}]`).check();
    await page.locator("#deploy").click();
    await page.locator("#field").waitFor({ timeout: 120000 });
    await wait(() => window.nextdayzReview().frame.lootReady);
    await page.evaluate(() => window.nextdayzLightPreview(540, 0));
    if (!(await read()).cargo.items.some(item => item.id === "ak")) await take("ak", [0, 1], 1);
    if (!(await read()).cargo.items.some(item => item.id === "ak" && item.weaponSlot >= 0)) await equip(page, "ak");
    if (!(await read()).cargo.items.some(item => item.id === "ammo_545")) await take("ammo_545", [0, 1], 1);
    console.log("joined and armed");

    await page.locator("#pack-button").click();
    const inventory = () => page.locator("#items");
    await inventory().locator("img").first().waitFor({ state: "visible" });
    await wait(() => Array.from(document.querySelectorAll("#items img")).some(img => img.complete && img.naturalWidth > 0));
    const cargo = (await read()).cargo;
    const bandage = cargo.items.find(item => item.id === "bandage");
    const source = inventory().getByRole("button", { name: /Bandage|绷带/i }).first();
    const pants = cargo.containers.find(box => box.id !== bandage.container && !box.weaponOnly && !box.used);
    assert(pants, "expected empty personal cargo");
    const cell = (box, x, y) => inventory().locator(`[data-container="${box}"] [data-x="${x}"][data-y="${y}"]`);
    await source.dragTo(cell(pants.id, 0, 0));
    await wait(params => window.nextdayzReview().cargo.items.some(row => row.instance === params.id &&
        row.container === params.container && row.x === 0 && row.y === 0), { id: bandage.instance, container: pants.id });

    const bottle = cargo.items.find(item => item.id === "water_bottle_empty");
    const empty = inventory().getByRole("button", { name: /Empty Bottle|空水瓶/i }).first();
    await empty.click();
    if (!bottle.rotated) await page.keyboard.press("r");
    await cell(pants.id, 0, 1).click();
    await wait(params => window.nextdayzReview().cargo.items.some(row => row.instance === params.id && row.rotated &&
        row.container === params.container && row.x === 0 && row.y === 1), { id: bottle.instance, container: pants.id });
    await cell(pants.id, 1, 0).click();
    await page.locator("#pack-notice").waitFor();
    const placed = (await read()).cargo.items.find(row => row.instance === bottle.instance);
    assert.deepEqual([placed.container, placed.x, placed.y, placed.rotated], [pants.id, 0, 1, true]);
    result.grid = { moved: bandage.instance, rotated: bottle.instance, rejectedOverflow: true };
    await page.screenshot({ path: resolve(evidence, "inventory-720p.png") });
    await page.locator("#close-pack").click();

    if (!(await read()).cargo.items.some(item => item.id === "optic_red_dot")) await take("optic_red_dot", [0, .85]);
    await page.locator("#pack-button").click();
    await inventory().getByRole("button", { name: /Red Dot Sight|红点/i }).first().click();
    await inventory().getByRole("button", { name: /安装到 AK-74|Mount on AK-74/ }).click();
    await wait(() => window.nextdayzReview().cargo.items.some(item => item.id === "ak" && item.attachments[0] === 1));
    await inventory().getByRole("button", { name: "AK-74", exact: true }).first().click();
    await inventory().getByRole("button", { name: /卸下 .*红点|卸下 Red Dot|Detach Red Dot/i }).click();
    await wait(() => window.nextdayzReview().cargo.items.some(item => item.id === "optic_red_dot"));
    result.attachments = { mounted: "optic_red_dot", detached: true };
    await page.locator("#close-pack").click();

    await page.locator("#map-button").click();
    await page.locator("#map-canvas").waitFor();
    await page.screenshot({ path: resolve(evidence, "field-map-720p.png") });
    await page.locator("#close-map").click();
    await capture();
    let ammo = (await read()).frame.combat;
    if (ammo.magazine < 5) {
        await press("r");
        await wait(() => window.nextdayzReview().frame.combat.magazine >= 5);
        ammo = (await read()).frame.combat;
    }
    await page.mouse.click(640, 360);
    await wait(initial => window.nextdayzReview().frame.combat.magazine < initial, ammo.magazine);
    await press("r");
    await wait(() => window.nextdayzReview().frame.combat.action === 0);
    const restored = (await read()).frame.combat;
    assert(restored.magazine > 0 && restored.reserve < ammo.reserve);
    result.weapon = { magazine: restored.magazine, reserve: restored.reserve, shots: restored.shots };
    await release();

    await page.locator("#settings-button").click();
    await page.locator("#settings-content").getByLabel("语言").selectOption("en");
    assert.equal(await page.locator("html").getAttribute("lang"), "en");
    await page.locator("#settings-content").getByRole("button", { name: "Reload", exact: true }).click();
    await page.keyboard.press("k");
    await page.locator("#settings-content").getByRole("button", { name: "Reload", exact: true }).waitFor();
    assert.equal(await page.locator("#settings-content").getByRole("button", { name: "Reload", exact: true }).textContent(), "K");
    await page.locator("#settings-content").getByLabel("Volume").fill("0.38");
    await page.locator("#close-settings").click();
    await page.setViewportSize({ width: 3840, height: 2160 });
    await page.locator("#map-button").click();
    await page.waitForTimeout(300);
    assert.equal(await page.locator("#map-panel").evaluate(panel => {
        const rect = panel.getBoundingClientRect();
        return rect.left >= 0 && rect.right <= innerWidth && rect.top >= 0 && rect.bottom <= innerHeight;
    }), true);
    await page.screenshot({ path: resolve(evidence, "field-map-4k.png") });
    await page.locator("#close-map").click();
    await page.locator("#leave").click();
    await page.locator("#profile").waitFor();
    await page.reload();
    await page.locator("#login-button").waitFor();
    await page.locator("#settings-button").click();
    await page.locator("#settings-content").getByLabel("Language").waitFor();
    assert.equal(await page.locator("#settings-content").getByLabel("Language").inputValue(), "en");
    assert.equal(await page.locator("#settings-content").getByLabel("Volume").inputValue(), "0.38");
    assert.equal(await page.locator("#settings-content").getByRole("button", { name: "Reload", exact: true }).textContent(), "K");
    result.settings = { language: "en", volume: .38, reload: "K", viewports: [[1280, 720], [3840, 2160]] };
    assert.deepEqual(errors, []);
    await writeFile(resolve(evidence, "ui.json"), JSON.stringify(result, null, 2));
    console.log(JSON.stringify(result));
} finally {
    await browser.close();
}
