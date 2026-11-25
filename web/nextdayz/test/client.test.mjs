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

test("the WASM client relays trading and contracts to the Master and shows its economy", async () => {
    const calls = [];
    const economy = {
        reputation: 2, tier: 0, nextTier: 3, maxActive: 2, slots: 3, contractsCompleted: 1,
        board: [{ id: 7, template: "med_dressings", title: "Field dressings", issuer: "medical", type: "deliver", tier: 1,
            items: [{ item: "bandage", count: 3 }], reward: 44, reputation: 1, sortiesLeft: 2, canHandIn: true, online: true }],
        contracts: [], trader: [{ item: "medkit", tier: 0, pack: 1, price: 75, left: 2, locked: false }],
        sellValues: [5], warehouse: { used: 1, capacity: 40, over: false, nextCapacity: 60, nextPrice: 300 },
    };
    const master = {
        login: { playerId: 3, token: "trade-token" },
        profile: { playerId: 3, name: "Trader", gold: 90, storage: [{ id: "bandage", count: 3 }], maps: [], economy },
    };
    const client = new Client(await createModule(), {
        async fetchMaster(request, body, token) {
            calls.push({ request, body: JSON.parse(body), token });
            if (request === "buy") return Response.json({ error: "not enough gold" }, { status: 409 });
            return Response.json(master[request] ?? { ok: true });
        },
        loadMap() { return new Promise(() => {}); },
        signal() { throw Error("no signaling in the lobby"); },
    });
    const settle = async () => { for (let i = 0; i < 20; ++i) await new Promise(resolve => setImmediate(resolve)); };
    try {
        client.command({ type: "login", name: "Trader" });
        await settle();
        const profile = client.read("profile");
        assert.equal(profile.economy.available, true);
        assert.equal(profile.economy.board[0].items[0].name, "Bandage");
        assert.equal(profile.economy.trader[0].name, "First Aid Kit");
        assert.equal(profile.economy.capacity, 40);
        const revision = profile.revision;
        client.command({ type: "economy", op: "handin", contractId: 7 });
        await settle();
        client.command({ type: "economy", op: "sell", index: 0, item: "bandage", count: 2 });
        await settle();
        client.command({ type: "economy", op: "buy", item: "medkit" });
        await settle();
        assert.deepEqual(calls.slice(2).map(call => [call.request, call.body]), [
            ["contracts/handin", { contractId: 7 }], ["profile", {}],
            ["sell", { index: 0, item: "bandage", count: 2 }], ["profile", {}],
            ["buy", { item: "medkit" }], ["profile", {}],
        ]);
        assert.equal(client.read().phase, "profile");
        assert.equal(client.read("profile").revision, revision + 3);
        assert.match(client.read().message, /not enough gold/);
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
