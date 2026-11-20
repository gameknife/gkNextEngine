import { Assets } from "./assets";
import { settings } from "./settings";
import type { AssetFile, Frame, Presentation, XYZ } from "./types";

const samples: Record<string, string[]> = {
    dry_fire: ["dry_fire"], reload: ["reload_rifle"], switch: ["switch_weapon"],
    melee: ["melee_swing_1", "melee_swing_2"], blood: ["hit_flesh_1", "hit_flesh_2", "hit_flesh_3"],
    hurt: ["player_hurt_1", "player_hurt_2"], headshot: ["hit_head_1", "hit_head_2"], kill: ["kill_confirm"],
    pickup: ["pickup"], drop: ["item_drop"], drink: ["drink"], eat: ["eat"], bandage: ["bandage"],
    ambient: ["ambient_wind_1", "ambient_wind_2", "ambient_wind_3"],
    infected: ["zombie_groan_1", "zombie_groan_2", "zombie_groan_3", "zombie_groan_4"],
    impact: ["impact_dirt_1", "impact_dirt_2"],
};

export class FieldAudio {
    private context?: AudioContext;
    private master?: GainNode;
    private stepBuffer?: AudioBuffer;
    private buffers = new Map<string, Promise<AudioBuffer>>();
    private sources = new Set<AudioBufferSourceNode>();
    private cache = new Assets();
    private lastStep = 0;
    private nextAmbient = 0;
    private nextInfected = 0;
    private generation = 0;

    constructor(private readonly files: Record<string, AssetFile>) {
        document.addEventListener("visibilitychange", () => {
            if (document.hidden) {
                this.stop();
                void this.context?.suspend();
            } else {
                void this.context?.resume().catch(() => {});
            }
        });
    }

    async unlock() {
        if (!this.context) {
            this.context = new AudioContext();
            this.master = this.context.createGain();
            this.master.connect(this.context.destination);
            this.volume();
        }
        await this.context.resume();
    }

    volume() {
        if (this.context && this.master)
            this.master.gain.setTargetAtTime(settings.volume * .62, this.context.currentTime, .04);
    }

    event(event: Presentation) {
        const pick = (set: string[]) => set[Math.floor(Math.random() * set.length)];
        if (event.event === "shot" || event.event === "remote_shot") {
            const weapon = event.weapon;
            const sidearm = weapon === "pistol" || weapon === "aps";
            const shotgun = weapon === "shotgun" || weapon === "toz34";
            const sniper = weapon === "svd" || weapon === "mosin" || weapon === "svt40";
            const family = sidearm ? "pistol" : shotgun ? "shotgun" : sniper ? "sniper" : "rifle";
            void this.sample(`shot_${family}_${1 + Math.floor(Math.random() * (family === "rifle" ? 3 : 2))}`,
                event.position, event.suppressed ? .22 : .87);
        } else if (event.event === "trace" && event.impact && event.end) {
            void this.sample(pick(samples.impact), event.end, .46);
        } else if (samples[event.event]) {
            void this.sample(pick(samples[event.event]), event.position, event.event === "blood" ? .56 : .78);
        }
    }

    update(frame: Frame) {
        if (!this.context || this.context.state !== "running" || document.hidden) return;
        const listener = this.context.listener;
        const [x, y, z] = frame.eye;
        const [fx, fy, fz] = frame.camera?.forward ?? frame.forward;
        const [ux, uy, uz] = frame.camera?.up ?? [0, 1, 0];
        const now = this.context.currentTime;
        for (const [param, value] of [
            [listener.positionX, x], [listener.positionY, y], [listener.positionZ, z],
            [listener.forwardX, fx], [listener.forwardY, fy], [listener.forwardZ, fz],
            [listener.upX, ux], [listener.upY, uy], [listener.upZ, uz],
        ] as const) param.setValueAtTime(value, now);
        const distance = (p: XYZ) => Math.hypot(p[0] - x, p[1] - y, p[2] - z);
        if (frame.pose?.grounded && frame.pose.speed > .5 && now >= this.lastStep) {
            this.lastStep = now + Math.min(.65, .84 / frame.pose.speed + .12);
            this.step(frame.feet, frame.pose.gait === "sprint" ? .26 : .14);
        }
        if (now >= this.nextInfected) {
            this.nextInfected = now + 1.1 + Math.random() * .65;
            const nearby = frame.zombies.filter(zombie => zombie.state !== 7 && distance(zombie.position) < 36);
            const infected = nearby.sort((a, b) => distance(a.position) - distance(b.position))[0];
            if (infected) {
                const pool = infected.state === 5 ? ["zombie_attack_1", "zombie_attack_2"] : samples.infected;
                void this.sample(pool[Math.floor(Math.random() * pool.length)], infected.position, .76);
            }
        }
        if (now >= this.nextAmbient) {
            this.nextAmbient = now + 7 + Math.random() * 4;
            const pool = frame.clock.daylight > .38 ? samples.ambient : ["ambient_night_1", "ambient_night_2"];
            void this.sample(pool[Math.floor(Math.random() * pool.length)], undefined, .24);
        }
    }

    stop() {
        this.generation++;
        for (const source of this.sources) source.stop();
        this.sources.clear();
    }

    private async sample(name: string, point: XYZ | undefined, gain: number) {
        const audio = this.context;
        const file = this.files[`audio/${name}.wav`];
        if (!audio || audio.state !== "running" || document.hidden || !file) return;
        let pending = this.buffers.get(name);
        if (!pending) {
            pending = this.cache.read(file).then(bytes => audio.decodeAudioData(bytes));
            this.buffers.set(name, pending);
        }
        const generation = this.generation;
        try {
            const decoded = await pending;
            if (generation === this.generation) this.play(decoded, point, gain);
        } catch { this.buffers.delete(name); }
    }

    private step(point: XYZ, gain: number) {
        const audio = this.context;
        if (!audio) return;
        if (!this.stepBuffer) {
            this.stepBuffer = audio.createBuffer(1, Math.floor(audio.sampleRate * .115), audio.sampleRate);
            const out = this.stepBuffer.getChannelData(0);
            let low = 0;
            for (let i = 0; i < out.length; i++) {
                low = .82 * low + .18 * (Math.random() * 2 - 1);
                const t = i / out.length;
                out[i] = Math.sin(Math.PI * t) * (1 - t) * (low + .35 * Math.sin(i * .011));
            }
        }
        this.play(this.stepBuffer, point, gain);
    }

    private play(buffer: AudioBuffer, point: XYZ | undefined, gain: number) {
        const audio = this.context;
        if (!audio || !this.master || audio.state !== "running" || document.hidden || this.sources.size >= 24) return;
        const source = audio.createBufferSource();
        source.buffer = buffer;
        const volume = audio.createGain();
        volume.gain.value = gain;
        source.connect(volume);
        if (point) {
            const pan = new PannerNode(audio, { panningModel: "HRTF", distanceModel: "inverse",
                refDistance: 1.7, maxDistance: 85, rolloffFactor: .78,
                positionX: point[0], positionY: point[1], positionZ: point[2] });
            volume.connect(pan).connect(this.master);
            source.onended = () => { pan.disconnect(); volume.disconnect(); this.sources.delete(source); };
        } else {
            volume.connect(this.master);
            source.onended = () => { volume.disconnect(); this.sources.delete(source); };
        }
        this.sources.add(source);
        source.start();
    }
}
