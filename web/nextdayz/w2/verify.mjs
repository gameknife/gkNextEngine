import assert from "node:assert/strict";
import { mkdir, readFile, writeFile } from "node:fs/promises";
import { dirname, join, resolve } from "node:path";
import { fileURLToPath } from "node:url";
import { chromium } from "../w0/node_modules/playwright/index.mjs";
import { MeshoptDecoder } from "../w0/node_modules/three/examples/jsm/libs/meshopt_decoder.module.js";

const root = fileURLToPath(new URL("../../..", import.meta.url));
const directory = resolve(root, "out/web/nextdayz/assets");
const evidence = resolve(root, "out/web/nextdayz/w2-evidence");
const manifest = JSON.parse(await readFile(join(directory, "manifest.json"), "utf8"));
const url = process.env.ND_W2_URL ?? "http://127.0.0.1:4178";
await mkdir(evidence, { recursive: true });
await MeshoptDecoder.ready;

function reader(data, magic) {
    assert.equal(data.toString("ascii", 0, 4), magic);
    let at = 4;
    return {
        count: () => { const value = data.readUInt32LE(at); at += 4; return value; },
        int: () => { const value = data.readInt32LE(at); at += 4; return value; },
        byte: () => data[at++],
        float: () => { const value = data.readFloatLE(at); at += 4; return value; },
        bytes: n => { assert(at + n <= data.length); const value = data.subarray(at, at + n); at += n; return value; },
        done: () => { assert.equal(at, data.length); },
    };
}

async function checkMapBinaries(bundle, stats) {
    const collision = reader(await readFile(join(directory, bundle.files["collision.bin"].path)), "NDC1");
    const meshes = collision.count();
    const instances = collision.count();
    assert.equal(meshes, stats.collisionMeshes);
    assert.equal(instances, stats.collisionInstances);
    const triangles = [];
    for (let mesh = 0; mesh < meshes; mesh++) {
        const vertices = collision.count();
        const indices = collision.count();
        const vertexBytes = collision.count();
        const indexBytes = collision.count();
        const positions = new Float32Array(vertices * 3);
        const decoded = new Uint32Array(indices);
        MeshoptDecoder.decodeVertexBuffer(new Uint8Array(positions.buffer), vertices, 12, collision.bytes(vertexBytes));
        MeshoptDecoder.decodeIndexBuffer(new Uint8Array(decoded.buffer), indices, 4, collision.bytes(indexBytes));
        assert(positions.every(Number.isFinite));
        assert(decoded.every(index => index < vertices));
        triangles.push(indices / 3);
    }
    let ray = 0, bodies = 0;
    for (let i = 0; i < instances; i++) {
        const mesh = collision.count();
        collision.count(); // source node
        collision.count(); // target node, optional
        const flags = collision.count();
        assert(mesh < meshes && flags > 0 && flags < 7);
        if (flags & 1) ray += triangles[mesh];
        if (flags & 6) bodies += triangles[mesh];
        for (let j = 0; j < 26; j++) assert(Number.isFinite(collision.float()));
    }
    collision.done();
    assert.equal(ray, stats.rayTriangles);
    assert.equal(bodies, stats.bodyTriangles);

    const terrain = reader(await readFile(join(directory, bundle.files["terrain.bin"].path)), "NDT1");
    assert.equal(terrain.count(), stats.terrainGrids);
    for (let i = 0; i < stats.terrainGrids; i++) {
        const x = terrain.count(), y = terrain.count();
        assert(x > 0 && y > 0);
        terrain.bytes(3 * 8 + 32 * 8 + (x + 1) * (y + 1) * 3 * 8 + x * y * 11);
    }
    terrain.done();
    return { ray, bodies, terrainGrids: stats.terrainGrids };
}

async function checkRigs(browser) {
    const results = {};
    const page = await browser.newPage();
    await page.goto(url);
    for (const [name, bundle] of Object.entries(manifest.rigs ?? {})) {
        const entries = bundle.files;
        const stats = JSON.parse(await readFile(join(directory, entries["bake.json"].path), "utf8"));
        const rig = reader(await readFile(join(directory, entries[`${name}.rig.bin`].path)), "NDR1");
        const bones = rig.count();
        const clips = rig.count();
        const string = () => rig.bytes(rig.count()).toString("utf8");
        assert.equal(bones, stats.bones);
        assert.equal(clips, stats.clips.length);
        for (let i = 0; i < bones; i++) {
            assert(string());
            const parent = rig.int();
            assert(parent >= -1 && parent < i);
            for (let j = 0; j < 10; j++) assert(Number.isFinite(rig.float()));
        }
        for (const clip of stats.clips) {
            assert.equal(string(), clip.name);
            assert.equal(rig.float(), clip.duration);
            assert(rig.byte() <= 1);
            assert.equal(rig.count(), clip.channels);
            for (let i = 0; i < clip.channels; i++) {
                const bone = rig.int();
                assert(bone >= 0 && bone < bones);
                for (const stride of [4, 5, 4]) {
                    const keys = rig.count();
                    rig.bytes(keys * stride * 4);
                }
            }
        }
        rig.done();
        const geometry = await page.evaluate(async asset => {
            const [{ GLTFLoader }, { MeshoptDecoder }] = await Promise.all([
                import("/three/examples/jsm/loaders/GLTFLoader.js"),
                import("/three/examples/jsm/libs/meshopt_decoder.module.js"),
            ]);
            const loaded = await new GLTFLoader().setMeshoptDecoder(MeshoptDecoder).loadAsync(asset);
            return loaded.scene.children.map(node => [node.userData.part, node.userData.bone]);
        }, "/assets/" + entries[`${name}.glb`].path);
        assert.equal(geometry.length, stats.parts);
        for (const [index, [part, bone]] of geometry.entries()) {
            assert.equal(part, index);
            assert(bone >= 0 && bone < bones);
        }
        results[name] = { bones, parts: stats.parts, clips };
        console.log(name, results[name]);
    }
    await page.close();
    return results;
}

