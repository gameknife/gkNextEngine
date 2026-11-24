interface ProbeManifest {
    scene: string;
    files: string[];
}

export interface ScadModule {
    FS: {
        mkdirTree(path: string): void;
        writeFile(path: string, bytes: Uint8Array): void;
    };
    HEAPF32: Float32Array;
    HEAPU8: Uint8Array;
    UTF8ToString(ptr: number): string;
    stringToNewUTF8(path: string): number;
    _free(ptr: number): void;
    _ScadGenerate(path: number): number;
    _ScadVertices(): number;
    _ScadFloatCount(): number;
    _ScadReport(): number;
    _ScadGenerateLevel(path: number): number;
    _ScadLevelIndex(): number;
    _ScadLevelPacket(): number;
    _ScadLevelSize(): number;
}

const root = new URL("/scad-probe/", self.location.href);
const evaluator = new URL("ndscad.mjs", root).href;
const wasm = new URL("ndscad.wasm", root).href;
const manifestUrl = new URL("manifest.json", root).href;

async function fetchFile(url: string) {
    const response = await fetch(url);
    if (!response.ok) // Runs in a worker, which has no translation table: the message stays English.
    throw new Error(`Read failed (HTTP ${response.status}) ${new URL(url).pathname}`);
    return response;
}

async function loadSources() {
    const manifest: ProbeManifest = await (await fetchFile(manifestUrl)).json();
    const files = await Promise.all(manifest.files.map(async path => {
        const url = new URL(`sources/${path}`, root).href;
        const bytes = new Uint8Array(await (await fetchFile(url)).arrayBuffer());
        return { path, url, bytes };
    }));
    return { scene: manifest.scene, files };
}

async function loadEvaluator(): Promise<ScadModule> {
    const { default: createModule } = await import(/* @vite-ignore */ evaluator);
    return createModule({ locateFile: () => wasm });
}

function encodedBytes(url: string) {
    const [entry] = performance.getEntriesByName(url, "resource") as PerformanceResourceTiming[];
    return entry?.encodedBodySize ?? 0;
}

export async function loadScadModule(expectedScene?: string) {
    const [module, sources] = await Promise.all([loadEvaluator(), loadSources()]);
    if (expectedScene && sources.scene !== expectedScene) {
        throw Error(`SCAD source scene does not match the server's published map · ${expectedScene}`);
    }
    for (const { path, bytes } of sources.files) {
        module.FS.mkdirTree("/" + path.slice(0, path.lastIndexOf("/")));
        module.FS.writeFile("/" + path, bytes);
    }
    return {
        module, scene: sources.scene,
        evaluatorBytes: encodedBytes(evaluator) + encodedBytes(wasm),
        sourceBytes: sources.files.reduce((sum, file) => sum + encodedBytes(file.url), encodedBytes(manifestUrl)),
    };
}

export function evaluateScad(module: ScadModule, scene: string, evaluate: (path: number) => number, report: () => number) {
    const path = module.stringToNewUTF8(scene);
    let ok: number;
    try {
        ok = evaluate(path);
    } finally {
        module._free(path);
    }
    const json = module.UTF8ToString(report());
    if (!ok) throw Error(json);
    return JSON.parse(json);
}
