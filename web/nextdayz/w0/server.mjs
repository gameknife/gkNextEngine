import { spawn } from 'node:child_process'
import { createSocket } from 'node:dgram'
import { createReadStream } from 'node:fs'
import { readFile, stat } from 'node:fs/promises'
import { createServer } from 'node:http'
import { networkInterfaces } from 'node:os'
import { join } from 'node:path'
import { createInterface } from 'node:readline'
import { fileURLToPath } from 'node:url'

const app = fileURLToPath(new URL('.', import.meta.url))
const output = fileURLToPath(new URL('../../../out/web/nextdayz/w0/', import.meta.url))
const rtcPort = Number(process.env.ND_W0_UDP_PORT ?? 28041)
const proxies = new Map()
let backend
let requestId = 0

function signal(message) {
  if (!backend) {
    const child = spawn(join(output, 'rtc-build/rtc-probe'), [String(rtcPort)], {
      stdio: ['pipe', 'pipe', 'inherit'],
    })
    const pending = new Map()
    backend = { child, pending }
    createInterface({ input: child.stdout }).on('line', line => {
      const result = JSON.parse(line)
      const callback = pending.get(result.request)
      pending.delete(result.request)
      if (result.error) callback?.reject(Error(result.error))
      else callback?.resolve(result)
    })
    const reset = error => {
      if (backend?.child === child) backend = undefined
      for (const callback of pending.values()) callback.reject(error)
      pending.clear()
    }
    child.on('error', reset)
    child.on('exit', code => reset(Error(`RTC probe exited (${code})`)))
    child.stdin.on('error', reset)
  }
  return new Promise((resolve, reject) => {
    const request = ++requestId
    backend.pending.set(request, { resolve, reject })
    backend.child.stdin.write(JSON.stringify({ ...message, request }) + '\n')
  })
}

async function openLossProxy() {
  const address = Object.values(networkInterfaces()).flat()
    .find(entry => entry?.family === 'IPv4' && !entry.internal)?.address ?? '127.0.0.1'
  const socket = createSocket('udp4')
  const stats = { sent: 0, dropped: 0 }
  let remote
  let seed = 0x4e455854
  socket.on('message', (packet, from) => {
    const fromServer = from.port === rtcPort && from.address === address
    if (!fromServer) remote = from
    const destination = fromServer ? remote : { address, port: rtcPort }
    if (!destination) return

    stats.sent++
    seed = (Math.imul(seed, 1664525) + 1013904223) >>> 0
    if (seed / 2 ** 32 < 0.02) {
      stats.dropped++
      return
    }
    socket.send(packet, destination.port, destination.address)
  })
  await new Promise((resolve, reject) => {
    socket.once('error', reject)
    socket.bind(0, address, resolve)
  })
  return { socket, stats, address, port: socket.address().port }
}

function throughProxy(sdp, proxy) {
  return sdp.split(/\r?\n/).flatMap(line => {
    if (!line.startsWith('a=candidate:')) return [line]
    const fields = line.split(' ')
    if (fields[4] !== proxy.address || fields[5] !== String(rtcPort)) return []
    fields[5] = String(proxy.port)
    return [fields.join(' ')]
  }).join('\r\n')
}

async function rtc(request) {
  const chunks = []
  let size = 0
  for await (const chunk of request) {
    size += chunk.length
    if (size > 65536) throw Error('Signal body is too large')
    chunks.push(chunk)
  }
  const body = JSON.parse(Buffer.concat(chunks).toString())
  const { method, id } = body
  if (method === 'stats') return { ...await signal({ method, id }), loss: proxies.get(id)?.stats }
  if (method === 'close') {
    proxies.get(id)?.socket.close()
    proxies.delete(id)
    return signal({ method, id })
  }
  if (method !== 'offer' || typeof body.sdp !== 'string') throw Error('Invalid offer')
  if (!body.loss) return signal({ method, sdp: body.sdp, stun: !!body.stun })

  const proxy = await openLossProxy()
  let answer
  try {
    // The server learns the peer-reflexive candidate through incoming ICE checks.
    // Removing the browser's host candidates prevents checks bypassing the proxy.
    const sdp = body.sdp.replace(/^a=(?:candidate:.*|end-of-candidates)\r?\n/gm, '')
    answer = await signal({ method, sdp })
    const forwarded = throughProxy(answer.sdp, proxy)
    if (!forwarded.includes('a=candidate:')) throw Error('Proxy address missing from ICE candidates')
    proxies.set(answer.id, proxy)
    return { ...answer, sdp: forwarded }
  } catch (error) {
    proxy.socket.close()
    if (answer) await signal({ method: 'close', id: answer.id })
    throw error
  }
}

