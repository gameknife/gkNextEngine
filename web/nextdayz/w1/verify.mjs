import assert from 'node:assert/strict'
import { mkdir, writeFile } from 'node:fs/promises'
import { setTimeout as delay } from 'node:timers/promises'
import { chromium } from '../w0/node_modules/playwright/index.mjs'

const direct = process.env.ND_W1_DIRECT ?? 'http://127.0.0.1:7781'
const master = process.env.ND_W1_MASTER
const evidence = new URL('../../../out/web/nextdayz/w1-evidence/', import.meta.url)
await mkdir(evidence, { recursive: true })

async function post(base, path, body, token) {
  const response = await fetch(base + path, {
    method: 'POST',
    headers: {
      'Content-Type': 'application/json',
      ...(token && { Authorization: `Bearer ${token}` }),
    },
    body: JSON.stringify(body),
    signal: AbortSignal.timeout(25000),
  })
  const data = await response.json()
  if (!response.ok) throw Error(`${path}: ${data.error ?? response.status}`)
  return data
}

const browser = await chromium.launch(process.env.ND_W1_CHROME ? { channel: 'chrome' } : {})
try {
  const page = await browser.newPage({ viewport: { width: 1280, height: 900 } })
  const errors = []
  page.on('pageerror', error => errors.push(error.message))
  await page.goto(direct)
  await page.getByLabel('幸存者昵称').fill('web-review-check')
  await page.getByRole('button', { name: '加入服务器' }).click()
  await page.waitForFunction(() =>
    Number(document.getElementById('snapshots').textContent) >= 10 &&
    /^\d/.test(document.getElementById('others').textContent) &&
    document.getElementById('loot').textContent.includes('KiB'), null, { timeout: 30000 })
  const read = id => page.locator('#' + id).textContent()
  const directResult = {
    survivor: await read('survivor'),
    scene: await read('scene'),
    protocol: await read('protocol'),
    snapshots: await read('snapshots'),
    enetSurvivors: await read('others'),
    loot: await read('loot'),
  }
  await page.screenshot({ path: new URL('direct.png', evidence).pathname })
  await page.getByRole('button', { name: '离开' }).click()
  await page.locator('#status').filter({ hasText: '已离开服务器' }).waitFor()
  console.log('Direct', JSON.stringify(directResult))

  let masterResult
  if (master) {
    const name = 'w1-browser-' + Date.now().toString(36)
    const { token } = await post(master, '/api/login', { name })
    const deadline = Date.now() + 90000
    let dispatch
    do {
      dispatch = await post(master, '/api/deploy', { map: 'cz_loket_riverbend', bring: [] }, token)
      if (dispatch.state === 'ready') break
      await delay(1000)
    } while (Date.now() < deadline)
    assert.equal(dispatch.state, 'ready')
    assert.ok(dispatch.rtcPort > 0)
    let offers = 0
    await page.exposeFunction('w1MasterOffer', async sdp => {
      ++offers
      return (await post(master, '/api/rtc/offer', { server: dispatch.server, ticket: dispatch.ticket, sdp }, token)).sdp
    })
    const result = await page.evaluate(async ({ name, ticket }) => {
      const { inspectConnection } = await import('/review.mjs')
      const evidence = {}
      let transport
      try {
        return await new Promise((resolve, reject) => {
          const timeout = setTimeout(() => reject(Error('等待 Master 下发出生数据超时')), 30000)
          const finish = (callback, result) => {
            clearTimeout(timeout)
            callback(result)
          }
          inspectConnection({
            name, ticket, signal: sdp => window.w1MasterOffer(sdp),
            onUpdate(data) {
              if (data.type === 'reject') finish(reject, Error(data.message))
              evidence[data.type] = data
              if (evidence.welcome && evidence.spawn && evidence.loot && evidence.snapshot) {
                finish(resolve, evidence)
              }
            },
            onClose(error) {
              if (error) finish(reject, Error(error))
            },
          }).then(value => { transport = value }, error => finish(reject, error))
        })
      } finally {
        transport?.disconnect()
      }
    }, { name, ticket: dispatch.ticket })
    assert.equal(offers, 1)
    masterResult = { server: dispatch.server, rtcPort: dispatch.rtcPort, ...result }
    console.log('Master', JSON.stringify(masterResult))
  }
  assert.deepEqual(errors, [])
  const result = { browser: browser.version(), direct: directResult, master: masterResult }
  await writeFile(new URL('browser.json', evidence), JSON.stringify(result, null, 2) + '\n')
} finally {
  await browser.close()
}
