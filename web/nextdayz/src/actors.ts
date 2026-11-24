import * as THREE from "three";
import { GLTFLoader } from "three/addons/loaders/GLTFLoader.js";
import { MeshoptDecoder } from "three/addons/libs/meshopt_decoder.module.js";
import type { Client } from "../shared/client.mjs";
import { Assets, assetURL } from "./assets";
import { tr } from "./settings";
import type { AssetFile, Frame, GroundItem, Manifest, Mount, Pose, XYZ } from "./types";

interface Part {
    bone: number;
    name: string;
    object: THREE.Object3D;
    gear?: number; // weapon models: 0 held, 1 + slot holstered; matrices follow the bones
}

interface RigParts {
    parts: Part[];
    bytes: Uint8Array;
}

function boneNames(data: Uint8Array) {
    const view = new DataView(data.buffer, data.byteOffset, data.byteLength);
    const count = view.getUint32(4, true);
    const utf8 = new TextDecoder();
    const names: string[] = [];
    let offset = 12;
    for (let i = 0; i < count; ++i) {
        const length = view.getUint32(offset, true);
        names.push(utf8.decode(data.subarray(offset + 4, offset + 4 + length)));
        offset += length + 48;
    }
    return names;
}

async function readRig(name: string, files: Record<string, AssetFile>, cache: Assets): Promise<RigParts> {
    const read = (filename: string) => cache.read(files[filename]);
    const textures = new Map<string, string>();
    const manager = new THREE.LoadingManager();
    manager.setURLModifier(request => textures.get(request) ?? request);
    const loader = new GLTFLoader(manager).setMeshoptDecoder(MeshoptDecoder);
    const binaryFile = name + ".rig.bin";
    const glbFile = name + ".glb";
    const base = assetURL(files[glbFile].path.replace(/[^/]+$/, ""));
    const imageFiles = Object.entries(files).filter(([key]) => key.endsWith(".webp"));
    const requests = await Promise.all([read(binaryFile), read(glbFile),
        Promise.all(imageFiles.map(async ([key, file]) => [file, await read(key)] as const))]);
    for (const [file, bytes] of requests[2])
        textures.set(assetURL(file.path), URL.createObjectURL(new Blob([bytes], { type: "image/webp" })));
    try {
        const bytes = new Uint8Array(requests[0]);
        const names = boneNames(bytes);
        const model = await loader.parseAsync(requests[1], base);
        const parts: Part[] = [];
        model.scene.traverse(object => {
            const bone = object.userData.bone;
            if (typeof bone === "number" && names[bone]) parts.push({ bone, name: names[bone], object });
        });
        return { bytes, parts };
    } finally {
        for (const url of textures.values()) URL.revokeObjectURL(url);
    }
}

export async function loadActors(manifest: Manifest) {
    const cache = new Assets();
    const load = (name: string) => {
        const entry = manifest.rigs?.[name];
        if (!entry) throw Error(tr("Character assets are missing. Run gnb web bake --rigs first."));
        return readRig(name, entry.files, cache);
    };
    const [survivor, infected, clothes, weapon, hands, mounts, ground] = await Promise.all([
        load("nextdayz_survivor"), load("nextdayz_infected"), load("nextdayz_clothing"), load("nextdayz_weapons"), load("nextdayz_fps_hands"), load("nextdayz_attachments"), load("nextdayz_loot_items"),
    ]);
    return { survivor, infected, clothes, weapon, hands, mounts, ground, survivorBones: boneNames(survivor.bytes) };
}

export type ActorAssets = Awaited<ReturnType<typeof loadActors>>;

function fitMounts(root: THREE.Object3D, assets: ActorAssets, mounts: Mount[]) {
    for (const mount of mounts) {
        const part = assets.mounts.parts[mount.index];
        if (!part) continue;
        const model = part.object.clone();
        model.position.fromArray(mount.position);
        model.scale.setScalar(mount.scale);
        root.add(model);
    }
}

