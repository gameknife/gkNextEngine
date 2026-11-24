export type XYZ = [number, number, number];

export interface Item {
    id: string;
    name: string;
    description: string;
    count: number;
    ammo: number;
    instance?: number;
    worn?: boolean;
    wearable?: boolean;
    usable?: boolean;
}

export interface MapEntry {
    id: string;
    name: string;
    scene: string;
    training: boolean;
}

export interface Profile {
    name: string;
    gold: number;
    storage: Item[];
    maps: MapEntry[];
    character: { map: string; storage: Item[] } | null;
    sorties: number;
    extractions: number;
    deaths: number;
    playing: string;
    lastOutcome: string;
}

export interface Clock {
    minutes: number;
    weather: "Clear" | "Overcast" | "Fog";
    daylight: number;
    sunRotation: number;
    hasSun: boolean;
    sunIntensity: number;
    skyIntensity: number;
    sunColor: XYZ;
    skyColor: XYZ;
    fogColor: XYZ;
    fogDensity: number;
}

export interface Loot { id: string; position: XYZ; contents: Item[] }

export interface Anchor {
    type: string;
    position: XYZ;
    label?: string;
    profile?: string;
}
export interface Mount { index: number; position: XYZ; scale: number }
export interface GroundItem { id: string; item: string; position: XYZ; yaw: number }
export interface CargoItem extends Item {
    instance: number;
    container: number;
    x: number;
    y: number;
    width: number;
    height: number;
    rotated: boolean;
    equipSlot: number;
    weaponSlot: number;
    preferredSlot?: number;
    wieldable: boolean;
    attachable: boolean;
    attachments: number[];
}
export interface CargoBox {
    id: number;
    owner: number;
    name: string;
    width: number;
    height: number;
    used: number;
    weaponOnly: boolean;
}
export interface Cargo { items: CargoItem[]; containers: CargoBox[]; activeSlot: number }
export interface Presentation {
    event: string;
    position: XYZ;
    end?: XYZ;
    impact?: boolean;
    weapon?: string;
    suppressed?: boolean;
}
export interface Pose {
    stance: "standing" | "crouched";
    gait: "idle" | "walk" | "run" | "sprint";
    speed: number;
    move: [number, number];
    grounded: boolean;
    jump: "none" | "up" | "air_loop" | "down";
    jumpTime: number;
    traversal: "none" | "vault" | "mantle" | "climb" | "climb_over";
    traversalTime: number;
    traversalYaw: number;
    sprint: number;
    pitch: number;
    aim: number;
    probe?: string;
    weapon: string;
    slots: { id: string; mounts: Mount[] }[];
    activeSlot: number;
    action: number;
    actionTime: number;
    weaponAction: number;
    weaponActionTime: number;
    clothing: string[];
    clothingCoverage: number;
    bodyYaw: number;
    viewModelOffset: number;
    viewModelArms: boolean;
    matrixOffset: number;
    // Held weapon then one matrix per holster slot, placed by the kernel as the desktop rig places them.
    gearOffset: number;
    heldVisible: boolean;
    holsterVisible: boolean[];
}

export interface Frame {
    phase: "login" | "waiting" | "profile" | "deploying" | "joining" | "playing";
    // Where to draw the local survivor: between the last two 60 Hz steps (feet is the latest).
    drawFeet?: XYZ;
    message: string;
    scene: string;
    id: number;
    feet: XYZ;
    eye: XYZ;
    forward: XYZ;
    yaw: number;
    snapshots: number;
    remotes: { id: number; position: XYZ; yaw: number; alive: boolean; matrixOffset: number; pose: Pose }[];
    zombies: { id: number; position: XYZ; yaw: number; profile: number; state: number; speed: number; matrixOffset: number }[];
    camera: { position: XYZ; forward: XYZ; up: XYZ; fov: number; firstPerson: boolean; arm: number } | null;
    pose: Pose | null;
    body: { health: number; hunger: number; hydration: number; bleeding: number; cold: number; healing: boolean; cause: string };
    combat: {
        shots: number; hits: number; kills: number; hitMarker: number; hurt: number;
        headshot: boolean; kill: boolean; weapon: string; magazine: number; reserve: number; capacity: number;
        action: number; progress: number;
    };
    nearWell: boolean;
    nearExtraction: boolean;
    extractSeconds: number;
    extractBroken: boolean;
    lootCount: number;
    lootReady: boolean;
    alive: boolean;
    pvp: boolean;
    clock: Clock;
}

export interface AssetFile {
    path: string;
    bytes: number;
    sha256: string;
    gzipBytes?: number;
}

export interface MapBundle {
    directory: string;
    protocol: number;
    map: { id: string; scene: string; name: string; localName: string; mapVersion: number; profile: string };
    files: Record<string, AssetFile>;
}

export interface Manifest {
    protocol: number;
    kernelBuild: string | null;
    maps: Record<string, MapBundle>;
    rigs?: Record<string, { files: Record<string, AssetFile> }>;
    shared?: { files: Record<string, AssetFile> };
}