async function checkLoot(browser, id, paths, expected) {
    const page = await browser.newPage();
    // The review protocol is compiled from the desktop encoder/decoder. Route
    // only these static files to use it without replacing the W1 acceptance page.
    await page.route(`${paths.url}/*`, async route => {
        const path = new URL(route.request().url()).pathname.slice(1);
        if (["review.mjs", "transport.mjs", "protocol.mjs", "protocol.wasm"].includes(path)) {
            await route.fulfill({ path: join(paths.protocol, path) });
        } else {
            await route.continue();
        }
    });
    await page.goto(paths.url + "/");
    const result = await page.evaluate(async () => {
        const { inspectConnection } = await import("/review.mjs");
        const signal = async sdp => {
            const response = await fetch("/rtc/offer", {
                method: "POST", headers: { "Content-Type": "application/json" }, body: JSON.stringify({ sdp }),
            });
            const answer = await response.json();
            if (!response.ok) throw new Error(answer.error);
            return answer.sdp;
        };
        let timer;
        let transport;
        try {
            return await new Promise((resolve, reject) => {
                timer = setTimeout(() => reject(Error("Timed out receiving the full LootState")), 30000);
                let scene;
                inspectConnection({
                    name: "W2-anchors", signal,
                    onUpdate: data => {
                        if (data.type === "welcome") scene = data.scene;
                        if (data.type === "loot") resolve({ scene, ids: data.ids, entries: data.entries });
                    },
                    onClose: () => reject(Error("DS closed the review connection")),
                }).then(value => { transport = value; }, reject);
            });
        } finally {
            clearTimeout(timer);
            transport?.disconnect();
        }
    });
    assert.equal(result.scene, manifest.maps[id].map.scene);
    assert.deepEqual(result.ids.sort(), [...expected].sort());
    await page.close();
    return { mapEntries: result.ids.length, packetEntries: result.entries };
}

const browser = await chromium.launch({
    ...(process.env.ND_W2_CHROME === "1" ? { channel: "chrome" } : {}),
    args: ["--enable-webgl", "--ignore-gpu-blocklist"],
});
const results = { maps: {}, rigs: {} };
try {
    const page = await browser.newPage({ viewport: { width: 1920, height: 1080 } });
    await page.goto(url);
    for (const [id, view] of [["training_ground", "出生点"], ["cz_loket_riverbend", "核心区"]]) {
        if (!manifest.maps[id]) continue;
        await page.locator(`[data-map=${id}]`).click();
        await page.waitForFunction(id => window.nextdayzMapReview?.id === id ||
            document.querySelector("#status").textContent.includes("加载失败"), id, { timeout: 60000 });
        const map = await page.evaluate(() => {
            const r = window.nextdayzMapReview;
            return r ? {
                stats: r.stats, boundError: r.boundError, batches: r.batches,
                triangles: r.triangles, mapBytes: r.mapBytes, transferBytes: r.transferBytes,
                drawCalls: r.renderer.info.render.calls, ids: r.anchors.loot.map(entry => entry.persistentId),
            } : document.querySelector("#status").textContent;
        });
        if (typeof map === "string") throw Error(map);
        assert.equal(map.triangles, map.stats.desktopTriangles);
        assert(map.boundError < .001);
        assert.equal(map.batches, map.stats.batches);
        for (const chunk of map.stats.chunks) {
            const file = manifest.maps[id].files[chunk.file];
            const response = await fetch(`${url}/assets/${file.path}`, { method: "HEAD", headers: { "Accept-Encoding": "gzip" } });
            assert(response.ok);
            assert.equal(Number(response.headers.get("content-length")), file.gzipBytes || file.bytes);
            if (file.gzipBytes) assert.equal(response.headers.get("content-encoding"), "gzip");
        }
        map.collision = await checkMapBinaries(manifest.maps[id], map.stats);
        const port = id === "training_ground" ? process.env.ND_W2_DS_TRAINING : process.env.ND_W2_DS_LOKET;
        if (port) {
            const protocol = process.env.ND_W1_OUTPUT ?? join(evidence, "protocol");
            map.ds = await checkLoot(browser, id, { url: port, protocol }, map.ids);
        }
        await page.screenshot({ path: join(evidence, `${id}.png`) });
        await page.getByRole("button", { name: view, exact: true }).click();
        await page.getByText("安全落点", { exact: true }).click();
        await page.evaluate(() => new Promise(resolve => requestAnimationFrame(resolve)));
        await page.screenshot({ path: join(evidence, `${id}-detail.png`) });
        delete map.ids;
        delete map.stats.chunks;
        results.maps[id] = map;
        console.log(id, { triangles: map.triangles, batches: map.batches, drawCalls: map.drawCalls,
            transferBytes: map.transferBytes, boundError: map.boundError, collision: map.collision, ds: map.ds });
    }
    await page.close();
    results.rigs = await checkRigs(browser);
    const output = join(evidence, "verification.json");
    await mkdir(dirname(output), { recursive: true });
    await writeFile(output, JSON.stringify(results, null, 2) + "\n");
} finally {
    await browser.close();
}
