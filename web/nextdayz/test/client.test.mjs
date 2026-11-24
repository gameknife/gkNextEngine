import assert from "node:assert/strict";
import { readFile } from "node:fs/promises";
import { test } from "node:test";
import createModule from "../../../out/web/nextdayz/core/ndcore.mjs";
import { Client } from "../shared/client.mjs";

test("the WASM client authenticates, loads storage, and asks again while the DS starts", async () => {
    const calls = [];
    let deploys = 0;
    const stored = { id: "bandage", count: 3 };
    const map = { id: "training_ground", name: "TRAINING", scene: "training.scad", training: true };
    const master = {
        login: { playerId: 2, token: "test-token" },
        profile: { playerId: 2, name: "Tester", storage: [stored], maps: [map] },
    };
    let resolveMap;
    const mapRequested = new Promise(resolve => { resolveMap = resolve; });
    const client = new Client(await createModule(), {
        async fetchMaster(request, body, token) {
            calls.push({ request, body: JSON.parse(body), token });
            const result = request === "deploy"
                ? { state: ++deploys === 1 ? "starting" : "ready", server: "ds1", host: "localhost",
                    port: 27015, ticket: "ticket", map: map.id, scene: map.scene }
                : master[request];
            return Response.json(result);
        },
        async loadMap(scene) {
            resolveMap(scene);
            return new Promise(() => {});
        },
        signal() { throw Error("Expected the map to finish loading before signaling"); },
    });
    try {
        client.command({ type: "login", name: "Tester" });
        for (let i = 0; i < 20 && client.read().phase !== "profile"; ++i) await new Promise(resolve => setImmediate(resolve));
        assert.equal(client.read().phase, "profile");
        assert.equal(client.read("profile").storage[0].count, 3);
        assert.equal(client.read("profile").maps[0].id, map.id);
        assert.deepEqual(calls.map(call => [call.request, call.token]), [["login", ""], ["profile", "test-token"]]);
        client.command({ type: "deploy", map: map.id, bring: [{ index: 0, count: 1 }] });
        await new Promise(resolve => setImmediate(resolve));
        assert.equal(client.read().phase, "deploying");
        for (let i = 0; i < 8; ++i) client.tick(.15);
        assert.equal(await mapRequested, map.scene);
        assert.equal(client.read().phase, "joining");
        assert.equal(deploys, 2);
        assert.deepEqual(calls.at(-1).body, { map: map.id, bring: [{ index: 0, count: 1 }] });
    } finally {
        client.destroy();
    }
});

test("only the review kernel knows the review shortcuts", async () => {
    const core = new URL("../../../out/web/nextdayz/core/", import.meta.url);
    const [release, review] = await Promise.all([readFile(new URL("ndcore.wasm", core)), readFile(new URL("ndcore-review.wasm", core))]);
    for (const command of ["reviewPlace", "reviewCrowd"]) {
        assert.equal(release.includes(command), false, `release kernel handles ${command}`);
        assert.equal(review.includes(command), true, `review kernel lacks ${command}`);
    }
});
