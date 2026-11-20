import * as THREE from "three";
import type { Presentation } from "./types";

type EffectObject = THREE.Line | THREE.Mesh;

export class FieldEffects {
    private readonly entries: { object: EffectObject; expires: number }[] = [];
    private readonly flash = new THREE.SphereGeometry(.10, 6, 4);
    private readonly particles = new THREE.SphereGeometry(.06, 5, 4);
    private readonly flashMaterial = new THREE.MeshBasicMaterial({ color: 0xffc36d, toneMapped: false });
    private readonly dirt = new THREE.MeshBasicMaterial({ color: 0xb9ab85 });
    private readonly blood = new THREE.MeshBasicMaterial({ color: 0x7d1414 });
    private readonly tracer = new THREE.LineBasicMaterial({ color: 0xffdca6, transparent: true, opacity: .85, toneMapped: false });

    constructor(private readonly scene: THREE.Scene) {}

    queue(event: Presentation) {
        const point = new THREE.Vector3().fromArray(event.position);
        if (event.event === "trace" && event.end) {
            const end = new THREE.Vector3().fromArray(event.end);
            const start = point.clone().lerp(end, Math.min(1, .5 / Math.max(end.distanceTo(point), .5)));
            const path = new THREE.BufferGeometry().setFromPoints([start, end]);
            this.add(new THREE.Line(path, this.tracer), .065);
            if (event.impact) this.particle(this.dirt, end, .20, 1);
        } else if (event.event === "shot" && !event.suppressed) {
            this.particle(this.flashMaterial, point, .052, .6, this.flash);
        } else if (event.event === "blood") {
            for (let i = 0; i < 4; ++i) {
                const delta = new THREE.Vector3((Math.random() - .5) * .20, Math.random() * .15, (Math.random() - .5) * .20);
                this.particle(this.blood, point.clone().add(delta), .18 + .035 * i, .45 + .25 * i);
            }
        }
    }

    private particle(material: THREE.Material, point: THREE.Vector3, duration: number, scale: number,
                     geometry = this.particles) {
        const mesh = new THREE.Mesh(geometry, material);
        mesh.position.copy(point);
        mesh.scale.setScalar(scale);
        this.add(mesh, duration);
    }

    private add(object: EffectObject, duration: number) {
        if (this.entries.length >= 128) this.remove(0);
        this.scene.add(object);
        this.entries.push({ object, expires: performance.now() + duration * 1000 });
    }

    private remove(index: number) {
        const [{ object }] = this.entries.splice(index, 1);
        this.scene.remove(object);
        if (object instanceof THREE.Line) object.geometry.dispose();
    }

    update() {
        const time = performance.now();
        for (let i = this.entries.length - 1; i >= 0; --i) {
            if (this.entries[i].expires <= time) this.remove(i);
        }
    }

    clear() {
        while (this.entries.length) this.remove(this.entries.length - 1);
    }
}
