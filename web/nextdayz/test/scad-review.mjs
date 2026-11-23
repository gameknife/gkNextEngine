import assert from "node:assert/strict";
import { mkdir, writeFile } from "node:fs/promises";
import { resolve } from "node:path";
import { fileURLToPath } from "node:url";
import { chromium } from "playwright";

const site = new URL(process.env.ND_TEST_SITE ?? "http://127.0.0.1:4184");
const evidence = fileURLToPath(new URL("../../../out/web/nextdayz/scad-probe/evidence/", import.meta.url));
await mkdir(evidence, { recursive: true });
const browser = await chromium.launch({ channel: "chrome", args: ["--enable-unsafe-swiftshader"] });
const runs = [];

try {
    for (let i = 0; i < 3; i++) {
        const context = await browser.newContext({ viewport: { width: 1390, height: 960 } });
        try {
            const page = await context.newPage();
            const requests = [];
            const errors = [];
            page.on("pageerror", error => errors.push(error.message));
            page.on("response", response => {
                const url = new URL(response.url());
                if (url.origin !== site.origin) return;
                const headers = response.headers();
                requests.push({
                    path: url.pathname,
                    status: response.status(),
                    bytes: Number(headers["content-length"] ?? 0),
                    encoding: headers["content-encoding"] ?? "identity",
                });
            });

            const status = page.locator("#status");
            async function readMetrics() {
                await page.waitForSelector('#status[data-state="ready"], #status[data-state="error"]', { timeout: 45000 });
                assert.equal(await status.getAttribute("data-state"), "ready", await status.textContent());
                return JSON.parse(await status.getAttribute("data-metrics"));
            }

            await page.goto(new URL("/scad-review/", site).href);
            const metrics = await readMetrics();
            assert.equal(metrics.terrainCount, 1);
            assert.equal(metrics.warningCount, 0, metrics.warnings.join("\n"));
            assert(metrics.triangleCount > 0 && metrics.evaluatedTriangles > metrics.triangleCount);
            assert(metrics.evaluateMs > 0 && metrics.wasmMemoryBytes > 0);
            const totalBytes = files => files.reduce((sum, file) => sum + file.bytes, 0);
            const probe = requests.filter(request => request.path.startsWith("/scad-probe/"));
            const evaluator = probe.filter(request => /\/ndscad\.(wasm|mjs)$/.test(request.path));
            const sources = probe.filter(request => !evaluator.includes(request));
            assert.equal(evaluator.length, 2);
            assert.equal(totalBytes(evaluator), metrics.evaluatorBytes);
            assert.equal(totalBytes(sources), metrics.sourceBytes);
            assert(metrics.evaluatorBytes > 0 && metrics.sourceBytes > 0);
            assert(probe.every(request => request.status === 200));
            assert(probe.filter(request => !request.path.endsWith(".json")).every(request => request.encoding === "gzip"));
            assert(sources.some(request => request.path.endsWith("/terrain.hmap")));
            assert(sources.some(request => request.path.endsWith("/DroidSansFallback.ttf")));
            assert(!requests.some(request => /\.(glb|gltf|bin)$|\/game-assets\//.test(request.path)));

            const run = { metrics, requests: requests.slice(), errors };
            if (i === 0) {
                await page.screenshot({ path: resolve(evidence, "overview.png") });
                await page.getByRole("button", { name: "门牌特写" }).click();
                await page.screenshot({ path: resolve(evidence, "sign.png") });
                await page.getByRole("button", { name: "重新生成" }).click();
                const regenerated = await readMetrics();
                assert.equal(regenerated.triangleCount, metrics.triangleCount);
                assert.equal(regenerated.evaluatedTriangles, metrics.evaluatedTriangles);
                assert.equal(regenerated.warningCount, 0);
                run.regenerated = regenerated;
            }
            assert.deepEqual(errors, []);
            runs.push(run);
            console.log(`SCAD Worker run ${i + 1}`, metrics);
        } finally {
            await context.close();
        }
    }
    await writeFile(resolve(evidence, "browser.json"), JSON.stringify({ browser: browser.version(), runs }, null, 2) + "\n");
} finally {
    await browser.close();
}
