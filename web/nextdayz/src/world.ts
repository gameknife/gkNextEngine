import * as THREE from "three";
import { CSM } from "three/addons/csm/CSM.js";
import { Sky } from "three/addons/objects/Sky.js";
import { dispose, type Level } from "./assets";
import { Actors, GroundModels, ViewModel, type ActorAssets } from "./actors";
import { matches, settings, tr, type Binding } from "./settings";
import { FieldEffects } from "./fieldEffects";
import type { Clock, Frame, GroundItem, Presentation } from "./types";

type View = "survivor" | "overview" | "detail";
export type Quality = "balanced" | "shadows";

export class World {
    private scene = new THREE.Scene();
    private camera = new THREE.PerspectiveCamera(62, 1, 0.18, 5000);
    private renderer = new THREE.WebGLRenderer({ antialias: true, powerPreference: "high-performance" });
    private sun = new THREE.DirectionalLight(0xffedd6, 2.2);
    private hemisphere = new THREE.HemisphereLight(0xf0f4ff, 0x8a7859, 1.5);
    private fog = new THREE.FogExp2(0xa0afb6, 0.001);
    private sky = new Sky();
    private csm?: CSM;
    private quality: Quality = "balanced";
    private world?: Level;
    private yaw = 0;
    private pitch = 0;
    private keys = new Set<string>();
    private position = new THREE.Vector3();
    private entered = false;
    private lookX = 0;
    private lookY = 0;
    private actions = 0;
    private aiming = false;
    private firing = false;
    private cameraLook = new THREE.Vector3();
    private view: View = "survivor";
    private actors: Actors;
    private viewModel: ViewModel;
    private ground: GroundModels;
    private effects: FieldEffects;
    private drawCount = 0;
    private triangleCount = 0;
    private pointerPending = false;

    constructor(private host: HTMLElement, assets: ActorAssets, private lockChanged: (locked: boolean) => void) {
        const { renderer, scene } = this;
        renderer.info.autoReset = false;
        renderer.outputColorSpace = THREE.SRGBColorSpace;
        renderer.toneMapping = THREE.ACESFilmicToneMapping;
        renderer.shadowMap.type = THREE.PCFShadowMap;
        renderer.domElement.setAttribute("aria-label", tr("Game view"));
        renderer.domElement.tabIndex = 0;
        host.append(renderer.domElement);
        this.actors = new Actors(assets, scene);
        this.viewModel = new ViewModel(assets);
        this.ground = new GroundModels(assets, scene);
        this.effects = new FieldEffects(scene);
        scene.fog = this.fog;
        scene.add(this.hemisphere, this.sun, this.sky);
        this.sky.scale.setScalar(2300);
        this.sky.frustumCulled = false;
        this.sky.material.uniforms.turbidity.value = 4;
        this.sky.material.uniforms.rayleigh.value = 1.9;
        this.sky.material.uniforms.mieCoefficient.value = 0.012;

        new ResizeObserver(() => this.resize()).observe(host);
        renderer.domElement.addEventListener("click", () => void this.lock());
        document.addEventListener("pointerlockchange", () => {
            if (!this.locked) this.resetControls();
            this.lockChanged(this.locked);
        });
        document.addEventListener("mousemove", event => {
            if (!this.locked) return;
            this.lookX += event.movementX;
            this.lookY += event.movementY;
        });
        const held: Binding[] = ["forward", "back", "left", "right", "walk", "sprint", "freeLook", "jump"];
        const actions: [Binding, number][] = [
            ["jump", 9], ["crouch", 10], ["view", 11], ["reload", 14],
            ["slot1", 15], ["slot2", 16], ["slot3", 17], ["previous", 18],
        ];
        window.addEventListener("keydown", event => {
            if (!this.locked || event.ctrlKey || event.metaKey) return;
            const action = actions.find(([name]) => matches(name, event.code));
            const press = held.some(name => matches(name, event.code));
            if (!action && !press) return;
            event.preventDefault();
            if (press) this.keys.add(event.code);
            if (action && !event.repeat) this.actions |= 1 << action[1];
        });
        window.addEventListener("keyup", event => this.keys.delete(event.code));
        window.addEventListener("blur", () => this.resetControls());
        renderer.domElement.addEventListener("contextmenu", event => event.preventDefault());
        renderer.domElement.addEventListener("mousedown", event => {
            if (!this.locked) return;
            if (event.button === 2) this.aiming = true;
            if (event.button === 0) {
                this.firing = true;
                this.actions |= 1 << 13;
            }
        });
        window.addEventListener("mouseup", event => {
            if (event.button === 2) this.aiming = false;
            if (event.button === 0) this.firing = false;
        });
        window.addEventListener("beforeunload", event => {
            if (!this.entered) return;
            event.preventDefault();
            event.returnValue = "";
        });
    }

    get locked() { return document.pointerLockElement === this.renderer.domElement; }
    get drawCalls() { return this.drawCount; }
    get triangles() { return this.triangleCount; }

