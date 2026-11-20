import type { Anchor, Cargo, Clock, Frame, GroundItem, Item, Loot, Presentation, Profile, XYZ } from "../src/types";

interface TransportOptions {
    fetchMaster(request: string, body: string, token: string): Promise<Response>;
    signal(effect: Record<string, unknown>, sdp: string): Promise<string>;
}
export function loopbackMaster(address: string, directAddress?: string): TransportOptions;

export class Client {
    constructor(wasm: unknown, options: TransportOptions & {
        loadMap(scene: string): Promise<{ spawn: XYZ; collision?: ArrayBuffer; sites?: Anchor[] }>;
        installCollision?(bytes: ArrayBuffer, client: Client): void;
        present?(event: Presentation): void;
        onClose?(reason: string): void;
    });
    readonly protocol: number;
    collision(operation: number, bytes: Uint8Array, first: number, second: number): void;
    loadRig(kind: number, bytes: Uint8Array): void;
    poseMatrices(): Float32Array;
    controls(flags: number, dx: number, dy: number): void;
    command(command: Record<string, unknown>): void;
    read(view?: "frame"): Frame;
    read(view: "profile"): Profile;
    read(view: "inventory"): Item[];
    read(view: "loot"): Loot[];
    read(view: "nearby"): Loot | null;
    read(view: "cargo"): Cargo;
    read(view: "ground"): GroundItem[];
    previewClock(minutes: number, weather: number): Clock;
    tick(dt: number): void;
    destroy(): void;
}
