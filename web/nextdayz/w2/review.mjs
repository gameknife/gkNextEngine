import * as THREE from "three";
import { GLTFLoader } from "/three/examples/jsm/loaders/GLTFLoader.js";
import { MeshoptDecoder } from "/three/examples/jsm/libs/meshopt_decoder.module.js";
import { OrbitControls } from "/three/examples/jsm/controls/OrbitControls.js";

const element = id => document.getElementById(id);
const viewport = element("viewport");
const status = element("status");
const number = new Intl.NumberFormat("zh-CN");
const categories = {
    playerSafe: ["安全落点", 0x09f5ff], zombie: ["感染者", 0xcf1e29],
    loot: ["物资点", 0xffc536], well: ["水井", 0x5664ff],
    poi: ["据点", 0xe760fa], extraction: ["撤离点", 0x75ff55],
};

const renderer = new THREE.WebGLRenderer({ antialias: true, powerPreference: "high-performance" });
renderer.setPixelRatio(Math.min(window.devicePixelRatio, 1.75));
renderer.toneMapping = THREE.ACESFilmicToneMapping;
viewport.prepend(renderer.domElement);
const scene = new THREE.Scene();
scene.background = new THREE.Color(0xa1b5be);
scene.fog = new THREE.FogExp2(0xa1b5be, 0.00055);
scene.add(new THREE.HemisphereLight(0xf1f5ff, 0xaf9b80, 2.1));
const sun = new THREE.DirectionalLight(0xffecd5, 1.95);
sun.position.set(-250, 360, 280);
scene.add(sun);
const camera = new THREE.PerspectiveCamera(48, 1, .15, 3500);
const orbit = new OrbitControls(camera, renderer.domElement);
orbit.enableDamping = true;
orbit.maxPolarAngle = Math.PI * .485;
orbit.minDistance = 1.5;
orbit.maxDistance = 1900;
const loader = new GLTFLoader().setMeshoptDecoder(MeshoptDecoder);
const resize = () => {
    const { clientWidth, clientHeight } = viewport;
    renderer.setSize(clientWidth, clientHeight, false);
    camera.aspect = clientWidth / clientHeight;
    camera.updateProjectionMatrix();
};
new ResizeObserver(resize).observe(viewport);
renderer.setAnimationLoop(() => {
    orbit.update();
    renderer.render(scene, camera);
});

let manifest;
let current;
let active;
let generation = 0;
function dispose(group) {
    const materials = new Set();
    const textures = new Set();
    group.traverse(node => {
        if (node.isInstancedMesh) node.dispose();
        node.geometry?.dispose();
        if (node.material) materials.add(node.material);
    });
    for (const material of materials) {
        for (const value of Object.values(material)) {
            if (value?.isTexture) textures.add(value);
        }
        material.dispose();
    }
    for (const texture of textures) {
        texture.source.data?.close?.();
        texture.dispose();
    }
    scene.remove(group);
}

async function read(url) {
    const response = await fetch(url);
    if (!response.ok) throw new Error(`${response.status} · ${url}`);
    return response.json();
}

function setView(kind) {
    if (!active) return;
    const { stats, anchors } = active;
    const min = new THREE.Vector3(...stats.bounds[0]);
    const max = new THREE.Vector3(...stats.bounds[1]);
    const center = min.clone().add(max).multiplyScalar(.5);
    const radius = Math.max(max.x - min.x, max.z - min.z) * .5;
    const spawn = anchors.anchors.find(anchor => anchor.type === "playerSafe");
    if (kind === "spawn" && spawn) {
        orbit.target.set(...spawn.position);
        camera.position.copy(orbit.target).add(new THREE.Vector3(-19, 20, 35));
    } else if (kind === "detail") {
        orbit.target.copy(center).setY(min.y + (max.y - min.y) * .19);
        camera.position.copy(orbit.target).add(new THREE.Vector3(-radius * .33, radius * .3, radius * .49));
    } else {
        orbit.target.copy(center).setY(min.y + (max.y - min.y) * .27);
        const distance = radius * (camera.aspect < 1.2 ? 1.43 : 1);
        camera.position.copy(orbit.target).add(new THREE.Vector3(-distance * .85, distance * 1.27, distance * 1.41));
    }
    orbit.update();
}
for (const name of ["overview", "detail", "spawn"]) element(name).onclick = () => setView(name);

function anchorControls(anchors, group) {
    const container = element("anchors");
    container.replaceChildren();
    for (const [kind, [label, color]] of Object.entries(categories)) {
        const subset = anchors.filter(anchor => anchor.type === kind);
        const points = new THREE.BufferGeometry();
        points.setAttribute("position", new THREE.Float32BufferAttribute(
            subset.flatMap(({ position: [x, y, z] }) => [x, y + 2.5, z]), 3));
        const markers = new THREE.Points(points, new THREE.PointsMaterial({
            color, size: 6, sizeAttenuation: false, depthTest: false, toneMapped: false,
        }));
        markers.visible = false;
        markers.renderOrder = 10;
        group.add(markers);
        const row = document.createElement("div");
        row.className = "anchor-row";
        const target = document.createElement("label");
        const checkbox = document.createElement("input");
        checkbox.type = "checkbox";
        checkbox.disabled = subset.length === 0;
        checkbox.onchange = () => { markers.visible = checkbox.checked; };
        target.append(checkbox, label);
        const count = document.createElement("span");
        count.textContent = number.format(subset.length);
        row.append(target, count);
        container.append(row);
    }
}