export class GroundModels {
    private objects = new Map<string, THREE.Object3D>();
    private catalogue: Map<string, THREE.Object3D>;

    constructor(private readonly assets: ActorAssets, private readonly scene: THREE.Scene) {
        this.catalogue = new Map(assets.ground.parts.map(part => [part.name.replace(/^bone_item_/, ""), part.object]));
    }

    update(items: GroundItem[]) {
        const seen = new Set<string>();
        for (const item of items) {
            const model = this.catalogue.get(item.item);
            if (!model) continue;
            const key = item.id + ":" + item.item;
            seen.add(key);
            let object = this.objects.get(key);
            if (!object) {
                object = model.clone();
                this.objects.set(key, object);
                this.scene.add(object);
            }
            object.position.fromArray(item.position);
            object.position.y += .045;
            object.rotation.y = item.yaw;
        }
        for (const [key, object] of this.objects) {
            if (seen.has(key)) continue;
            this.scene.remove(object);
            this.objects.delete(key);
        }
    }

    clear() { this.update([]); }
}

class Survivor {
    private group = new THREE.Group();
    private body: Part[];
    private gear: Part[] = [];
    private equipment = "";
    private lastYaw = 0;

    constructor(private assets: ActorAssets, private scene: THREE.Scene) {
        this.body = assets.survivor.parts.map(part => this.attach(part, this.group));
        scene.add(this.group);
    }

    private attach(part: Part, group: THREE.Group, bone = part.bone) {
        const object = part.object.clone();
        object.matrixAutoUpdate = false;
        object.traverse(child => {
            if ((child as THREE.Mesh).isMesh) child.castShadow = child.receiveShadow = true;
        });
        group.add(object);
        return { bone, name: part.name, object };
    }

    private dress(pose: Pose) {
        const key = JSON.stringify([pose.weapon, pose.clothing, pose.slots]);
        if (key === this.equipment) return;
        this.equipment = key;
        for (const part of this.gear) this.group.remove(part.object);
        this.gear = [];
        const worn = new Set(pose.clothing);
        for (const part of this.assets.clothes.parts) {
            const separator = part.name.indexOf("__");
            const item = part.name.slice(5, separator);
            if (separator < 0 || !worn.has(item)) continue;
            const destination = part.name.slice(separator + 2);
            const bone = this.assets.survivorBones.indexOf("bone_" + (destination === "harness" ? "torso" : destination));
            if (bone >= 0) this.gear.push(this.attach(part, this.group, bone));
        }
        const coat = pose.clothingCoverage & 1;
        const trousers = pose.clothingCoverage & 2;
        for (const part of this.body) {
            const upper = part.name === "bone_torso" || /bone_(upperarm|forearm)_/.test(part.name);
            const lower = part.name === "bone_pelvis" || /bone_(thigh|calf|foot)_/.test(part.name);
            part.object.visible = !(coat && upper) && !(trousers && lower);
        }
        const weaponModel = (id: string, mounts: Mount[], gear: number) => {
            const weapon = this.assets.weapon.parts.find(part => part.name === "bone_wpn_" + id);
            if (!weapon) return;
            const part = { ...this.attach(weapon, this.group), gear };
            fitMounts(part.object, this.assets, mounts);
            this.gear.push(part);
        };
        weaponModel(pose.weapon, pose.slots[pose.activeSlot]?.mounts ?? [], 0);
        pose.slots.forEach((slot, index) => { if (slot.id) weaponModel(slot.id, slot.mounts, index + 1); });
    }

