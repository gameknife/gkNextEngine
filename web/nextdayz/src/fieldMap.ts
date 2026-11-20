import type { Level } from "./assets";
import type { Frame, XYZ } from "./types";
import { text } from "./settings";

export function drawFieldMap(canvas: HTMLCanvasElement, level: Level, frame: Frame, backdrop?: HTMLImageElement) {
    const ctx = canvas.getContext("2d");
    if (!ctx) return;
    const { width, height } = canvas;
    const [min, max] = level.bounds;
    const centerX = (min[0] + max[0]) / 2;
    const centerZ = (min[2] + max[2]) / 2;
    const scale = (height - 34) / Math.max(max[0] - min[0], max[2] - min[2]);
    const point = ([x, , z]: XYZ) => [width / 2 + (x - centerX) * scale, height / 2 + (z - centerZ) * scale];
    ctx.fillStyle = "#192217";
    ctx.fillRect(0, 0, width, height);
    if (backdrop?.complete && backdrop.naturalWidth) {
        ctx.drawImage(backdrop, (width - height + 34) / 2, 17, height - 34, height - 34);
    }
    ctx.strokeStyle = "#66745e65";
    ctx.lineWidth = 1;
    for (let value = Math.ceil(min[0] / 100) * 100; value < max[0]; value += 100) {
        const [x] = point([value, 0, 0]);
        ctx.beginPath();
        ctx.moveTo(x, 17);
        ctx.lineTo(x, height - 17);
        ctx.stroke();
    }
    for (let value = Math.ceil(min[2] / 100) * 100; value < max[2]; value += 100) {
        const [, y] = point([0, 0, value]);
        ctx.beginPath();
        ctx.moveTo((width - height + 34) / 2, y);
        ctx.lineTo((width + height - 34) / 2, y);
        ctx.stroke();
    }
    ctx.font = "13px system-ui";
    ctx.lineWidth = 3;
    ctx.strokeStyle = "#172017";
    const marker = (position: XYZ, color: string, label?: string, radius = 4) => {
        const [x, y] = point(position);
        ctx.fillStyle = color;
        ctx.beginPath();
        ctx.arc(x, y, radius, 0, Math.PI * 2);
        ctx.fill();
        if (!label) return;
        ctx.strokeText(label, x + 8, y - 6);
        ctx.fillText(label, x + 8, y - 6);
    };
    for (const anchor of level.anchors) {
        if (anchor.type === "poi") marker(anchor.position, "#e7ddaa", anchor.label);
        if (anchor.type === "well") marker(anchor.position, "#7bd4dc", anchor.label || text("水源", "Water"));
        if (anchor.type === "extraction") marker(anchor.position, "#bade8e", anchor.label || text("撤离", "Extraction"), 6);
    }
    const [px, py] = point(frame.feet);
    ctx.save();
    ctx.translate(px, py);
    ctx.rotate(-frame.yaw + Math.PI);
    ctx.beginPath();
    ctx.moveTo(0, -11);
    ctx.lineTo(6, 8);
    ctx.lineTo(0, 5);
    ctx.lineTo(-6, 8);
    ctx.closePath();
    ctx.fillStyle = "#d8f4ff";
    ctx.stroke();
    ctx.fill();
    ctx.restore();
    ctx.fillStyle = "#e4ebd8";
    ctx.fillText(text("北 ↑", "N ↑"), 20, 29);
    ctx.fillRect(18, height - 28, 100 * scale, 2);
    ctx.fillText("100 m", 18, height - 34);
}

export function compass(yaw: number) {
    const heading = ((180 - yaw * 180 / Math.PI) % 360 + 360) % 360;
    const labels = text("北 东北 东 东南 南 西南 西 西北", "N NE E SE S SW W NW").split(" ");
    const index = Math.round(heading / 45);
    return { heading: Math.round(heading) % 360, label: labels[index % 8],
        left: labels[(index + 7) % 8], right: labels[(index + 1) % 8] };
}
