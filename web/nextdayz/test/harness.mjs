// Shared setup for the Playwright scenarios (w7*.mjs, e2e-return.mjs). `gnb web test --e2e`
// starts the services and passes their addresses; run by hand, the defaults below match the
// manual setup in web/nextdayz/README.md.
import { spawn } from "node:child_process";
import { mkdir } from "node:fs/promises";
import { resolve } from "node:path";
import { fileURLToPath } from "node:url";
import { chromium } from "playwright";

export const repo = fileURLToPath(new URL("../../..", import.meta.url));
export const site = process.env.ND_TEST_SITE ?? "http://127.0.0.1:4182";
export const master = process.env.ND_TEST_MASTER ?? "http://127.0.0.1:7797";
export const preset = process.env.ND_TEST_PRESET ??
    (process.platform === "win32" ? "windows" : process.platform === "darwin" ? "macos-arm64" : "linux");

// Screenshots and results; `gnb web test --e2e` points all scenarios at one run directory.
export async function evidence(fallback) {
    const dir = resolve(repo, process.env.ND_TEST_EVIDENCE ?? fallback);
    await mkdir(dir, { recursive: true });
    return dir;
}

// ND_TEST_BROWSER: "chrome" (an installed Chrome), "msedge", or "chromium" (Playwright's own,
// what CI installs). ND_TEST_SOFTWARE_GL=1 renders WebGL on the CPU for GPU-less runners.
export function launchBrowser(extraArgs = []) {
    const channel = process.env.ND_TEST_BROWSER ?? "chrome";
    const args = ["--enable-webgl", "--ignore-gpu-blocklist", ...extraArgs];
    if (process.env.ND_TEST_SOFTWARE_GL === "1") args.push("--use-angle=swiftshader", "--enable-unsafe-swiftshader");
    return chromium.launch({ ...(channel === "chromium" ? {} : { channel }), args });
}

// A page for a scripted survivor. Chrome on Windows reads raw mouse input while the pointer is
// locked, headless or not, so someone using the machine during a run turns the survivor and
// sends it past the wall or the target it was walking at. Scripts aim with
// nextdayzReviewPlace, so the page ignores mouse movement while it holds the lock.
export async function openPage(owner, options) {
    const page = await owner.newPage(options);
    await page.addInitScript(() => window.addEventListener("mousemove", event => {
        if (document.pointerLockElement) event.stopImmediatePropagation();
    }, true));
    return page;
}

// The Master's loopback admin API (what `gnb master status|pvp|stop` call).
export async function masterAdmin(path, body = {}) {
    const response = await fetch(new URL(path, master), {
        method: "POST", headers: { "Content-Type": "application/json" }, body: JSON.stringify(body),
    });
    if (!response.ok) throw Error(`${path}: ${response.status} ${await response.text()}`);
    return response.json();
}

// The repository's gnb entry, which rebuilds gnb when its sources changed.
export function spawnGnb(args, options = {}) {
    return process.platform === "win32"
        ? spawn("cmd", ["/c", resolve(repo, "gnb.bat"), ...args], { cwd: repo, ...options })
        : spawn(resolve(repo, "gnb.sh"), args, { cwd: repo, ...options });
}

// A picked-up weapon goes into the bag (as on desktop); this puts it in its weapon slot
// through the pack, the way a player would. The pointer must be free.
export async function equip(page, id) {
    await page.locator("#pack-button").click();
    await page.locator(`#items [data-item="${id}"]`).first().click();
    await page.locator(".item-detail").getByRole("button", { name: /^(装备|Equip)$/ }).click();
    await page.waitForFunction(item => window.nextdayzReview().cargo.items.some(row => row.id === item && row.weaponSlot >= 0),
        id, { timeout: 10000 });
    await page.locator("#close-pack").click();
}
