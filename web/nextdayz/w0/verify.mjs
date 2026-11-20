import assert from 'node:assert/strict'
import { mkdir, writeFile } from 'node:fs/promises'
import { chromium, firefox, webkit } from 'playwright'
import { PeerConnection, getLibraryVersion } from 'node-datachannel'

const url = process.env.ND_W0_URL ?? 'http://127.0.0.1:4176'
const artifacts = new URL('../../../out/web/nextdayz/w0/evidence/', import.meta.url)
await mkdir(artifacts, { recursive: true })
const report = { browsers: {} }
const requested = (process.env.ND_W0_BROWSERS ?? 'chromium').split(',')
const engines = { chromium, firefox, webkit }
const units = ['training_ground', 'cz_loket_riverbend']

async function verifyMaps(page) {
  const maps = []
  for (const map of units) {
    if (map !== 'training_ground') {
      await page.locator('#map').selectOption(map)
      await page.getByRole('button', { name: '加载', exact: true }).click()
    }
    await page.waitForFunction(id => window.w0Metrics?.id === id, map, { timeout: 45000 })
    const starting = await page.evaluate(() => window.w0Metrics)
    await page.locator('canvas').focus()
    await page.keyboard.down('KeyW')
    await page.waitForTimeout(2300)
    await page.keyboard.up('KeyW')
    const moved = await page.evaluate(() => window.w0Metrics.position)
    const distance = Math.hypot(moved[0] - starting.position[0], moved[2] - starting.position[2])
    assert.ok(distance > 3, `${map} capsule failed to move`)
    assert.ok(Math.abs(moved[1] - starting.position[1]) < 3, `${map} capsule didn't stay on ground`)

    // Drive the same capsule into the real training gate's side wall.
    let wallStop
    if (map === 'training_ground') {
      await page.getByRole('button', { name: '回到起点' }).click()
      await page.locator('canvas').focus()
      await page.keyboard.down('KeyD')
      await page.waitForTimeout(1600)
      await page.keyboard.up('KeyD')
      await page.keyboard.down('KeyW')
      await page.waitForFunction(() => window.w0Metrics.position[2] < 267, null, { timeout: 35000 })
      const near = await page.evaluate(() => window.w0Metrics.position)
      await page.waitForTimeout(2400)
      await page.keyboard.up('KeyW')
      wallStop = await page.evaluate(() => window.w0Metrics.position)
      assert.ok(wallStop[2] > 265 && wallStop[2] < 267 && Math.abs(wallStop[2] - near[2]) < 0.7,
        `training wall failed to stop capsule: ${near} → ${wallStop}`)
    }

    await page.getByRole('button', { name: '俯瞰' }).click()
    await page.waitForTimeout(2100)
    const overview = await page.evaluate(() => window.w0Metrics)
    await page.screenshot({ path: new URL(`${map}.png`, artifacts).pathname })
    maps.push({ map, starting, moved, distance, wallStop, overview })
  }
  return maps
}

for (const engine of requested) {
  const options = {}
  if (engine === 'chromium' && process.env.ND_W0_CHROME) options.channel = 'chrome'
  const browser = await engines[engine].launch(options)
  try {
    const page = await browser.newPage({ viewport: { width: 1920, height: 1080 } })
    page.on('pageerror', error => console.error(engine, error.message))
    page.on('console', entry => {
      if (entry.type() === 'error') console.error(engine, entry.text())
    })
    // Run the transport probe without a rendering loop or map load.
    await page.goto(url + '/transport')
    const evaluate = args => page.evaluate(async args => (await import('/rtc.mjs')).testRtc(args), args)
    const clientStun = engine === 'firefox' ? process.env.ND_W0_STUN : undefined
    const direct = await evaluate({
      stun: !!clientStun, clientStun, dual: !clientStun, reliableSize: clientStun ? 3072 : undefined,
    })
    if (!clientStun) {
      assert.equal(direct.received, direct.sent)
      assert.equal(direct.stats.length, 2)
      assert.equal(direct.stats[0].port, direct.stats[1].port)
    }
    report.browsers[engine] = { version: browser.version(), direct }
    console.log(engine, 'direct', JSON.stringify(direct))
    // The injected loss route uses local host candidates. A STUN-mapped Firefox
    // session verifies negotiation and both channels through the real network path.
    if (clientStun) continue
    const loss = await evaluate({ loss: true, clientStun, count: Number(process.env.ND_W0_SAMPLES ?? 600) })
    assert.ok(loss.stats[0].loss.dropped > 0, 'No UDP datagrams were dropped')
    let publicCandidate
    if (engine === 'chromium' && process.env.ND_W0_CHECK_STUN) {
      publicCandidate = await evaluate({ stun: true })
      assert.ok(publicCandidate.srflx > 0, 'No server-reflexive candidate in answer')
    }
    report.browsers[engine] = { version: browser.version(), direct, loss, publicCandidate }
    console.log(engine, JSON.stringify(report.browsers[engine]))
    await page.close()

    if (engine === 'chromium' && process.env.ND_W0_SKIP_MAPS !== '1') {
      const app = await browser.newPage({ viewport: { width: 1920, height: 1080 } })
      app.on('pageerror', error => console.error(error.message))
      app.on('console', entry => {
        if (entry.type() === 'error') console.error(entry.text())
      })
      await app.goto(url)
      report.maps = await verifyMaps(app)
      console.log(JSON.stringify(report.maps))
    }
  } catch (error) {
    report.browsers[engine] = { ...report.browsers[engine], error: error.message }
    process.exitCode = 1
    console.error(engine, error.message)
  } finally {
    await browser.close()
  }
}

const peer = new PeerConnection('nextdayz-w0', { iceServers: [] })
let nativeId
try {
  const offer = new Promise((resolve, reject) => {
    const timeout = setTimeout(() => reject(Error('Node ICE gathering timed out')), 10000)
    peer.onGatheringStateChange(state => {
      if (state !== 'complete') return
      clearTimeout(timeout)
      resolve(peer.localDescription())
    })
  })
  const data = peer.createDataChannel('r')
  const sdp = await offer
  const response = await fetch(url + '/rtc', {
    method: 'POST',
    headers: { 'Content-Type': 'application/json' },
    body: JSON.stringify({ method: 'offer', sdp: sdp.sdp }),
  })
  const answer = await response.json()
  assert.ok(response.ok, answer.error)
  nativeId = answer.id
  const echo = new Promise((resolve, reject) => {
    const timeout = setTimeout(() => reject(Error('Node channel echo timed out')), 10000)
    data.onOpen(() => data.sendMessage('nextdayz-node'))
    data.onMessage(message => {
      clearTimeout(timeout)
      resolve(message)
    })
  })
  peer.setRemoteDescription(answer.sdp, 'answer')
  assert.equal(await echo, 'nextdayz-node')
  report.node = { version: process.version, libdatachannel: getLibraryVersion(), echo: true }
} finally {
  peer.close()
  if (nativeId !== undefined) await fetch(url + '/rtc', {
    method: 'POST',
    headers: { 'Content-Type': 'application/json' },
    body: JSON.stringify({ method: 'close', id: nativeId }),
  })
}
const destination = new URL(`${requested.join('-')}.json`, artifacts)
await writeFile(destination, JSON.stringify(report, null, 2) + '\n')
console.log('Node', JSON.stringify(report.node))
console.log(destination.pathname)
