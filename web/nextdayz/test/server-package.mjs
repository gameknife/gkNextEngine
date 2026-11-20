import assert from "node:assert/strict";
import { resolve } from "node:path";
import { chromium } from "playwright";

const [site, evidence] = process.argv.slice(2);
const account = "package-browser";
const password = process.env.ND_PACKAGE_TEST_PASSWORD;
assert(site && evidence && password, "Usage: ND_PACKAGE_TEST_PASSWORD=... node test/server-package.mjs URL EVIDENCE_DIR");
const browser = await chromium.launch({
    channel: process.env.ND_TEST_BROWSER || undefined,
    args: ["--enable-webgl", "--ignore-gpu-blocklist", "--use-gl=angle", "--use-angle=swiftshader", "--enable-unsafe-swiftshader"],
});
const timeout = 120_000;
const page = await browser.newPage({ viewport: { width: 1280, height: 720 } });
const errors = [];
page.on("pageerror", error => errors.push(error.message));

async function waitText(selector, pattern) {
    await page.waitForFunction(
        ({ selector, source }) => new RegExp(source).test(document.querySelector(selector)?.textContent ?? ""),
        { selector, source: pattern.source }, { timeout },
    );
}

async function login(pass) {
    await page.locator("#name").fill(account);
    await page.locator("#password").fill(pass);
    const [response] = await Promise.all([
        page.waitForResponse(result => result.url().endsWith("/api/login")),
        page.locator("#login-button").click(),
    ]);
    return response.status();
}
async function deploy() {
    await page.locator("#deploy").click({ timeout });
    await page.locator("#field").waitFor({ timeout });
    await waitText("#sync", /物资 \d+|Loot \d+/);
}

try {
    await page.goto(site);
    await page.locator("#password").waitFor({ timeout });
    assert.equal(await login(password), 200);
    await page.locator("#profile").waitFor({ timeout });

    // Make this page stale for one deploy without changing its auth or the DS.
    const deployURL = "**/api/deploy";
    await page.route(deployURL, route => {
        const body = route.request().postDataJSON();
        assert.match(body.webBuild, /^[0-9a-f]{64}$/);
        return route.continue({ postData: { ...body, webBuild: "stale" } });
    });
    await page.locator("#deploy").click();
    await waitText("#notice", /刷新页面|Refresh the page/);
    await page.unroute(deployURL);

    await deploy();
    await page.locator("#pack-button").click();
    const pack = page.locator("#items");
    const bottle = pack.getByRole("button", { name: /空水瓶|Empty Bottle/ });
    await bottle.click({ timeout });
    await pack.getByRole("button", { name: /^(丢弃|Drop)$/ }).click();
    const take = pack.getByRole("button", { name: /^(拾取|Take)$/ });
    await take.click({ timeout });
    await bottle.waitFor({ timeout });
    await page.locator("#close-pack").click();

    await page.locator("#leave").click();
    await waitText("#deploy", /回到野外|Resume expedition/);
    await deploy();
    await page.locator("#capture").click();
    await page.waitForFunction(() => Boolean(document.pointerLockElement));
    await waitText("#field-hint", /开始撤离|Extract/);
    await page.keyboard.press("g");
    await waitText("#field-hint", /撤离中|Extracting/);
    await page.locator("#profile").waitFor({ timeout });
    await waitText("#profile-activity", /撤离 1|Extractions 1/);
    await waitText("#storage", /空水瓶|Empty Bottle/);

    await page.locator("#logout").click();
    assert.equal(await login("wrong-package-password"), 401);
    assert.equal(await login(password), 200);
    await waitText("#storage", /空水瓶|Empty Bottle/);
    assert.deepEqual(errors, []);
    await page.screenshot({ path: resolve(evidence, "browser-extraction.png") });
    console.log("PASS production browser: password, version check, WebRTC, loot, leave/resume, extraction, stored item");
} catch (error) {
    await page.screenshot({ path: resolve(evidence, "browser-failure.png") }).catch(() => {});
    console.error(await page.locator("#notice").textContent().catch(() => ""));
    throw error;
} finally {
    await browser.close();
}
