import { MeshoptDecoder } from "three/addons/libs/meshopt_decoder.module.js";

export function installCollision(bytes, client) {
    const data = new DataView(bytes);
    let offset = 0;
    const count = () => {
        const value = data.getUint32(offset, true);
        offset += 4;
        return value;
    };
    const take = length => {
        if (offset + length > bytes.byteLength) throw Error("地图碰撞文件不完整");
        const slice = new Uint8Array(bytes, offset, length);
        offset += length;
        return slice;
    };
    if (count() !== 0x3143444e) throw Error("地图碰撞格式不兼容");
    const meshes = count();
    const instances = count();
    client.collision(0, new Uint8Array(), meshes, instances);
    for (let i = 0; i < meshes; ++i) {
        const vertices = count();
        const indices = count();
        const vertexBytes = count();
        const indexBytes = count();
        const packedPositions = take(vertexBytes);
        const packedIndices = take(indexBytes);
        if (!vertices || vertices > 2_000_000 || !indices || indices > 12_000_000 || indices % 3) {
            throw Error("地图碰撞网格计数无效");
        }
        const mesh = new Uint8Array(vertices * 12 + indices * 4);
        MeshoptDecoder.decodeVertexBuffer(mesh.subarray(0, vertices * 12), vertices, 12, packedPositions);
        MeshoptDecoder.decodeIndexBuffer(mesh.subarray(vertices * 12), indices, 4, packedIndices);
        client.collision(1, mesh, vertices * 3, indices);
    }
    const instanceWords = instances * 30;
    const records = take(instanceWords * 4);
    if (offset !== bytes.byteLength) throw Error("地图碰撞文件大小不一致");
    client.collision(2, records, instanceWords, 0);
}