    update(position: XYZ, yaw: number, visible: boolean, pose: Pose, offset: number, matrices: Float32Array) {
        this.group.visible = visible && offset >= 0;
        if (!this.group.visible) return;
        this.group.position.fromArray(position);
        if (Number.isFinite(yaw)) this.lastYaw = yaw;
        this.group.rotation.y = this.lastYaw;
        this.dress(pose);
        for (const part of this.body.concat(this.gear)) {
            if (part.gear === undefined) part.object.matrix.fromArray(matrices, offset + part.bone * 16);
            else {
                part.object.visible = pose.gearOffset >= 0 &&
                    (part.gear === 0 ? pose.heldVisible : pose.holsterVisible[part.gear - 1] ?? false);
                if (part.object.visible) part.object.matrix.fromArray(matrices, pose.gearOffset + part.gear * 16);
            }
            part.object.matrixWorldNeedsUpdate = true;
        }
    }

    remove() { this.scene.remove(this.group); }
}

class Infected {
    private readonly capacity = 64;
    private matrices = new THREE.Matrix4();
    private root = new THREE.Matrix4();
    private bone = new THREE.Matrix4();
    private parts: { bone: number; mesh: THREE.InstancedMesh; local: THREE.Matrix4; tint: boolean }[] = [];
    private tint = new THREE.Color();
    private selected: Frame["zombies"] = [];

    constructor(private assets: ActorAssets, scene: THREE.Scene) {
        for (const part of assets.infected.parts) {
            part.object.updateWorldMatrix(true, true);
            part.object.traverse(child => {
                const model = child as THREE.Mesh;
                if (!model.isMesh) return;
                const source = model.material;
                const tint = source instanceof THREE.MeshStandardMaterial && source.userData.tintable === true;
                const material = tint ? source.clone() : source;
                // The baked magenta channel marks regions painted by the instance.
                if (tint) (material as THREE.MeshStandardMaterial).color.set(0xffffff);
                const mesh = new THREE.InstancedMesh(model.geometry, material, this.capacity);
                mesh.instanceMatrix.setUsage(THREE.DynamicDrawUsage);
                mesh.count = 0;
                mesh.frustumCulled = false;
                mesh.castShadow = mesh.receiveShadow = true;
                scene.add(mesh);
                this.parts.push({ bone: part.bone, mesh, local: model.matrixWorld.clone(), tint });
            });
        }
    }

    update(frame: Frame, matrices: Float32Array) {
        this.selected.length = 0;
        for (const zombie of frame.zombies) {
            if (zombie.matrixOffset >= 0) this.selected.push(zombie);
        }
        if (this.selected.length > this.capacity) {
            const distance = (zombie: Frame["zombies"][number]) =>
                (zombie.position[0] - frame.feet[0]) ** 2 + (zombie.position[2] - frame.feet[2]) ** 2;
            this.selected.sort((a, b) => distance(a) - distance(b));
            this.selected.length = this.capacity;
        }
        for (const part of this.parts) part.mesh.count = this.selected.length;
        for (const [index, zombie] of this.selected.entries()) {
            this.root.makeRotationY(zombie.yaw).setPosition(...zombie.position);
            if (zombie.profile === 0) {
                this.tint.setHSL(((zombie.id * .618) % 1), .29, .39);
            } else {
                this.tint.setHex(zombie.profile === 1 ? 0x72774a : 0x7e6553);
            }
            for (const part of this.parts) {
                this.bone.fromArray(matrices, zombie.matrixOffset + part.bone * 16);
                this.matrices.multiplyMatrices(this.root, this.bone).multiply(part.local);
                part.mesh.setMatrixAt(index, this.matrices);
                if (part.tint) part.mesh.setColorAt(index, this.tint);
            }
        }
        for (const { mesh } of this.parts) {
            mesh.instanceMatrix.needsUpdate = true;
            if (mesh.instanceColor) mesh.instanceColor.needsUpdate = true;
        }
    }
}

export class ViewModel {
    private scene = new THREE.Scene();
    private camera = new THREE.PerspectiveCamera(75, 1, .025, 8);
    private axis = new THREE.Group();
    private weapon = new THREE.Group();
    private parts: THREE.Object3D[] = [];
    private equipment = "";