async function selectMap(id) {
    const ticket = ++generation;
    const bundle = manifest.maps[id];
    current = id;
    active = undefined;
    delete window.nextdayzMapReview;
    for (const button of element("maps").querySelectorAll("button")) {
        button.setAttribute("aria-pressed", String(button.dataset.map === id));
    }
    for (const name of ["overview", "detail", "spawn"]) element(name).disabled = true;
    const previous = scene.getObjectByName("map");
    if (previous) dispose(previous);
    element("metrics").replaceChildren();
    element("anchors").replaceChildren();
    element("map-title").textContent = bundle.map.localName || bundle.map.name;
    element("map-version").textContent = `v${bundle.map.mapVersion} · ${bundle.map.name}`;
    status.textContent = "正在读取地图数据";
    const url = key => "/assets/" + bundle.files[key].path;
    const group = new THREE.Group();
    group.name = "map";
    try {
        const [stats, anchors, env] = await Promise.all([
            read(url("bake.json")), read(url("anchors.json")), read(url("env.json")),
        ]);
        if (ticket !== generation) return;
        let done = 0;
        let next = 0;
        const loaded = new Array(stats.chunks.length);
        status.textContent = `正在加载地图 · 0 / ${stats.chunks.length} 块`;
        let loadError;
        await Promise.all(Array.from({ length: 4 }, async () => {
            while (!loadError && next < loaded.length && ticket === generation) {
                const index = next++;
                try {
                    loaded[index] = (await loader.loadAsync(url(stats.chunks[index].file))).scene;
                } catch (error) {
                    loadError = error;
                    break;
                }
                if (ticket === generation) status.textContent = `正在加载地图 · ${++done} / ${loaded.length} 块`;
            }
        }));
        group.add(...loaded.filter(Boolean));
        if (loadError) throw loadError;
        if (ticket !== generation) {
            dispose(group);
            return;
        }

        // Count the geometry actually decoded by the browser and compute extrema
        // from vertices in world space, including all GPU instances.
        const box = new THREE.Box3();
        const point = new THREE.Vector3();
        const transform = new THREE.Matrix4();
        let triangles = 0;
        let batches = 0;
        group.updateMatrixWorld(true);
        group.traverse(node => {
            if (!node.isMesh) return;
            const count = node.isInstancedMesh ? node.count : 1;
            const attr = node.geometry.attributes.position;
            triangles += (node.geometry.index?.count ?? attr.count) / 3 * count;
            batches++;
            for (let i = 0; i < count; i++) {
                if (node.isInstancedMesh) node.getMatrixAt(i, transform).premultiply(node.matrixWorld);
                else transform.copy(node.matrixWorld);
                for (let j = 0; j < attr.count; j++) {
                    box.expandByPoint(point.fromBufferAttribute(attr, j).applyMatrix4(transform));
                }
            }
        });
        const bounds = [box.min.toArray(), box.max.toArray()];
        const boundError = Math.max(...bounds.flatMap((corner, i) => corner.map((value, j) => Math.abs(value - stats.bounds[i][j]))));
        if (triangles !== stats.desktopTriangles || boundError > .001 || batches !== stats.batches) {
            throw new Error(`解码数据与桌面统计不一致 · ${triangles} · ${boundError} m`);
        }
        let mapBytes = 0;
        let transferBytes = 0;
        for (const file of Object.values(bundle.files)) {
            mapBytes += file.bytes;
            transferBytes += file.gzipBytes || file.bytes;
        }
        active = { stats, anchors, env };
        scene.add(group);
        anchorControls(anchors.anchors, group);
        sun.position.fromArray(env.sun.direction).multiplyScalar(300);
        setView("overview");
        const values = {
            "三角形": number.format(triangles), "分块": number.format(stats.chunks.length),
            "全图渲染批次": number.format(batches), "GPU 实例": number.format(stats.instancedNodes),
            "地图文件": `${(mapBytes / 1e6).toFixed(1)} MB`, "传输体积": `${(transferBytes / 1e6).toFixed(1)} MB`,
            "边界差值": `${(boundError * 1000).toFixed(3)} mm`,
        };
        for (const [label, value] of Object.entries(values)) {
            const row = document.createElement("div");
            const dt = document.createElement("dt");
            const dd = document.createElement("dd");
            dt.textContent = label;
            dd.textContent = value;
            row.append(dt, dd);
            element("metrics").append(row);
        }
        for (const name of ["overview", "detail", "spawn"]) element(name).disabled = false;
        element("spawn").disabled = !anchors.anchors.some(anchor => anchor.type === "playerSafe");
        status.textContent = "";
        window.nextdayzMapReview = { id, stats, bounds, boundError, triangles, batches, mapBytes, transferBytes, anchors, env, renderer };
    } catch (error) {
        dispose(group);
        if (ticket === generation) status.textContent = `地图加载失败 · ${error.message}`;
        console.error(error);
    }
}

async function updateManifest() {
    const next = await read("/assets/manifest.json");
    const updated = JSON.stringify(next.maps) !== JSON.stringify(manifest?.maps);
    if (!updated) return;
    const previous = manifest?.maps[current]?.directory;
    manifest = next;
    const nav = element("maps");
    nav.replaceChildren();
    const ids = Object.keys(next.maps).sort((a, b) => Number(b === "training_ground") - Number(a === "training_ground"));
    for (const id of ids) {
        const button = document.createElement("button");
        button.type = "button";
        button.dataset.map = id;
        button.textContent = next.maps[id].map.localName ?? id;
        button.setAttribute("aria-pressed", String(current === id));
        button.onclick = () => selectMap(id);
        nav.append(button);
    }
    const id = ids.includes(current) ? current : ids[0];
    if (!id) throw new Error("清单中没有已烘焙的地图");
    if (previous !== next.maps[id].directory) void selectMap(id);
}
updateManifest().catch(error => { status.textContent = `资源清单读取失败 · ${error.message}`; });
setInterval(() => updateManifest().catch(console.error), 3000);