    mapImage() {
        if (!this.world) return undefined;
        const [min, max] = this.world.bounds;
        const edge = Math.max(max[0] - min[0], max[2] - min[2]) / 2;
        const canvas = this.renderer.domElement;
        const aspect = canvas.width / canvas.height;
        const camera = new THREE.OrthographicCamera(-edge * aspect, edge * aspect, edge, -edge, .1, 4000);
        camera.position.set((min[0] + max[0]) / 2, max[1] + 650, (min[2] + max[2]) / 2);
        camera.up.set(0, 0, -1);
        camera.lookAt(camera.position.x, min[1], camera.position.z);
        const fog = this.scene.fog;
        const shadows = this.renderer.shadowMap.enabled;
        this.renderer.shadowMap.enabled = false;
        this.scene.fog = null;
        this.sky.visible = false;
        const background = this.scene.background;
        this.scene.background = new THREE.Color(0x233322);
        this.renderer.render(this.scene, camera);
        this.scene.background = background;
        const preview = document.createElement("canvas");
        preview.width = preview.height = 600;
        const side = Math.min(canvas.width, canvas.height);
        preview.getContext("2d")?.drawImage(canvas, (canvas.width - side) / 2, (canvas.height - side) / 2,
            side, side, 0, 0, preview.width, preview.height);
        const image = new Image();
        image.src = preview.toDataURL();
        this.scene.fog = fog;
        this.sky.visible = true;
        this.renderer.shadowMap.enabled = shadows;
        return image;
    }

    present(event: Presentation) { if (this.entered) this.effects.queue(event); }
    groundItems(items: GroundItem[]) { this.ground.update(items); }

    private resetControls() {
        this.keys.clear();
        this.lookX = this.lookY = this.actions = 0;
        this.aiming = this.firing = false;
    }

    sendControls(send: (flags: number, dx: number, dy: number) => void) {
        let flags = this.actions;
        const held: Binding[] = ["forward", "back", "left", "right", "walk", "sprint", "freeLook", "jump"];
        held.forEach((key, index) => {
            if ([...this.keys].some(code => matches(key, code))) flags |= 1 << index;
        });
        if (this.aiming) flags |= 1 << 8;
        if (this.firing) flags |= 1 << 12;
        // Without pointer lock a panel, the map or a menu is open: the survivor stands still
        // and lowers the weapon, as on desktop.
        if (!this.locked) flags |= 1 << 19;
        send(flags, this.lookX * settings.sensitivity, this.lookY * settings.sensitivity);
        this.lookX = this.lookY = this.actions = 0;
    }

    async lock() {
        if (!this.entered || this.locked || this.pointerPending) return;
        this.resetControls();
        this.pointerPending = true;
        this.view = "survivor";
        try {
            try {
                await this.renderer.domElement.requestPointerLock({ unadjustedMovement: true });
            } catch {
                await this.renderer.domElement.requestPointerLock();
            }
        } catch {
            this.lockChanged(false);
        } finally {
            this.pointerPending = false;
        }
    }

    release() {
        if (this.locked) document.exitPointerLock();
    }

    setLevel(level: Level) {
        const old = this.world;
        if (old && old !== level) {
            this.scene.remove(old.group);
            dispose(old.group);
        }
        this.world = level;
        this.scene.add(level.group);
        this.configureShadows();
    }

    enter(frame: Frame) {
        this.entered = true;
        this.setView("survivor", frame);
        this.resize();
    }

    leave() {
        this.entered = false;
        this.release();
        this.ground.clear();
        this.effects.clear();
    }

    setView(view: View, frame: Frame) {
        this.view = view;
        if (view !== "survivor" && this.world) {
            const min = new THREE.Vector3(...this.world.bounds[0]);
            const max = new THREE.Vector3(...this.world.bounds[1]);
            const center = min.clone().add(max).multiplyScalar(0.5);
            center.y = min.y + (max.y - min.y) * .24;
            const radius = Math.max(max.x - min.x, max.z - min.z) / 2;
            const offset = view === "detail" ? new THREE.Vector3(-.33, .3, .49) : new THREE.Vector3(-.95, 1.43, 1.85);
            this.position.copy(center).add(offset.multiplyScalar(radius));
            this.camera.position.copy(this.position);
            this.camera.lookAt(center);
            const rotation = new THREE.Euler().setFromQuaternion(this.camera.quaternion, "YXZ");
            this.yaw = rotation.y;
            this.pitch = rotation.x;
        } else {
            this.position.fromArray(frame.eye);
            this.yaw = Math.atan2(-frame.forward[0], -frame.forward[2]);
            this.pitch = Math.asin(Math.max(-1, Math.min(1, frame.forward[1])));
        }
    }

    setQuality(quality: Quality) {
        if (quality === this.quality) return;
        this.quality = quality;
        this.configureShadows();
        this.resize();
    }