const files = new Map([
  ['/', join(app, 'index.html')],
  ...['preview.mjs', 'rtc.mjs', 'style.css'].map(name => ['/' + name, join(app, name)]),
  ['/three', join(app, 'node_modules/three/build/three.module.js')],
  ['/three.core.js', join(app, 'node_modules/three/build/three.core.js')],
  ...['collision.mjs', 'collision.wasm'].map(name => ['/jolt/' + name, join(output, 'jolt-build', name)]),
  ...['training_ground', 'cz_loket_riverbend'].flatMap(map =>
    ['bake.json', 'scene.glb', 'collision.bin'].map(name => [
      `/maps/${map}/${name}`, join(output, 'maps', map, name),
    ]),
  ),
])
const mime = {
  html: 'text/html; charset=utf-8',
  css: 'text/css; charset=utf-8',
  mjs: 'text/javascript; charset=utf-8',
  js: 'text/javascript; charset=utf-8',
  wasm: 'application/wasm',
  json: 'application/json',
  glb: 'model/gltf-binary',
}
function json(response, status, body) {
  response.writeHead(status, { 'Content-Type': 'application/json', 'Cache-Control': 'no-store' })
  response.end(JSON.stringify(body))
}

const server = createServer(async (request, response) => {
  try {
    const pathname = new URL(request.url, 'http://localhost').pathname
    if (pathname === '/transport' && request.method === 'GET') {
      response.writeHead(200, { 'Content-Type': 'text/html; charset=utf-8' })
      response.end('<!doctype html><title>NextDayz W0 transport probe</title>')
      return
    }
    if (pathname === '/favicon.ico' && request.method === 'GET') {
      response.writeHead(204).end()
      return
    }
    if (pathname === '/rtc' && request.method === 'POST') {
      return json(response, 200, await rtc(request))
    }
    if (pathname === '/protocol' && request.method === 'GET') {
      const dir = join(output, 'protocol')
      const wasm = await readFile(join(dir, 'wasm32.txt'), 'utf8')
      const results = await Promise.all(['arm64', 'x86_64'].map(async arch => {
        const native = await readFile(join(dir, `native-${arch}.txt`), 'utf8').catch(() => null)
        return { arch, matches: native === null ? null : native === wasm }
      }))
      return json(response, 200, { results, wasm })
    }
    let path = files.get(pathname)
    if (!path && /^\/addons\/(loaders|utils)\/[A-Za-z0-9]+\.js$/.test(pathname)) {
      path = join(app, 'node_modules/three/examples/jsm', pathname.slice('/addons/'.length))
    }
    if (request.method !== 'GET' || !path) {
      return json(response, 404, { error: 'Not found' })
    }
    const { size } = await stat(path)
    response.writeHead(200, {
      'Content-Length': size,
      'Content-Type': mime[path.split('.').at(-1)] ?? 'application/octet-stream',
      'Cache-Control': 'no-store',
    })
    const stream = createReadStream(path)
    stream.on('error', () => response.destroy())
    response.on('close', () => stream.destroy())
    stream.pipe(response)
  } catch (error) {
    json(response, error.code === 'ENOENT' ? 404 : 500, { error: error.message })
  }
})
server.listen(Number(process.env.ND_W0_HTTP_PORT ?? 4176), '127.0.0.1', () => {
  console.log(`NextDayz W0 · http://127.0.0.1:${server.address().port}`)
})
