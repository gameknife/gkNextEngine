async function signal(request) {
  const response = await fetch('/rtc', {
    method: 'POST',
    headers: { 'Content-Type': 'application/json' },
    body: JSON.stringify(request),
  })
  const result = await response.json()
  if (!response.ok) throw Error(result.error)
  return result
}

function until(target, events, predicate, timeout = 15000) {
  return new Promise((resolve, reject) => {
    const finish = error => {
      clearTimeout(timer)
      for (const event of events) target.removeEventListener(event, check)
      if (error) reject(error)
      else resolve()
    }
    const check = () => {
      if (predicate()) finish()
    }
    const timer = setTimeout(() => finish(Error(`${events.join('/')} timed out`)), timeout)
    for (const event of events) target.addEventListener(event, check)
    check()
  })
}

async function connect({ loss, stun, clientStun }) {
  const peer = new RTCPeerConnection({
    iceServers: clientStun ? [{ urls: clientStun }] : [],
  })
  const reliable = peer.createDataChannel('r')
  const unreliable = peer.createDataChannel('u', { ordered: false, maxRetransmits: 0 })
  reliable.binaryType = unreliable.binaryType = 'arraybuffer'
  let id
  async function close() {
    peer.close()
    if (id !== undefined) await signal({ method: 'close', id })
  }

  try {
    await peer.setLocalDescription(await peer.createOffer())
    await until(peer, ['icegatheringstatechange'], () => peer.iceGatheringState === 'complete')
    const answer = await signal({ method: 'offer', sdp: peer.localDescription.sdp, loss, stun })
    id = answer.id
    await peer.setRemoteDescription({ type: 'answer', sdp: answer.sdp })
    await Promise.all([reliable, unreliable].map(channel =>
      until(channel, ['open', 'close'], () => channel.readyState === 'open'),
    ))
    return {
      peer, reliable, unreliable, close,
      serverStats: () => signal({ method: 'stats', id }),
      srflx: (answer.sdp.match(/^a=candidate:.* typ srflx.*$/gm) ?? []).length,
    }
  } catch (error) {
    await close()
    throw error
  }
}

function delay(ms) {
  return new Promise(resolve => setTimeout(resolve, ms))
}

async function echo(channel, payload) {
  channel.send(payload)
  return new Promise((resolve, reject) => {
    const deadline = setTimeout(() => {
      channel.removeEventListener('message', receive)
      reject(Error('DataChannel echo timed out'))
    }, 4000)
    function receive(event) {
      clearTimeout(deadline)
      resolve(event.data)
    }
    channel.addEventListener('message', receive, { once: true })
  })
}

export async function testRtc({
  loss = false, count = 0, stun = false, clientStun, dual = !loss && !stun, reliableSize,
} = {}) {
  const peers = []
  try {
    const first = await connect({ loss, stun, clientStun })
    peers.push(first)
    if (dual) peers.push(await connect({ loss, stun, clientStun }))

    const { reliable, unreliable, peer } = first
    const maxMessageSize = peer.sctp.maxMessageSize
    const large = new Uint8Array(Math.min(maxMessageSize, reliableSize ?? maxMessageSize))
    large[0] = 42
    large[large.length - 1] = 11
    const reply = new Uint8Array(await echo(reliable, large))
    if (reply.length !== large.length || reply.some((byte, i) => byte !== large[i])) {
      throw Error('Reliable echo content mismatch')
    }
    if (unreliable.ordered || unreliable.maxRetransmits !== 0 || !reliable.ordered) {
      throw Error('Unexpected reliability options')
    }

    const received = new Set()
    unreliable.onmessage = ({ data }) => {
      if (data.byteLength === 3072) received.add(new DataView(data).getUint32(0))
    }
    const message = new DataView(new ArrayBuffer(3072))
    const samples = count || 10
    for (let index = 0; index < samples; index++) {
      message.setUint32(0, index)
      unreliable.send(message.buffer)
      await delay(loss ? 16 : 10)
    }
    await delay(600)
    if (!received.size) throw Error('No unreliable echoes')
    if (peers.length === 2 && await echo(peers[1].reliable, 'mux') !== 'mux') {
      throw Error('Second peer echo failed')
    }

    const stats = await Promise.all(peers.map(connection => connection.serverStats()))
    if (stats.some(value => value.port !== stats[0].port)) throw Error('UDP port not shared')
    return { maxMessageSize, reliableBytes: large.byteLength, stats, srflx: first.srflx, sent: samples, received: received.size }
  } finally {
    await Promise.all(peers.map(connection => connection.close()))
  }
}
