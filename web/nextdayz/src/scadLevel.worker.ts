import { evaluateScad, loadScadModule } from "./scadModule";
import type { ScadLevelIndex, ScadLevelMetrics } from "./scadLevel";

export type LevelWorkerMessage =
    | { status: "evaluating" }
    | { status: "ready"; index: ScadLevelIndex; packet: ArrayBuffer; metrics: ScadLevelMetrics }
    | { status: "error"; message: string };

const worker = self as unknown as {
    onmessage: (message: MessageEvent<string>) => void;
    postMessage(message: LevelWorkerMessage, transfer?: Transferable[]): void;
};

worker.onmessage = ({ data: scene }) => {
    void generate(scene).catch(error => {
        worker.postMessage({ status: "error", message: error instanceof Error ? error.message : String(error) });
    });
};

async function generate(expectedScene: string) {
    const start = performance.now();
    const { module, scene, evaluatorBytes, sourceBytes } = await loadScadModule(expectedScene);
    worker.postMessage({ status: "evaluating" });
    const index: ScadLevelIndex = evaluateScad(module, scene, module._ScadGenerateLevel, module._ScadLevelIndex);
    const ptr = module._ScadLevelPacket();
    const packet = module.HEAPU8.slice(ptr, ptr + module._ScadLevelSize()).buffer;
    const metrics = {
        evaluatorBytes, sourceBytes, workerMs: performance.now() - start, wasmMemoryBytes: module.HEAPU8.byteLength,
        packetBytes: packet.byteLength, triangles: index.triangles, collisionTriangles: index.collisionTriangles,
        collisionMeshes: index.collision.meshes.length, collisionInstances: index.collision.records[1] / 30,
        anchors: index.anchors.length, bounds: index.bounds, batches: index.batches.length, textures: index.textures.length,
        evaluateMs: index.evaluateMs, generateMs: index.generateMs, ...index.diagnostics,
    };
    worker.postMessage({ status: "ready", index, packet, metrics }, [packet]);
}
