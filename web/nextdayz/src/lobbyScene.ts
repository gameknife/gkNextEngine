import * as THREE from "three";
import type { ActorAssets } from "./actors";

// Lightweight Three.js Helicopter Cabin + live-dressed 3D Survivor backdrop
// for the Web login & safehouse lobby (matching desktop's 3D helicopter cabin).
export class LobbyScene {
    private renderer: THREE.WebGLRenderer;
    private scene = new THREE.Scene();
    private camera = new THREE.PerspectiveCamera(42, 16 / 9, 0.1, 80);
    private survivorRoot = new THREE.Group();
    private gearGroup = new THREE.Group();
    private bodyParts: { name: string; object: THREE.Object3D }[] = [];
    private active = false;
    private rafId = 0;
    private elapsed = 0;
    private lastTime = 0;
    private yaw = -0.34;
    private dragging = false;
    private dragX = 0;
    private gearKey = "";
    private resizeObserver: ResizeObserver;

    constructor(private host: HTMLElement, private assets: ActorAssets) {
        this.renderer = new THREE.WebGLRenderer({ antialias: true, alpha: false, powerPreference: "default" });
        this.renderer.outputColorSpace = THREE.SRGBColorSpace;
        this.renderer.toneMapping = THREE.ACESFilmicToneMapping;
        this.renderer.toneMappingExposure = 1.05;
        this.renderer.setPixelRatio(Math.min(window.devicePixelRatio || 1, 1.75));
        this.renderer.domElement.className = "lobby-canvas";
        host.replaceChildren(this.renderer.domElement);

        this.scene.background = new THREE.Color(0x0c0f0d);
        this.scene.fog = new THREE.FogExp2(0x111613, 0.095);

        this.buildCabinSet();
        this.buildLights();
        this.buildSurvivor();

        this.camera.position.set(-0.55, 1.38, 3.05);
        this.camera.lookAt(0.28, 1.04, 0.0);

        this.resizeObserver = new ResizeObserver(() => this.resize());
        this.resizeObserver.observe(host);
        this.resize();

        const dom = this.renderer.domElement;
        dom.addEventListener("pointerdown", e => {
            this.dragging = true;
            this.dragX = e.clientX;
        });
        window.addEventListener("pointermove", e => {
            if (!this.dragging || !this.active) return;
            this.yaw += (e.clientX - this.dragX) * 0.008;
            this.dragX = e.clientX;
        });
        window.addEventListener("pointerup", () => { this.dragging = false; });
    }

    private buildLights() {
        const hemi = new THREE.HemisphereLight(0x88a4b2, 0x1b1e18, 0.85);
        this.scene.add(hemi);

        // Warm tungsten overhead cabin lamp (matches desktop cabin mood)
        const cabinLamp = new THREE.PointLight(0xffb866, 5.2, 11, 1.7);
        cabinLamp.position.set(0.45, 2.35, 0.25);
        this.scene.add(cabinLamp);

        // Cool misty daylight entering through open port/starboard hatch
        const hatchRim = new THREE.DirectionalLight(0x9bc0d4, 1.55);
        hatchRim.position.set(-3.2, 1.6, -1.8);
        this.scene.add(hatchRim);

        // Subtle warm front catchlight for readability
        const fill = new THREE.DirectionalLight(0xf3dfc2, 0.55);
        fill.position.set(1.4, 1.4, 3.0);
        this.scene.add(fill);
    }

    private buildCabinSet() {
        const hullMat = new THREE.MeshStandardMaterial({ color: 0x222923, roughness: 0.86, metalness: 0.22 });
        const ribMat = new THREE.MeshStandardMaterial({ color: 0x181e19, roughness: 0.78, metalness: 0.35 });
        const deckMat = new THREE.MeshStandardMaterial({ color: 0x161a17, roughness: 0.92, metalness: 0.2 });
        const crateMat = new THREE.MeshStandardMaterial({ color: 0x364133, roughness: 0.82, metalness: 0.1 });
        const lampGlow = new THREE.MeshBasicMaterial({ color: 0xffcb78 });

        const cabin = new THREE.Group();

        // Deck floor
        const floor = new THREE.Mesh(new THREE.BoxGeometry(6.4, 0.12, 4.6), deckMat);
        floor.position.set(0, -0.06, 0);
        cabin.add(floor);

        // Rear bulkhead & side panels
        const backWall = new THREE.Mesh(new THREE.BoxGeometry(6.4, 2.75, 0.14), hullMat);
        backWall.position.set(0, 1.35, -1.45);
        cabin.add(backWall);

        const rightWall = new THREE.Mesh(new THREE.BoxGeometry(0.16, 2.75, 4.4), hullMat);
        rightWall.position.set(2.45, 1.35, 0.2);
        cabin.add(rightWall);

        const ceiling = new THREE.Mesh(new THREE.BoxGeometry(6.4, 0.14, 4.4), hullMat);
        ceiling.position.set(0, 2.66, 0.2);
        cabin.add(ceiling);

        // Curved structural fuselage ribs along the cabin wall
        for (let i = -3; i <= 3; ++i) {
            const x = i * 0.78;
            const post = new THREE.Mesh(new THREE.BoxGeometry(0.08, 2.65, 0.12), ribMat);
            post.position.set(x, 1.32, -1.36);
            cabin.add(post);

            const beam = new THREE.Mesh(new THREE.BoxGeometry(0.08, 0.1, 4.2), ribMat);
            beam.position.set(x, 2.58, 0.2);
            cabin.add(beam);
        }

        // Open hatch misty backdrop panel on left-center
        const hatchSky = new THREE.Mesh(
            new THREE.PlaneGeometry(1.8, 1.75),
            new THREE.MeshBasicMaterial({ color: 0x7a8d94 })
        );
        hatchSky.position.set(-1.35, 1.28, -1.37);
        cabin.add(hatchSky);

        // Overhead cabin lamp fixture
        const lamp = new THREE.Mesh(new THREE.SphereGeometry(0.085, 12, 10), lampGlow);
        lamp.position.set(0.45, 2.52, 0.25);
        cabin.add(lamp);

        // Side troop bench & tactical supply crates
        const bench = new THREE.Mesh(new THREE.BoxGeometry(2.5, 0.08, 0.56), ribMat);
        bench.position.set(0.7, 0.48, -0.95);
        cabin.add(bench);

        const crateA = new THREE.Mesh(new THREE.BoxGeometry(0.78, 0.46, 0.52), crateMat);
        crateA.position.set(-1.65, 0.23, 0.35);
        crateA.rotation.y = 0.18;
        cabin.add(crateA);

        const crateB = new THREE.Mesh(new THREE.BoxGeometry(0.54, 0.34, 0.42), crateMat);
        crateB.position.set(-1.58, 0.63, 0.28);
        crateB.rotation.y = -0.1;
        cabin.add(crateB);

        this.scene.add(cabin);
    }

