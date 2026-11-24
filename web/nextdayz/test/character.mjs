import assert from "node:assert/strict";
import { readFile } from "node:fs/promises";
import { MeshoptDecoder } from "three/addons/libs/meshopt_decoder.module.js";
import createModule from "../../../out/web/nextdayz/core/ndcore-review.mjs";
import { Client } from "../shared/client.mjs";
import { installCollision } from "../shared/collision.mjs";

const fixture = process.argv[2] ?? "out/web/nextdayz/w5-evidence/traversal/collision.bin";
const [wasm, bytes] = await Promise.all([createModule(), readFile(fixture), MeshoptDecoder.ready]);
const client = new Client(wasm, {});
// Drive the real direct-join state machine locally. This fixture exercises the
// baked map, keeping the network-independent ticks deterministic.
client.drain = () => {};
const scene = "traversal.scad";
const start = (x, z) => client.command({ type: "reviewPlace", position: [x, 0.75, z], yaw: 0 });
const step = (count, flags = 0, first = 0) => {
    const frames = [];
    for (let i = 0; i < count; ++i) {
        client.controls(flags | (i === 0 ? first : 0), 0, 0);
        client.tick(1 / 60);
        frames.push(client.read());
    }
    return frames;
};
try {
    installCollision(bytes.buffer.slice(bytes.byteOffset, bytes.byteOffset + bytes.byteLength), client);
    client.command({ type: "direct", name: "Fixture" });
    client.command({ type: "connected" });
    const welcome = Buffer.alloc(10 + scene.length);
    welcome[0] = 2;
    welcome.writeUInt16LE(client.protocol, 1);
    welcome.writeUInt32LE(2, 3);
    welcome.writeUInt16LE(scene.length, 7);
    welcome.write(scene, 9);
    client.receive(welcome);
    client.command({ type: "mapReady", scene, spawn: [0, .06, 1.6] });
    assert.equal(client.read().phase, "playing");

    const forward = 1;
    const jump = 1 << 9;
    const jumpHeld = 1 << 7;
    const walk = 1 << 4;
    const sprint = 1 << 5;
    const crouch = 1 << 10;
    const toggleView = 1 << 11;
    for (const [expected, x, z, approach] of [
        ["vault", 0, 1.6, 18],
        ["mantle", 4.75, 2.6, 18],
        ["climb", 9.5, 2.6, 18],
        ["climb_over", 16, 2.6, 18],
    ]) {
        start(x, z);
        step(60);
        step(approach, forward);
        const frames = step(120, forward | jumpHeld, jump);
        const active = frames.find(frame => frame.pose.traversal === expected);
        const traversals = [...new Set(frames.map(frame => frame.pose.traversal))];
        assert(active, `${expected}: ${traversals.join(", ")} (${frames.at(-1).pose.probe})`);
        assert(frames.at(-1).feet[2] > 4.2, `${expected} did not clear the front face`);
        console.log(expected, active.pose.probe, frames.at(-1).feet);
    }
    start(0, 2.6);
    step(60);
    const blocked = step(150, forward).at(-1);
    assert(blocked.feet[2] > 3 && blocked.feet[2] < 4, "collider stopped the capsule before the wall");
    start(-19, -9);
    step(60);
    const walkFrame = step(60, forward | walk).at(-1);
    const runFrame = step(60, forward).at(-1);
    const sprintFrame = step(60, forward | sprint).at(-1);
    assert.equal(walkFrame.pose.gait, "walk");
    assert.equal(runFrame.pose.gait, "run");
    assert.equal(sprintFrame.pose.gait, "sprint");
    assert(walkFrame.pose.speed < runFrame.pose.speed && runFrame.pose.speed < sprintFrame.pose.speed);
    const low = step(30, 0, crouch).at(-1);
    assert.equal(low.pose.stance, "crouched");
    const firstPerson = step(1, 0, toggleView)[0];
    assert.equal(firstPerson.camera.firstPerson, true);
    console.log("wall, walk/run/sprint, crouch and first-person OK");
} finally {
    client.destroy();
}