    constructor(private assets: ActorAssets) {
        this.axis.rotation.y = Math.PI;
        this.axis.add(this.weapon);
        this.weapon.matrixAutoUpdate = false;
        this.scene.add(this.axis, new THREE.HemisphereLight(0xf0f4ff, 0x958471, 2.2));
    }

    private configure(pose: Pose) {
        const key = JSON.stringify([pose.weapon, pose.viewModelArms, pose.clothing, pose.slots[pose.activeSlot]]);
        if (key === this.equipment) return;
        this.equipment = key;
        this.weapon.clear();
        this.parts = [];
        const weapon = this.assets.weapon.parts.find(part => part.name === "bone_wpn_" + pose.weapon);
        if (!weapon) return;
        const model = weapon.object.clone();
        this.weapon.add(model);
        fitMounts(model, this.assets, pose.slots[pose.activeSlot]?.mounts ?? []);
        if (!pose.viewModelArms) return;
        const arm = (side: string) => {
            const defaultPart = this.assets.survivor.parts.find(part => part.name === "bone_forearm_" + side);
            return this.assets.clothes.parts.find(part => pose.clothing.some(item =>
                part.name === `bone_${item}__forearm_${side}`)) ?? defaultPart;
        };
        for (const part of [this.assets.hands.parts[0], this.assets.hands.parts[1], arm("r"), arm("l")]) {
            const object = part?.object.clone() ?? new THREE.Group();
            object.matrixAutoUpdate = false;
            this.weapon.add(object);
            this.parts.push(object);
        }
    }

    render(renderer: THREE.WebGLRenderer, frame: Frame, matrices: Float32Array, aspect: number) {
        const pose = frame.pose;
        const fov = frame.camera?.fov;
        if (!pose || !fov || pose.viewModelOffset < 0) return;
        this.configure(pose);
        if (this.camera.fov !== fov || this.camera.aspect !== aspect) {
            this.camera.fov = fov;
            this.camera.aspect = aspect;
            this.camera.updateProjectionMatrix();
        }
        this.weapon.matrix.fromArray(matrices, pose.viewModelOffset);
        this.weapon.matrixWorldNeedsUpdate = true;
        for (const [i, part] of this.parts.entries()) {
            part.matrix.fromArray(matrices, pose.viewModelOffset + 16 * (i + 1));
            part.matrixWorldNeedsUpdate = true;
        }
        const autoClear = renderer.autoClear;
        renderer.autoClear = false;
        renderer.clearDepth();
        renderer.render(this.scene, this.camera);
        renderer.autoClear = autoClear;
    }
}

export class Actors {
    private survivor: Survivor;
    private remotes = new Map<number, Survivor>();
    private infected: Infected;

    constructor(private assets: ActorAssets, private scene: THREE.Scene) {
        this.survivor = new Survivor(assets, scene);
        this.infected = new Infected(assets, scene);
    }

    static loadKernel(client: Client, assets: ActorAssets) {
        client.loadRig(0, assets.survivor.bytes);
        client.loadRig(1, assets.infected.bytes);
    }

    render(frame: Frame, matrices: Float32Array, firstPerson: boolean) {
        if (frame.pose)
            this.survivor.update(frame.drawFeet ?? frame.feet, frame.pose.bodyYaw, frame.alive && !firstPerson,
                frame.pose, frame.pose.matrixOffset, matrices);
        const seen = new Set<number>();
        for (const remote of frame.remotes) {
            seen.add(remote.id);
            let person = this.remotes.get(remote.id);
            if (!person) {
                person = new Survivor(this.assets, this.scene);
                this.remotes.set(remote.id, person);
            }
            person.update(remote.position, remote.yaw, remote.alive, remote.pose, remote.matrixOffset, matrices);
        }
        for (const [id, person] of this.remotes) {
            if (seen.has(id)) continue;
            person.remove();
            this.remotes.delete(id);
        }
        this.infected.update(frame, matrices);
    }
}
