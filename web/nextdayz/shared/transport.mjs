// WebRTC framing for NextDayz: reliable messages are split into frames and reassembled,
// unreliable ones carry a sequence number. The payload itself is handled by C++/WASM.
const final = 0
const more = 1
const ping = 2
const pong = 3
const close = 4
const closeAck = 5
const maxFrameBytes = 16 * 1024
const maxMessageBytes = 4 * 1024 * 1024

function waitFor(target, event, accept) {
  if (accept()) return Promise.resolve()
  return new Promise((resolve, reject) => {
    function check() {
      if (accept()) {
        target.removeEventListener(event, check)
        clearTimeout(timer)
        resolve()
      }
    }
    const timer = setTimeout(() => {
      target.removeEventListener(event, check)
      reject(Error(`WebRTC ${event} 超时`))
    }, 12000)
    target.addEventListener(event, check)
  })
}

export async function connectRtc(signal, onPayload, onClose, options = {}) {
  const peer = new (options.PeerConnection ?? globalThis.RTCPeerConnection)()
  const latency = options.latency ?? 0
  const lost = options.loss ?? 0
  let sentSequence = 0
  const timers = new Set()
  const reliable = peer.createDataChannel('r')
  const unreliable = peer.createDataChannel('u', { ordered: false, maxRetransmits: 0 })
  const chunks = []
  let size = 0
  let sequence
  let closed = false
  reliable.binaryType = unreliable.binaryType = 'arraybuffer'

  function finish(reason) {
    if (closed) return
    closed = true
    for (const timer of timers) clearTimeout(timer)
    timers.clear()
    peer.close()
    onClose(reason)
  }

  function sendFrame(channel, frame, mustDeliver) {
    try {
      channel.send(frame)
      return true
    } catch (error) {
      if (mustDeliver || channel.readyState !== 'open') finish(`WebRTC 发送失败：${error.message}`)
      return false
    }
  }

  reliable.onmessage = ({ data }) => {
    if (closed) return
    const frame = new Uint8Array(data)
    if (!frame.length || frame.length > maxFrameBytes) return finish('可靠帧长度无效')
    const kind = frame[0]
    if (kind === ping || kind === close) {
      if (frame.length !== 5) return finish('控制帧长度无效')
      frame[0] = kind === ping ? pong : closeAck
      if (reliable.readyState === 'open') sendFrame(reliable, frame, true)
      if (kind === close) setTimeout(() => finish('服务器已结束连接'), 200)
      return
    }
    if (kind === closeAck) return finish()
    if ((kind !== final && kind !== more) || (kind === more && frame.length === 1)) return finish('可靠帧类型无效')
    size += frame.length - 1
    if (size > maxMessageBytes) return finish('可靠消息超出重组上限')

    if (!chunks.length && kind === final) {
      onPayload(frame.subarray(1))
      size = 0
      return
    }
    chunks.push(frame.subarray(1))
    if (kind === final) {
      const message = new Uint8Array(size)
      let offset = 0
      for (const chunk of chunks) {
        message.set(chunk, offset)
        offset += chunk.length
      }
      size = 0
      chunks.length = 0
      onPayload(message)
    }
  }

  unreliable.onmessage = ({ data }) => {
    const frame = new Uint8Array(data)
    if (closed || frame.length < 2 || frame.length > maxFrameBytes) return
    const next = frame[0] | frame[1] << 8
    const ahead = (next - sequence) & 0xffff
    if (sequence !== undefined && (ahead === 0 || ahead >= 0x8000)) return
    sequence = next
    onPayload(frame.subarray(2))
  }

  peer.onconnectionstatechange = () => {
    if (['failed', 'closed', 'disconnected'].includes(peer.connectionState)) finish('WebRTC 已断开')
  }
  reliable.onclose = unreliable.onclose = () => finish()

  try {
    await peer.setLocalDescription(await peer.createOffer())
    await waitFor(peer, 'icegatheringstatechange', () => peer.iceGatheringState === 'complete')
    const answer = await signal(peer.localDescription.sdp)
    await peer.setRemoteDescription({ type: 'answer', sdp: answer })
    await Promise.all([reliable, unreliable].map(channel => waitFor(channel, 'open', () => channel.readyState === 'open')))
    return {
      send(payload, reliableDelivery = true) {
        if (closed) return
        if (!reliableDelivery && Math.random() * 100 < lost) return
        const sendFrames = () => {
          if (closed) return
          if (!reliableDelivery) {
            if (unreliable.readyState !== 'open' || payload.length + 2 > maxFrameBytes) return
            const frame = new Uint8Array(payload.length + 2)
            const sequence = sentSequence++ & 0xffff
            frame[0] = sequence & 0xff
            frame[1] = sequence >> 8
            frame.set(payload, 2)
            sendFrame(unreliable, frame, false)
            return
          }
          if (reliable.readyState !== 'open' || payload.length > maxMessageBytes) return
          for (let offset = 0; offset < payload.length; offset += maxFrameBytes - 1) {
            const body = payload.subarray(offset, offset + maxFrameBytes - 1)
            const frame = new Uint8Array(body.length + 1)
            frame[0] = offset + body.length < payload.length ? more : final
            frame.set(body, 1)
            if (!sendFrame(reliable, frame, true)) return
          }
        }
        if (latency > 0) {
          const timer = setTimeout(() => {
            timers.delete(timer)
            sendFrames()
          }, latency)
          timers.add(timer)
        } else sendFrames()
      },
      disconnect() {
        if (!closed && reliable.readyState === 'open') sendFrame(reliable, new Uint8Array([close, 0, 0, 0, 0]), true)
        setTimeout(() => finish(), 600)
      },
    }
  } catch (error) {
    finish(error.message)
    throw error
  }
}
