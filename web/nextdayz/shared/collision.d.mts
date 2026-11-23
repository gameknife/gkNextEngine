import type { Client } from "./client.mjs";

export type WordRange = [offset: number, length: number];
export interface GeneratedCollision {
    packet: ArrayBuffer;
    meshes: { positions: WordRange; indices: WordRange }[];
    records: WordRange;
}
export type LevelCollision = ArrayBuffer | GeneratedCollision;

export function installCollision(bytes: LevelCollision, client: Client): void;