    private configureShadows() {
        if (this.csm) {
            this.csm.remove();
            this.csm.dispose();
            this.csm = undefined;
        }
        const shadows = this.quality === "shadows";
        this.renderer.shadowMap.enabled = shadows;
        this.sun.visible = !shadows;
        if (shadows) {
            this.csm = new CSM({
                camera: this.camera, parent: this.scene, cascades: 3, maxFar: 165,
                mode: "practical", shadowMapSize: 1024, lightDirection: new THREE.Vector3(-.5, -1, .5).normalize(),
                lightMargin: 40, shadowBias: -0.00015, lightIntensity: 2.0,
            });
            this.csm.fade = true;
        }
        this.world?.group.traverse(node => {
            const mesh = node as THREE.Mesh;
            if (!mesh.isMesh) return;
            mesh.castShadow = mesh.receiveShadow = shadows;
            if (this.csm) {
                for (const material of Array.isArray(mesh.material) ? mesh.material : [mesh.material]) {
                    this.csm.setupMaterial(material);
                }
            }
        });
    }

    private resize() {
        const { clientWidth: width, clientHeight: height } = this.host;
        if (width <= 0 || height <= 0) return;
        this.camera.aspect = width / height;
        this.camera.updateProjectionMatrix();
        this.renderer.setPixelRatio(Math.min(devicePixelRatio, this.quality === "shadows" ? 1.5 : 1));
        this.renderer.setSize(width, height, false);
        this.csm?.updateFrustums();
    }

    private light(clock: Clock) {
        const azimuth = clock.sunRotation * Math.PI;
        const elevation = this.world?.sunElevation ?? 0.65;
        const direction = new THREE.Vector3(
            Math.sin(azimuth) * Math.cos(elevation), Math.sin(elevation), Math.cos(azimuth) * Math.cos(elevation),
        );
        const weather = clock.weather;
        const day = THREE.MathUtils.clamp(clock.daylight, 0, 1);
        const dawn = Math.max(0, 1 - day * 1.8);
        const cloudy = weather !== "Clear";
        this.sun.position.copy(direction).multiplyScalar(180);
        this.sun.color.fromArray(clock.sunColor);
        this.sun.intensity = clock.hasSun ? Math.min(3, clock.sunIntensity / 200) : 0.05;
        this.hemisphere.color.fromArray(clock.skyColor);
        this.hemisphere.intensity = Math.max(.14, clock.skyIntensity / 64);
        const altitude = Math.max(0, this.position.y - (this.world?.bounds[0][1] ?? 0));
        this.fog.density = Math.min(.008, clock.fogDensity * .13 / (1 + altitude / 180));
        const fogBase = new THREE.Color(cloudy ? 0xafbac2 : 0xa5bac6);
        const fogLight = new THREE.Color().fromArray(clock.fogColor).multiplyScalar(1 / Math.max(1, ...clock.fogColor));
        this.fog.color.copy(fogBase).lerp(fogLight, .16 * day).lerp(new THREE.Color(0x122337), dawn * .9);
        const sunPosition = this.sky.material.uniforms.sunPosition.value;
        sunPosition.copy(direction).setY(direction.y - 0.95 * (1 - day)).multiplyScalar(450000);
        this.sky.material.uniforms.rayleigh.value = 1.6 + (1 - day) * .8;
        this.sky.material.uniforms.turbidity.value = weather === "Fog" ? 14 : cloudy ? 8 : 4;
        if (this.csm) {
            this.csm.lightDirection.copy(direction).negate();
            for (const light of this.csm.lights) {
                light.color.copy(this.sun.color);
                light.intensity = this.sun.intensity;
            }
        }
    }

    render(frame: Frame, matrices: Float32Array, clock = frame.clock) {
        if (!this.entered || !this.world) return;
        const usePlayer = this.view === "survivor" && frame.camera;
        if (usePlayer) {
            const view = frame.camera!;
            this.camera.position.fromArray(view.position);
            this.camera.up.fromArray(view.up);
            this.cameraLook.fromArray(view.forward).add(this.camera.position);
            this.camera.lookAt(this.cameraLook);
        } else {
            this.camera.up.set(0, 1, 0);
            this.camera.position.copy(this.position);
            this.camera.rotation.set(this.pitch, this.yaw, 0, "YXZ");
        }
        const fov = usePlayer ? frame.camera!.fov : 62;
        if (fov !== this.camera.fov) {
            this.camera.fov = fov;
            this.camera.updateProjectionMatrix();
        }
        this.sky.position.copy(this.camera.position);
        this.light(clock);

        this.effects.update();
        this.actors.render(frame, matrices, Boolean(usePlayer && frame.camera?.firstPerson));
        this.camera.updateMatrixWorld();
        this.csm?.update();
        this.renderer.info.reset();
        this.renderer.render(this.scene, this.camera);
        if (usePlayer && frame.camera?.firstPerson)
            this.viewModel.render(this.renderer, frame, matrices, this.camera.aspect);
        this.drawCount = this.renderer.info.render.calls;
        this.triangleCount = this.renderer.info.render.triangles;
    }
}