    private buildSurvivor() {
        this.survivorRoot.position.set(0.55, 0.0, -0.15);
        this.survivorRoot.rotation.y = this.yaw;

        // Place survivor rig parts in their authored neutral pose (rig meshes carry baked offsets).
        for (const part of this.assets.survivor.parts) {
            const clone = part.object.clone();
            this.survivorRoot.add(clone);
            this.bodyParts.push({ name: part.name, object: clone });
        }
        this.survivorRoot.add(this.gearGroup);
        this.scene.add(this.survivorRoot);
    }

    // Updates visible clothing & primary weapon preview when the player edits their loadout.
    setLoadout(itemIds: string[]) {
        const key = [...new Set(itemIds)].sort().join(",");
        if (key === this.gearKey) return;
        this.gearKey = key;

        this.gearGroup.clear();
        const owned = new Set(itemIds);

        // Match clothing parts whose prefix matches an owned item id (`bone_<itemId>__<slot>`).
        let hasTorsoCoat = false;
        let hasTrousers = false;
        for (const part of this.assets.clothes.parts) {
            const sep = part.name.indexOf("__");
            if (sep < 0) continue;
            const id = part.name.slice(5, sep);
            if (!owned.has(id)) continue;
            const dest = part.name.slice(sep + 2);
            if (dest === "torso") hasTorsoCoat = true;
            if (dest === "pelvis" || dest === "thigh_l") hasTrousers = true;
            this.gearGroup.add(part.object.clone());
        }

        for (const part of this.bodyParts) {
            const upper = part.name === "bone_torso" || /bone_(upperarm|forearm)_/.test(part.name);
            const lower = part.name === "bone_pelvis" || /bone_(thigh|calf|foot)_/.test(part.name);
            part.object.visible = !(hasTorsoCoat && upper) && !(hasTrousers && lower);
        }

        // Display first carried firearm slung in front/side for hero silhouette.
        for (const id of itemIds) {
            const weapon = this.assets.weapon.parts.find(p => p.name === "bone_wpn_" + id);
            if (!weapon) continue;
            const model = weapon.object.clone();
            model.position.set(0.18, 1.02, 0.24);
            model.rotation.set(0.14, 1.25, -0.32);
            this.gearGroup.add(model);
            break;
        }
    }

    setActive(active: boolean) {
        if (this.active === active) return;
        this.active = active;
        if (active) {
            this.resize();
            this.lastTime = performance.now();
            this.loop(this.lastTime);
        } else if (this.rafId) {
            cancelAnimationFrame(this.rafId);
            this.rafId = 0;
        }
    }

    private resize() {
        const w = Math.max(1, this.host.clientWidth);
        const h = Math.max(1, this.host.clientHeight);
        this.camera.aspect = w / h;
        this.camera.updateProjectionMatrix();
        this.renderer.setSize(w, h, false);
    }

    private loop = (now: number) => {
        if (!this.active) return;
        const dt = Math.min((now - this.lastTime) / 1000, 0.1);
        this.lastTime = now;
        this.elapsed += dt;

        // Subtle idle breathing & gentle helicopter cabin sway
        this.survivorRoot.rotation.y = this.yaw + Math.sin(this.elapsed * 0.55) * 0.04;
        this.survivorRoot.position.y = Math.sin(this.elapsed * 1.6) * 0.008;
        this.camera.position.x = -0.55 + Math.sin(this.elapsed * 0.42) * 0.03;
        this.camera.position.y = 1.38 + Math.cos(this.elapsed * 0.65) * 0.015;
        this.camera.lookAt(0.28, 1.04, 0.0);

        this.renderer.render(this.scene, this.camera);
        this.rafId = requestAnimationFrame(this.loop);
    };

    destroy() {
        this.setActive(false);
        this.resizeObserver.disconnect();
        this.renderer.dispose();
    }
}
