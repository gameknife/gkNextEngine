import { LoadingManager, Group } from "three";
import { GLTFLoader } from "three/addons/loaders/GLTFLoader.js";
import { MeshoptDecoder } from "three/addons/libs/meshopt_decoder.module.js";
import type { Anchor, AssetFile, Manifest, MapBundle, XYZ } from "./types";
import type { LevelCollision } from "../shared/collision.mjs";
import type { ScadLevelMetrics } from "./scadLevel";

export const assetURL = (path: string) => "/game-assets/" + path;
const cacheKey = "nextdayz-map-assets-v1";

export interface Level {
    scene: string;
    spawn: XYZ;
    group: Group;
    bounds: [XYZ, XYZ];
    batches: number;
    sunElevation: number;
    cacheHits: number;
    collision: LevelCollision;
    anchors: Anchor[];
    scad?: ScadLevelMetrics;
}

interface MapStats { chunks: { file: string }[]; bounds: [XYZ, XYZ]; batches: number }
interface AnchorTable { anchors: Anchor[] }
interface Environment { sun: { elevation: number }; profile: string }

export async function manifest(): Promise<Manifest> {
    const response = await fetch(assetURL("manifest.json"), { cache: "no-store" });
    if (!response.ok) throw Error("找不到 Web 资产清单，请先烘焙已发布地图");
    return response.json();
}

async function verify(bytes: ArrayBuffer, file: AssetFile) {
    if (bytes.byteLength !== file.bytes) throw Error(`资源长度不一致 · ${file.path}`);
    const digest = new Uint8Array(await crypto.subtle.digest("SHA-256", bytes));
    const hash = Array.from(digest, value => value.toString(16).padStart(2, "0")).join("");
    if (hash !== file.sha256) throw Error(`资源校验失败 · ${file.path}`);
}

export class Assets {
    cacheHits = 0;
    private cache = globalThis.caches?.open(cacheKey).catch(() => undefined);

    async read(file: AssetFile) {
        const path = assetURL(file.path);
        const cache = await this.cache;
        const cached = await cache?.match(path);
        if (cached) {
            const bytes = await cached.arrayBuffer();
            try {
                await verify(bytes, file);
                this.cacheHits++;
                return bytes;
            } catch {
                await cache?.delete(path);
            }
        }
        const response = await fetch(path);
        if (!response.ok) throw Error(`资源下载失败 · ${response.status} · ${file.path}`);
        const bytes = await response.arrayBuffer();
        await verify(bytes, file);
        if (cache) {
            await cache.put(path, new Response(bytes, {
                headers: { "Content-Type": response.headers.get("Content-Type") ?? "application/octet-stream" },
            })).catch(() => undefined);
        }
        return bytes;
    }

    async json<T>(file: AssetFile): Promise<T> {
        return JSON.parse(new TextDecoder().decode(await this.read(file)));
    }
}

async function work<T>(values: T[], run: (value: T) => Promise<void>) {
    let next = 0;
    let failure: unknown;
    await Promise.all(Array.from({ length: 6 }, async () => {
        while (!failure && next < values.length) {
            try {
                await run(values[next++]);
            } catch (error) {
                failure = error;
            }
        }
    }));
    if (failure) throw failure;
}

export async function loadLevel(bundle: MapBundle, progress: (done: number, total: number) => void): Promise<Level> {
    const cache = new Assets();
    const read = <T>(name: string) => cache.json<T>(bundle.files[name]);
    const [stats, anchors, environment, collision] = await Promise.all([
        read<MapStats>("bake.json"), read<AnchorTable>("anchors.json"), read<Environment>("env.json"),
        cache.read(bundle.files["collision.bin"]), MeshoptDecoder.ready,
    ]);
    const spawn = anchors.anchors.find(anchor => anchor.type === "playerSafe")?.position;
    if (!spawn) throw Error("地图缺少安全落点");

    const textures = Object.keys(bundle.files).filter(name => name.endsWith(".webp"));
    const images = new Map<string, string>();
    const paths = new Map(Object.entries(bundle.files).map(([key, file]) => [key, assetURL(file.path)]));
    const group = new Group();
    const total = textures.length + stats.chunks.length;
    let done = 0;
    progress(done, total);
    try {
        await work(textures, async key => {
            const bytes = await cache.read(bundle.files[key]);
            const blob = URL.createObjectURL(new Blob([bytes], { type: "image/webp" }));
            images.set(paths.get(key)!, blob);
            progress(++done, total);
        });
        const manager = new LoadingManager();
        manager.setURLModifier(request => images.get(request) ?? request);
        const loader = new GLTFLoader(manager).setMeshoptDecoder(MeshoptDecoder);
        await work(stats.chunks, async chunk => {
            const file = bundle.files[chunk.file];
            const base = assetURL(file.path.slice(0, file.path.lastIndexOf("/") + 1));
            const asset = await loader.parseAsync(await cache.read(file), base);
            group.add(asset.scene);
            progress(++done, total);
        });
    } catch (error) {
        dispose(group);
        throw error;
    } finally {
        for (const url of images.values()) URL.revokeObjectURL(url);
    }
    return { scene: bundle.map.scene, spawn, group, bounds: stats.bounds, batches: stats.batches,
        sunElevation: environment.sun.elevation, cacheHits: cache.cacheHits, collision, anchors: anchors.anchors };
}

export function dispose(group: Group) {
    const materials = new Set<import("three").Material>();
    const maps = new Set<import("three").Texture>();
    group.traverse(node => {
        const mesh = node as import("three").Mesh;
        if (!mesh.isMesh) return;
        if ((mesh as import("three").InstancedMesh).isInstancedMesh) (mesh as import("three").InstancedMesh).dispose();
        mesh.geometry.dispose();
        for (const material of Array.isArray(mesh.material) ? mesh.material : [mesh.material]) {
            materials.add(material);
        }
    });
    for (const material of materials) {
        for (const value of Object.values(material)) if (value?.isTexture) maps.add(value);
        material.dispose();
    }
    for (const map of maps) {
        if (map.image instanceof ImageBitmap) map.image.close();
        map.dispose();
    }
}
