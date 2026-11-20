import * as THREE from 'three'
import { GLTFLoader } from 'three/addons/loaders/GLTFLoader.js'
import { testRtc } from './rtc.mjs'

const status = document.querySelector('#scene-status')
const controls = document.querySelector('#scene-controls')
const frameStats = document.querySelector('#frame-stats')
const bakeStats = document.querySelector('#bake-stats')
const canvas = document.querySelector('canvas')
const maps = document.querySelector('#maps')
const keys = new Set()
let renderer
let jolt
let output
let current
let overview = false
let angle = 0
let accumulator = 0
let lastFrame
let sampleStart = 0
let sampleFrames = 0

async function fetchFile(path) {
  const response = await fetch(path)
  if (!response.ok) throw Error(`${path} (${response.status})`)
  return response
}

function buildCollision(buffer, spawn) {
  const header = new DataView(buffer)
  const magic = header.getUint32(0, true)
  const vertices = header.getUint32(4, true)
  const triangles = header.getUint32(8, true)
  if (magic !== 0x3043444e || buffer.byteLength !== 12 + (vertices + triangles) * 12) {
    throw Error('碰撞网格格式无效')
  }
  const bytes = new Uint8Array(buffer, 12)
  const ptr = jolt._malloc(bytes.byteLength)
  if (!ptr) throw Error('WASM 碰撞缓冲分配失败')
  try {
    jolt.HEAPU8.set(bytes, ptr)
    const start = performance.now()
    if (!jolt._BuildCollision(ptr, vertices, ptr + vertices * 12, triangles, ...spawn)) {
      throw Error('Jolt 碰撞体构建失败')
    }
    return performance.now() - start
  } finally {
    jolt._free(ptr)
  }
}

const camera = new THREE.PerspectiveCamera(58, 1, 0.15, 2200)
const scene = new THREE.Scene()
scene.background = new THREE.Color(0xa5bac1)
scene.fog = new THREE.FogExp2(scene.background, 0.0016)
scene.add(new THREE.HemisphereLight(0xf2f9ff, 0x9c9477, 1.8))
const sun = new THREE.DirectionalLight(0xffebc8, 2.25)
sun.position.set(-250, 400, 100)
scene.add(sun)
const marker = new THREE.Mesh(new THREE.CapsuleGeometry(0.3, 1.1, 5, 12),
  new THREE.MeshStandardMaterial({ color: 0xf2d941, roughness: 0.65 }))
marker.visible = false
scene.add(marker)
const position = new THREE.Vector3()
const focus = new THREE.Vector3()

function reset() {
  overview = false
  document.querySelector('#overview').textContent = '俯瞰'
  angle = current.stats.source.includes('training_ground') ? 0 : Math.PI / 2
  if (output) {
    jolt._ResetCapsule(...current.stats.spawns[0])
    jolt._StepCapsule(0, 0, 1 / 60, output)
  }
}

function render(time) {
  requestAnimationFrame(render)
  const dt = Math.min((time - (lastFrame ?? time)) / 1000, 0.05)
  lastFrame = time
  if (!current) return
  const width = canvas.clientWidth
  const height = canvas.clientHeight
  if (canvas.width !== width || canvas.height !== height) {
    renderer.setSize(width, height, false)
    camera.aspect = width / height
    camera.updateProjectionMatrix()
  }

  if (overview) {
    const [min, max] = current.stats.bounds
    const x = (min[0] + max[0]) / 2
    const z = (min[2] + max[2]) / 2
    const size = Math.max(max[0] - min[0], max[2] - min[2])
    camera.position.set(x - size * 0.45, max[1] + size * 0.54, z + size * 0.58)
    camera.lookAt(x, (min[1] + max[1]) / 2, z)
  } else {
    angle += dt * 1.7 * (Number(keys.has('KeyQ')) - Number(keys.has('KeyE')))
    const forward = Number(keys.has('KeyW')) - Number(keys.has('KeyS'))
    const side = Number(keys.has('KeyD')) - Number(keys.has('KeyA'))
    const scale = 5.5 / (Math.hypot(forward, side) || 1)
    const dx = (side * Math.cos(angle) - forward * Math.sin(angle)) * scale
    const dz = (-forward * Math.cos(angle) - side * Math.sin(angle)) * scale
    accumulator += dt
    while (accumulator >= 1 / 60) {
      jolt._StepCapsule(dx, dz, 1 / 60, output)
      accumulator -= 1 / 60
    }
    position.fromArray(jolt.HEAPF32, output / 4)
    marker.position.copy(position).y += 0.85
    camera.position.set(
      position.x + 8 * Math.sin(angle), position.y + 3.8, position.z + 8 * Math.cos(angle),
    )
    focus.set(position.x - 12 * Math.sin(angle), position.y + 2.2, position.z - 12 * Math.cos(angle))
    camera.lookAt(focus)
  }
  renderer.render(scene, camera)

  sampleFrames++
  if (time - sampleStart >= 1000) {
    const frameMs = (time - sampleStart) / sampleFrames
    const { calls, triangles } = renderer.info.render
    frameStats.textContent = `${(1000 / frameMs).toFixed(0)} FPS · ${frameMs.toFixed(1)} ms · ${calls} 次绘制`
    window.w0Metrics = { ...current.metrics, frameMs, calls, visibleTriangles: triangles, position: position.toArray() }
    sampleFrames = 0
    sampleStart = time
  }
}

