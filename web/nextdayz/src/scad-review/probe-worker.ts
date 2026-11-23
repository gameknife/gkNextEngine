import { evaluateScad, loadScadModule } from "../scadModule";

export interface ProbeMetrics {
    triangleCount: number;
    evaluatedTriangles: number;
    terrainCount: number;
    warningCount: number;
    warnings: string[];
    evaluatorBytes: number;
    sourceBytes: number;
    evaluateMs: number;
    workerMs: number;
    wasmMemoryBytes: number;
}

export type ProbeMessage =
    | { status: "evaluating" }
    | { status: "ready"; vertices: Float32Array<ArrayBuffer>; decalStart: number; gate: [number, number, number]; metrics: ProbeMetrics }
    | { status: "error"; message: string };

const worker = self as unknown as {
    postMessage(message: ProbeMessage, transfer?: Transferable[]): void;
};
async function generate() {
    const start = performance.now();
    const { module, scene, evaluatorBytes, sourceBytes } = await loadScadModule();
    worker.postMessage({ status: "evaluating" });
    const beforeGenerate = performance.now();
    const report = evaluateScad(module, scene, module._ScadGenerate, module._ScadReport);
    const evaluateMs = performance.now() - beforeGenerate;

    const vertexStart = module._ScadVertices() / Float32Array.BYTES_PER_ELEMENT;
    const vertices = module.HEAPF32.slice(vertexStart, vertexStart + module._ScadFloatCount());
    const { triangleCount, evaluatedTriangles, terrainCount, warningCount, warnings, decalStart, gate } =
        report as ProbeMetrics & { decalStart: number; gate: [number, number, number] };
    const metrics: ProbeMetrics = {
        triangleCount, evaluatedTriangles, terrainCount, warningCount, warnings,
        evaluatorBytes, sourceBytes,
        evaluateMs,
        workerMs: performance.now() - start,
        wasmMemoryBytes: module.HEAPU8.buffer.byteLength,
    };
    worker.postMessage({ status: "ready", vertices, decalStart, gate, metrics }, [vertices.buffer]);
}

void generate().catch(error => {
    worker.postMessage({ status: "error", message: error instanceof Error ? error.message : String(error) });
});
