import * as THREE from "three";
import { dispose, type Level } from "./assets";
import type { LevelWorkerMessage } from "./scadLevel.worker";
import type { Anchor, XYZ } from "./types";
import { tr } from "./settings";
import type { GeneratedCollision, WordRange } from "../shared/collision.mjs";

interface TextureIndex {
    size: number;
    albedo: WordRange;
    normal: WordRange;
    mra: WordRange;
}

interface Surface {
    print: number;
    roughness: number;
    metalness: number;
    transparent: boolean;
    decal: boolean;
}

interface Batch {
    surface: number;
    vertices: WordRange;
    directions: WordRange;
    indices: WordRange;
    colors: WordRange;
    matrices?: WordRange;
}

interface Evaluation {
    evaluatedTriangles: number;
    terrainCount: number;
    warningCount: number;
    warnings: string[];
}

export interface ScadLevelIndex {
    anchors: Anchor[];
    bounds: [XYZ, XYZ];
    triangles: number;
    collisionTriangles: number;
    collision: Omit<GeneratedCollision, "packet">;
    batches: Batch[];
    textures: TextureIndex[];
    surfaces: Surface[];
    diagnostics: Evaluation;
    evaluateMs: number;
    generateMs: number;
}

export interface ScadLevelMetrics extends Evaluation {
    evaluatorBytes: number;
    sourceBytes: number;
    workerMs: number;
    wasmMemoryBytes: number;
    packetBytes: number;
    triangles: number;
    collisionTriangles: number;
    collisionMeshes: number;
    collisionInstances: number;
    anchors: number;
    bounds: [XYZ, XYZ];
    batches: number;
    textures: number;
    evaluateMs: number;
    generateMs: number;
    prepareMs?: number;
}

function buildScene(packet: ArrayBuffer, index: ScadLevelIndex) {
    const texture = (range: WordRange, size: number, srgb = false) => {
        if (range[1] === 0) return null;
        const data = new Uint8Array(packet, range[0] * 4, range[1] * 4);
        const image = new THREE.DataTexture(data, size, size);
        image.colorSpace = srgb ? THREE.SRGBColorSpace : THREE.NoColorSpace;
        image.wrapS = image.wrapT = THREE.RepeatWrapping;
        image.minFilter = THREE.LinearMipmapLinearFilter;
        image.magFilter = THREE.LinearFilter;
        image.generateMipmaps = true;
        image.needsUpdate = true;
        return image;
    };
    const maps = index.textures.map(({ size, albedo, normal, mra }) => ({
        map: texture(albedo, size, true),
        normalMap: texture(normal, size),
        mra: texture(mra, size),
    }));
    const materials = index.surfaces.map(surface => {
        const print = maps[surface.print];
        return new THREE.MeshStandardMaterial({
            roughness: surface.roughness,
            metalness: surface.metalness,
            map: print?.map ?? null,
            normalMap: print?.normalMap ?? null,
            roughnessMap: print?.mra ?? null,
            metalnessMap: print?.mra ?? null,
            transparent: surface.transparent,
            depthWrite: !surface.transparent,
            polygonOffset: surface.decal,
            polygonOffsetFactor: 1,
            polygonOffsetUnits: 4,
            vertexColors: true,
        });
    });
    const group = new THREE.Group();
    const instanceGeometry = new Map<number, THREE.BufferGeometry>();
    const instanceMaterials = new Map<number, THREE.MeshStandardMaterial>();
    const floats = ([offset, length]: WordRange) => new Float32Array(packet, offset * 4, length);
    const rgba = ([offset, length]: WordRange) => new Uint8Array(packet, offset * 4, length * 4);
    const matrix = new THREE.Matrix4();
    const color = new THREE.Color();

    function createGeometry(batch: Batch) {
        const geometry = new THREE.BufferGeometry();
        const vertices = new THREE.InterleavedBuffer(floats(batch.vertices), 5);
        const normals = new THREE.InterleavedBuffer(new Int16Array(packet, batch.directions[0] * 4, batch.directions[1] * 2), 8);
        geometry.setAttribute("position", new THREE.InterleavedBufferAttribute(vertices, 3, 0));
        geometry.setAttribute("uv", new THREE.InterleavedBufferAttribute(vertices, 2, 3));
        geometry.setAttribute("normal", new THREE.InterleavedBufferAttribute(normals, 3, 0, true));
        geometry.setAttribute("tangent", new THREE.InterleavedBufferAttribute(normals, 4, 4, true));
        geometry.setIndex(new THREE.BufferAttribute(new Uint32Array(packet, batch.indices[0] * 4, batch.indices[1]), 1));
        return geometry;
    }

    try {
        for (const batch of index.batches) {
            const material = materials[batch.surface];
            if (!batch.matrices) {
                const geometry = createGeometry(batch);
                geometry.setAttribute("color", new THREE.BufferAttribute(rgba(batch.colors), 4, true));
                group.add(new THREE.Mesh(geometry, material));
                continue;
            }

            let geometry = instanceGeometry.get(batch.vertices[0]);
            if (!geometry) {
                geometry = createGeometry(batch);
                instanceGeometry.set(batch.vertices[0], geometry);
            }
            let instanceMaterial = instanceMaterials.get(batch.surface);
            if (!instanceMaterial) {
                instanceMaterial = material.clone();
                instanceMaterial.vertexColors = false;
                instanceMaterials.set(batch.surface, instanceMaterial);
            }
            const matrices = floats(batch.matrices);
            const tints = rgba(batch.colors);
            const mesh = new THREE.InstancedMesh(geometry, instanceMaterial, matrices.length / 16);
            for (let i = 0; i < mesh.count; ++i) {
                mesh.setMatrixAt(i, matrix.fromArray(matrices, i * 16));
                const offset = i * 4;
                mesh.setColorAt(i, color.setRGB(tints[offset] / 255, tints[offset + 1] / 255, tints[offset + 2] / 255));
            }
            group.add(mesh);
        }
        return group;
    } catch (error) {
        dispose(group);
        throw error;
    }
}

export async function loadScadLevel(scene: string, onEvaluate: () => void): Promise<Level> {
    const start = performance.now();
    const worker = new Worker(new URL("./scadLevel.worker.ts", import.meta.url), { type: "module" });
    try {
        const { index, packet, metrics } = await new Promise<Extract<LevelWorkerMessage, { status: "ready" }>>((resolve, reject) => {
            worker.onmessage = ({ data }: MessageEvent<LevelWorkerMessage>) => {
                if (data.status === "evaluating") onEvaluate();
                else if (data.status === "ready") resolve(data);
                else reject(Error(data.message));
            };
            worker.onerror = event => reject(Error(event.message || "SCAD Worker initialization failed"));
            worker.onmessageerror = () => reject(Error("SCAD Worker scene transfer failed"));
            worker.postMessage(scene);
        });
        const spawn = index.anchors.find(anchor => anchor.type === "playerSafe")?.position;
        if (!spawn) throw Error(tr("The map has no safe spawn point"));
        const group = buildScene(packet, index);
        metrics.prepareMs = performance.now() - start;
        return {
            scene, spawn, group, anchors: index.anchors, bounds: index.bounds, batches: index.batches.length,
            sunElevation: Math.atan(0.75), cacheHits: 0, collision: { ...index.collision, packet }, scad: metrics,
        };
    } finally {
        worker.terminate();
    }
}
