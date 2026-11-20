import createProtocol from '/protocol.mjs'
import { connectRtc } from '/transport.mjs'

const form = document.querySelector('form')
const join = form.querySelector('button[type="submit"]')
const leave = document.querySelector('#disconnect')
const fields = ['survivor', 'scene', 'protocol', 'snapshots', 'others', 'loot']
const set = (id, value) => { document.getElementById(id).textContent = value }

function packet(protocol, len) {
  const address = protocol._ReviewBytes()
  return protocol.HEAPU8.slice(address, address + len)
}

export async function inspectConnection({ name, ticket = '', signal, onUpdate, onClose }) {
  const protocol = await createProtocol()
  let transport
  let snapshots = 0
  transport = await connectRtc(signal, payload => {
    const address = protocol._malloc(payload.length)
    if (!address) return
    let data
    try {
      protocol.HEAPU8.set(payload, address)
      const value = protocol._ReviewRead(address, payload.length)
      if (!value) return
      data = JSON.parse(protocol.UTF8ToString(value))
    } finally {
      protocol._free(address)
    }
    if (data.type === 'welcome') transport.send(packet(protocol, protocol._ReviewLootSync()))
    if (data.type === 'snapshot') data.count = ++snapshots
    onUpdate(data)
  }, onClose)

  const encoder = new TextEncoder()
  const nameBytes = encoder.encode(name)
  const ticketBytes = encoder.encode(ticket)
  const address = protocol._malloc(nameBytes.length + ticketBytes.length + 2)
  if (!address) {
    transport.disconnect()
    throw Error('WASM 内存分配失败')
  }
  try {
    protocol.HEAPU8.set(nameBytes, address)
    protocol.HEAPU8[address + nameBytes.length] = 0
    const ticketAddress = address + nameBytes.length + 1
    protocol.HEAPU8.set(ticketBytes, ticketAddress)
    protocol.HEAPU8[ticketAddress + ticketBytes.length] = 0
    transport.send(packet(protocol, protocol._ReviewHello(address, ticketAddress)))
  } finally {
    protocol._free(address)
  }
  return transport
}

let active
async function localOffer(sdp) {
  const response = await fetch('/rtc/offer', {
    method: 'POST',
    headers: { 'Content-Type': 'application/json' },
    body: JSON.stringify({ sdp }),
  })
  const answer = await response.json()
  if (!response.ok) throw Error(answer.error)
  return answer.sdp
}

form.addEventListener('submit', async event => {
  event.preventDefault()
  join.disabled = true
  for (const field of fields) set(field, '—')
  set('status', '正在交换信令…')
  let welcomed = false
  let rejected = false
  let survivor
  try {
    active = await inspectConnection({
      name: document.getElementById('name').value.trim(),
      signal: localOffer,
      onUpdate(data) {
        if (data.type === 'welcome') {
          welcomed = true
          survivor = data.survivor
          set('status', '已入服 · WebRTC 双通道')
          set('survivor', survivor)
          const parts = data.scene.split('/')
          const published = parts.lastIndexOf('published')
          const map = published > 0 ? `${parts[published - 1]} · ${parts[published + 1]}` : parts.at(-1)
          set('scene', map)
          document.getElementById('scene').title = data.scene
          set('protocol', data.protocol)
        } else if (data.type === 'snapshot' && welcomed) {
          set('snapshots', data.count)
          set('others', data.survivors.filter(id => id !== survivor).join('、') || '当前无其他幸存者')
        } else if (data.type === 'loot') {
          set('loot', `${data.entries.toLocaleString()} 条 · ${(data.bytes / 1024).toFixed(1)} KiB`)
        } else if (data.type === 'reject') {
          rejected = true
          set('status', `服务器拒绝连接 · ${data.message}`)
        }
      },
      onClose(reason) {
        active = undefined
        join.disabled = false
        leave.disabled = true
        if (!rejected) set('status', reason || '已离开服务器')
      },
    })
    leave.disabled = false
    if (!welcomed) set('status', '双通道已开启，等待服务器 Welcome…')
  } catch (error) {
    join.disabled = false
    set('status', `连接失败 · ${error.message}`)
  }
})

leave.addEventListener('click', () => active?.disconnect())
