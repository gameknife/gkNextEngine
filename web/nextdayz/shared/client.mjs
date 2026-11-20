import { connectRtc } from "../w1/transport.mjs";

const views = { frame: 0, profile: 1, effects: 2, loot: 3, inventory: 4, nearby: 5, cargo: 6, ground: 7 };
const utf8 = new TextEncoder();

export class Client {
    constructor(wasm, options) {
        this.wasm = wasm;
        this.options = options;
        this.pointer = wasm._NdCreate();
        this.protocol = wasm._NdProtocol();
    }

    read(view = "frame") {
        const { wasm, pointer } = this;
        return JSON.parse(wasm.UTF8ToString(wasm._NdRead(pointer, views[view])));
    }

    previewClock(minutes, weather) {
        return JSON.parse(this.wasm.UTF8ToString(this.wasm._NdPreviewClock(this.pointer, minutes, weather)));
    }

    input(bytes, apply) {
        const { wasm, pointer } = this;
        const buffer = wasm._malloc(bytes.length || 1);
        if (!buffer) throw Error("Cannot allocate WASM input");
        try {
            wasm.HEAPU8.set(bytes, buffer);
            return apply(pointer, buffer, bytes.length);
        } finally {
            wasm._free(buffer);
        }
    }

    collision(operation, bytes, first, second) {
        const error = this.input(bytes, (p, buffer) => this.wasm._NdCollision(p, operation, buffer, first, second));
        if (error) throw Error(this.wasm.UTF8ToString(error));
    }

    loadRig(kind, bytes) {
        const error = this.input(bytes, (p, buffer, length) => this.wasm._NdLoadRig(p, kind, buffer, length));
        if (error) throw Error(this.wasm.UTF8ToString(error));
    }

    poseMatrices() {
        const { pointer, wasm } = this;
        return new Float32Array(wasm.HEAPU8.buffer, wasm._NdPoseData(pointer), wasm._NdPoseSize(pointer));
    }

    controls(flags, dx, dy) {
        if (this.pointer) this.wasm._NdControls(this.pointer, flags, dx, dy);
    }

    command(command) {
        if (!this.pointer) return;
        const bytes = utf8.encode(JSON.stringify(command));
        const error = this.input(bytes, (p, buffer, length) => this.wasm._NdCommand(p, buffer, length));
        if (error) throw Error(this.wasm.UTF8ToString(error));
        this.drain();
    }

    receive = bytes => {
        if (!this.pointer) return;
        this.input(bytes, (p, buffer, length) => this.wasm._NdReceive(p, buffer, length));
        this.drain();
    };

    tick(dt) {
        if (!this.pointer) return;
        this.wasm._NdTick(this.pointer, dt);
        this.drain();
    }

    drain() {
        const { wasm, pointer, transport } = this;
        let length;
        while ((length = wasm._NdPopPacket(pointer)) !== 0) {
            const data = wasm._NdPacket(pointer);
            transport?.send(wasm.HEAPU8.slice(data, data + length), wasm._NdPacketReliable(pointer) !== 0);
        }
        for (const effect of this.read("effects")) {
            if (effect.type === "http") void this.master(effect);
            else if (effect.type === "connect") void this.connect(effect);
            else if (effect.type === "presentation") this.options.present?.(effect);
            else if (effect.type === "map") void this.confirmMap(effect.scene);
            else if (effect.type === "disconnect") {
                if (this.transport) this.transport.disconnect();
                else this.closed();
            }
        }
    }

    async master(effect) {
        let reply;
        try {
            const response = await this.options.fetchMaster(effect.request, effect.body, effect.token);
            const body = await response.text();
            let error;
            if (!response.ok) {
                try { error = JSON.parse(body).error; } catch { error = response.statusText; }
            }
            reply = { status: response.status, body, error };
        } catch (error) {
            reply = { status: 0, body: "", error: error.message };
        }
        this.command({ type: "http", request: effect.request, ...reply });
    }

    async connect(effect) {
        const attempt = this.attempt = {};
        try {
            if (!effect.direct) await this.getMap(effect.scene);
            if (this.attempt !== attempt) return;
            const transport = await connectRtc(
                sdp => this.options.signal(effect, sdp),
                this.receive,
                reason => {
                    if (this.attempt === attempt) this.closed(reason);
                },
                this.options.rtc,
            );
            if (this.attempt !== attempt) {
                transport.disconnect();
                return;
            }
            this.transport = transport;
            this.command({ type: "connected" });
        } catch (error) {
            if (this.attempt === attempt) this.closed(error.message);
        }
    }

    getMap(scene) {
        if (this.map?.scene !== scene) {
            const loading = { scene, ready: this.options.loadMap(scene).catch(error => {
                if (this.map === loading) this.map = undefined;
                throw error;
            }) };
            this.map = loading;
        }
        return this.map.ready;
    }

    async confirmMap(scene) {
        const attempt = this.attempt;
        try {
            const { spawn, collision, sites } = await this.getMap(scene);
            if (attempt !== this.attempt) return;
            if (collision) this.options.installCollision(collision, this);
            this.command({ type: "mapReady", scene, spawn, sites });
        } catch (error) {
            if (attempt === this.attempt) {
                const transport = this.transport;
                this.closed(error.message);
                transport?.disconnect();
            }
        }
    }

    closed(reason = "") {
        this.attempt = undefined;
        this.transport = undefined;
        this.command({ type: "closed", reason });
        this.options.onClose?.(reason);
    }

    destroy() {
        this.attempt = undefined;
        this.transport?.disconnect();
        this.wasm._NdDestroy(this.pointer);
        this.pointer = 0;
    }
}

export function loopbackMaster(address, directAddress = "") {
    const request = (url, body, token) => fetch(url, {
        method: "POST",
        headers: { "Content-Type": "application/json", ...(token ? { Authorization: `Bearer ${token}` } : {}) },
        body,
        signal: AbortSignal.timeout(35000),
    });
    return {
        fetchMaster: (operation, body, token) => request(`${address}/api/${operation}`, body, token),
        async signal({ direct, server, ticket, token }, sdp) {
            const url = direct ? `${directAddress}/rtc/offer` : `${address}/api/rtc/offer`;
            const response = await request(url, JSON.stringify(direct ? { sdp } : { server, ticket, sdp }), token);
            const answer = await response.json();
            if (!response.ok || !answer.sdp) throw Error(answer.error ?? "Invalid signal reply");
            return answer.sdp;
        },
    };
}