async function load(id) {
  const submit = maps.querySelector('button')
  submit.disabled = true
  status.hidden = false
  status.textContent = '正在下载场景及碰撞数据…'
  try {
    if (!renderer) {
      renderer = new THREE.WebGLRenderer({ canvas, antialias: true })
      renderer.toneMapping = THREE.ACESFilmicToneMapping
      requestAnimationFrame(render)
    }
    if (!jolt) {
      const { default: createJolt } = await import('/jolt/collision.mjs')
      jolt = await createJolt()
      output = jolt._malloc(12)
      if (!output) throw Error('WASM 位置缓冲分配失败')
    }

    const base = `/maps/${id}/`
    const start = performance.now()
    const [stats, glb, collision] = await Promise.all([
      fetchFile(base + 'bake.json').then(response => response.json()),
      fetchFile(base + 'scene.glb').then(response => response.arrayBuffer()),
      fetchFile(base + 'collision.bin').then(response => response.arrayBuffer()),
    ])
    const downloadMs = performance.now() - start
    const { scene: map } = await new GLTFLoader().parseAsync(glb, base)
    const parseMs = performance.now() - start - downloadMs
    const cookMs = buildCollision(collision, stats.spawns[0])
    if (current) {
      scene.remove(current.map)
      const materials = new Set()
      current.map.traverse(node => {
        if (node.geometry) node.geometry.dispose()
        if (node.material) materials.add(node.material)
      })
      for (const material of materials) material.dispose()
    }

    scene.add(map)
    marker.visible = true
    current = { map, stats, metrics: { id, downloadMs, parseMs, cookMs, readyMs: performance.now() - start } }
    reset()
    sampleFrames = 0
    sampleStart = performance.now()
    controls.hidden = false
    status.hidden = true
    const mb = bytes => (bytes / 1e6).toFixed(1) + ' MB'
    bakeStats.replaceChildren(...Object.entries({
      三角形: stats.triangles.toLocaleString(),
      GLB: mb(stats.glbBytes),
      碰撞网格: mb(stats.collisionBytes),
      下载与解析: `${Math.round(downloadMs + parseMs)} ms`,
      'Jolt 建体': `${Math.round(cookMs)} ms`,
    }).map(([label, value]) => {
      const group = document.createElement('div')
      const dt = document.createElement('dt')
      const dd = document.createElement('dd')
      dt.textContent = label
      dd.textContent = value
      group.append(dt, dd)
      return group
    }))
  } catch (error) {
    console.error(error)
    status.textContent = `加载失败 · ${error.message}`
  } finally {
    submit.disabled = false
  }
}

canvas.addEventListener('pointerdown', () => canvas.focus())
canvas.addEventListener('blur', () => keys.clear())
canvas.addEventListener('keydown', event => {
  if (event.ctrlKey || event.metaKey || event.altKey) return
  if (/^Key[WASDQE]$/.test(event.code)) {
    keys.add(event.code)
    event.preventDefault()
  }
})
canvas.addEventListener('keyup', event => keys.delete(event.code))
document.querySelector('#overview').addEventListener('click', event => {
  overview = !overview
  event.target.textContent = overview ? '回到角色' : '俯瞰'
})
document.querySelector('#reset').addEventListener('click', reset)
maps.addEventListener('submit', event => {
  event.preventDefault()
  load(document.querySelector('#map').value)
})
load(document.querySelector('#map').value)

async function protocol() {
  const indicator = document.querySelector('#protocol-status')
  try {
    const { results, wasm } = await fetchFile('/protocol').then(response => response.json())
    indicator.textContent = results.map(({ arch, matches }) => {
      const label = matches === null ? '未测' : matches ? '一致' : '不一致'
      return `${arch} ↔ wasm32 ${label}`
    }).join(' · ')
    document.querySelector('#protocol-bytes').textContent = wasm
  } catch (error) {
    indicator.textContent = `协议对照未加载 · ${error.message}`
  }
}
protocol()

const buttons = [document.querySelector('#rtc-direct'), document.querySelector('#rtc-loss')]
for (const [index, button] of buttons.entries()) {
  button.addEventListener('click', async () => {
    const indicator = document.querySelector('#rtc-status')
    for (const control of buttons) control.disabled = true
    indicator.textContent = '正在交换 ICE 候选并收发数据…'
    try {
      const result = await testRtc(index ? { loss: true, count: 180 } : {})
      const [first] = result.stats
      const stats = first.loss
        ? `UDP 丢包 ${first.loss.dropped}/${first.loss.sent}`
        : `${result.stats.length} 对端共用 UDP ${first.port}`
      indicator.textContent = `${stats} · 消息上限 ${result.maxMessageSize / 1024} KiB · 3 KiB 上行 ${first.unreliableReceived}/${result.sent} · 往返 ${result.received}/${result.sent}`
    } catch (error) {
      indicator.textContent = `连接测试失败 · ${error.message}`
    } finally {
      for (const control of buttons) control.disabled = false
    }
  })
}
