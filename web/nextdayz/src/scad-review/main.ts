import * as THREE from "three";
import { OrbitControls } from "three/addons/controls/OrbitControls.js";
import type { ProbeMessage, ProbeMetrics } from "./probe-worker";
import "./style.css";

const canvas = document.querySelector<HTMLCanvasElement>("#scene")!;
const status = document.querySelector<HTMLElement>("#status")!;
const regenerate = document.querySelector<HTMLButtonElement>("#regenerate")!;
const overview = document.querySelector<HTMLButtonElement>("#overview")!;
const sign = document.querySelector<HTMLButtonElement>("#sign")!;

function showError(message: string) {
    status.textContent = message;
    status.dataset.state = "error";
}

function setText(id: string, text: string) {
    document.getElementById(id)!.textContent = text;
}

function showMetrics(metrics: ProbeMetrics) {
    const mb = (bytes: number) => bytes ? `${(bytes / 1e6).toFixed(2)} MB` : "未记录";
    setText("evaluator-bytes", mb(metrics.evaluatorBytes));
    setText("source-bytes", mb(metrics.sourceBytes));
    setText("evaluate-ms", `${(metrics.evaluateMs / 1000).toFixed(2)} s`);
    setText("wasm-memory", `${(metrics.wasmMemoryBytes / 1024 ** 2).toFixed(1)} MiB`);
    setText("triangles", metrics.triangleCount.toLocaleString("zh-CN"));
    document.getElementById("diagnostics")!.hidden = metrics.warningCount === 0;
    setText("warning-count", String(metrics.warningCount));
    setText("warnings", metrics.warnings.join("\n"));
}

function setup() {
    const renderer = new THREE.WebGLRenderer({ canvas, antialias: true });
    renderer.setPixelRatio(Math.min(devicePixelRatio, 2));
    const scene = new THREE.Scene();
    scene.background = new THREE.Color(0x9fb3b5);
    const camera = new THREE.PerspectiveCamera(45, 1, 0.15, 250);
    const controls = new OrbitControls(camera, canvas);
    controls.minDistance = 1;
    controls.maxDistance = 110;
    controls.maxPolarAngle = Math.PI / 2 + 0.15;
    scene.add(new THREE.HemisphereLight(0xe6f4ff, 0x78806a, 2.05));
    const sun = new THREE.DirectionalLight(0xffe2b6, 2.35);
    sun.position.set(-26, 55, 36);
    scene.add(sun);

    const material = new THREE.MeshStandardMaterial({ vertexColors: true, roughness: 0.86 });
    // The road ribbon shares its height with the concrete gate apron.
    const roadMaterial = material.clone();
    roadMaterial.polygonOffset = true;
    roadMaterial.polygonOffsetFactor = 1;
    roadMaterial.polygonOffsetUnits = 4;
    const mesh = new THREE.Mesh(new THREE.BufferGeometry(), [material, roadMaterial]);
    const gate = new THREE.Vector3();
    scene.add(mesh);

    function render() {
        renderer.render(scene, camera);
    }

    function showView(close: boolean) {
        if (close) {
            controls.target.set(gate.x, gate.y + 3.5, gate.z - 8);
            camera.position.set(gate.x, gate.y + 4.1, gate.z + 6);
        } else {
            controls.target.set(gate.x, gate.y + 1.5, gate.z - 2);
            camera.position.set(gate.x + 23, gate.y + 26, gate.z + 40);
        }
        controls.update();
        render();
    }

    controls.addEventListener("change", render);
    overview.addEventListener("click", () => showView(false));
    sign.addEventListener("click", () => showView(true));
    new ResizeObserver(() => {
        renderer.setSize(canvas.clientWidth, canvas.clientHeight, false);
        camera.aspect = canvas.clientWidth / Math.max(1, canvas.clientHeight);
        camera.updateProjectionMatrix();
        render();
    }).observe(canvas);

    function generate() {
        regenerate.disabled = true;
        status.textContent = "读取 SCAD 求值器与源文件…";
        status.dataset.state = "loading";
        const worker = new Worker(new URL("./probe-worker.ts", import.meta.url), { type: "module" });
        const started = performance.now();
        const finish = () => {
            worker.terminate();
            regenerate.disabled = false;
        };
        worker.onerror = event => {
            showError(event.message || "无法加载 SCAD Worker");
            finish();
        };
        worker.onmessage = ({ data }: MessageEvent<ProbeMessage>) => {
            if (data.status === "evaluating") {
                status.textContent = "正在生成地形、大门和标牌…";
                return;
            }
            if (data.status === "error") {
                showError(data.message);
                finish();
                return;
            }

            const buffer = new THREE.InterleavedBuffer(data.vertices, 9);
            const geometry = new THREE.BufferGeometry();
            geometry.setAttribute("position", new THREE.InterleavedBufferAttribute(buffer, 3, 0));
            geometry.setAttribute("normal", new THREE.InterleavedBufferAttribute(buffer, 3, 3));
            geometry.setAttribute("color", new THREE.InterleavedBufferAttribute(buffer, 3, 6));
            geometry.addGroup(0, data.decalStart, 0);
            geometry.addGroup(data.decalStart, buffer.count - data.decalStart, 1);
            mesh.geometry.dispose();
            mesh.geometry = geometry;
            gate.fromArray(data.gate);
            showView(false);
            showMetrics(data.metrics);
            status.textContent = "场景已现场生成";
            status.dataset.state = "ready";
            status.dataset.metrics = JSON.stringify({ ...data.metrics, firstRenderMs: performance.now() - started });
            overview.disabled = sign.disabled = false;
            finish();
        };
    }

    regenerate.addEventListener("click", generate);
    generate();
}

try {
    setup();
} catch (error) {
    showError(error instanceof Error ? error.message : String(error));
}
