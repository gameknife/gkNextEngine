import worldLand from "../../../assets/configs/nextdayz/world_land.json";
import { tr } from "./settings";

export interface AtlasLocation {
    id: string;
    name: string;
    region: string;
    lat: number;
    lon: number;
    training?: boolean;
    available: boolean;
}

// Known expedition coordinates aligned with assets/configs/nextdayz_maps.json.
const KNOWN_COORDS: Record<string, { lat: number; lon: number; region: string }> = {
    cz_loket_riverbend: { lat: 50.186, lon: 12.754, region: "CZECHIA / WEST BOHEMIA" },
    ke_talek_mara: { lat: -1.443, lon: 35.2112, region: "KENYA / MAASAI MARA" },
};

export function mapLocationMeta(id: string) {
    return KNOWN_COORDS[id] ?? null;
}

const polygons: number[][][] = worldLand.polygons ?? [];

export class WorldAtlas {
    private zoom = 1;
    private panX = 0;
    private panY = 0;
    private dragging = false;
    private lastX = 0;
    private lastY = 0;
    private locations: AtlasLocation[] = [];
    private selectedId = "";

    constructor(private canvas: HTMLCanvasElement, private onSelect: (id: string) => void) {
        canvas.addEventListener("wheel", e => {
            e.preventDefault();
            this.zoom = Math.max(1, Math.min(5, this.zoom * Math.pow(1.16, e.deltaY < 0 ? 1 : -1)));
            this.draw();
        }, { passive: false });

        canvas.addEventListener("contextmenu", e => e.preventDefault());
        canvas.addEventListener("mousedown", e => {
            if (e.button === 2) {
                this.dragging = true;
                this.lastX = e.clientX;
                this.lastY = e.clientY;
            } else if (e.button === 0) {
                this.pick(e.clientX, e.clientY);
            }
        });
        window.addEventListener("mousemove", e => {
            if (!this.dragging) return;
            this.panX += e.clientX - this.lastX;
            this.panY += e.clientY - this.lastY;
            this.lastX = e.clientX;
            this.lastY = e.clientY;
            this.draw();
        });
        window.addEventListener("mouseup", () => { this.dragging = false; });
        canvas.addEventListener("dblclick", () => {
            this.zoom = 1;
            this.panX = 0;
            this.panY = 0;
            this.draw();
        });
        new ResizeObserver(() => this.draw()).observe(canvas);
    }

    update(locations: AtlasLocation[], selectedId: string) {
        this.locations = locations;
        this.selectedId = selectedId;
        this.draw();
    }

    private project(lon: number, lat: number, w: number, h: number): [number, number] {
        const scale = Math.min(w / 360, h / 155) * this.zoom * 0.94;
        const cx = w * 0.5 + this.panX;
        const cy = h * 0.5 + this.panY;
        return [cx + lon * scale, cy - (lat - 12.5) * scale];
    }

    private pick(clientX: number, clientY: number) {
        const rect = this.canvas.getBoundingClientRect();
        const mx = clientX - rect.left;
        const my = clientY - rect.top;
        for (const loc of this.locations) {
            if (loc.training || !loc.available) continue;
            const [x, y] = this.project(loc.lon, loc.lat, rect.width, rect.height);
            if (Math.hypot(mx - x, my - y) < 24) {
                this.selectedId = loc.id;
                this.onSelect(loc.id);
                this.draw();
                return;
            }
        }
    }

    draw() {
        const dpr = Math.min(window.devicePixelRatio || 1, 2);
        const w = Math.max(300, this.canvas.clientWidth);
        const h = Math.max(220, this.canvas.clientHeight);
        if (this.canvas.width !== Math.round(w * dpr) || this.canvas.height !== Math.round(h * dpr)) {
            this.canvas.width = Math.round(w * dpr);
            this.canvas.height = Math.round(h * dpr);
        }
        const ctx = this.canvas.getContext("2d");
        if (!ctx) return;
        ctx.save();
        ctx.scale(dpr, dpr);

        // Ocean
        ctx.fillStyle = "#0d1414";
        ctx.fillRect(0, 0, w, h);

        // Graticule grid
        ctx.strokeStyle = "rgba(120, 152, 140, 0.14)";
        ctx.lineWidth = 1;
        for (let lon = -180; lon <= 180; lon += 30) {
            const [x0, y0] = this.project(lon, -90, w, h);
            const [x1, y1] = this.project(lon, 90, w, h);
            ctx.beginPath(); ctx.moveTo(x0, y0); ctx.lineTo(x1, y1); ctx.stroke();
        }
        for (let lat = -60; lat <= 90; lat += 30) {
            const [x0, y0] = this.project(-180, lat, w, h);
            const [x1, y1] = this.project(180, lat, w, h);
            ctx.beginPath(); ctx.moveTo(x0, y0); ctx.lineTo(x1, y1); ctx.stroke();
        }

        // Vector continents (same Natural Earth dataset as Desktop)
        ctx.fillStyle = "#22302c";
        ctx.strokeStyle = "#3f5951";
        ctx.lineWidth = 0.9;
        for (const poly of polygons) {
            if (poly.length < 3) continue;
            ctx.beginPath();
            poly.forEach(([lon, lat], i) => {
                const [x, y] = this.project(lon, lat, w, h);
                if (i === 0) ctx.moveTo(x, y);
                else ctx.lineTo(x, y);
            });
            ctx.closePath();
            ctx.fill();
            ctx.stroke();
        }

        // Map markers
        ctx.font = '600 12px "Bahnschrift", system-ui, sans-serif';
        for (const loc of this.locations) {
            if (loc.training) continue;
            const [x, y] = this.project(loc.lon, loc.lat, w, h);
            const active = loc.id === this.selectedId;
            if (active) {
                ctx.strokeStyle = "rgba(217, 155, 56, 0.55)";
                ctx.lineWidth = 1.4;
                ctx.beginPath(); ctx.arc(x, y, 16, 0, Math.PI * 2); ctx.stroke();
                ctx.beginPath();
                ctx.moveTo(x - 24, y); ctx.lineTo(x - 9, y);
                ctx.moveTo(x + 9, y); ctx.lineTo(x + 24, y);
                ctx.stroke();
            }
            ctx.fillStyle = active ? "#d99b38" : loc.available ? "#eceae0" : "#676d63";
            ctx.beginPath(); ctx.arc(x, y, active ? 5.5 : 4.2, 0, Math.PI * 2); ctx.fill();
            ctx.strokeStyle = "#0d1414";
            ctx.lineWidth = 2;
            ctx.stroke();

            // Callout tag
            const tw = ctx.measureText(loc.name).width;
            const bx = x > w - tw - 48 ? x - tw - 32 : x + 18;
            const by = y - 14;
            ctx.fillStyle = "rgba(14, 17, 15, 0.92)";
            ctx.strokeStyle = active ? "#d99b38" : "rgba(228, 224, 208, 0.22)";
            ctx.lineWidth = 1;
            ctx.fillRect(bx, by, tw + 16, 24);
            ctx.strokeRect(bx, by, tw + 16, 24);
            ctx.fillStyle = active ? "#ebb152" : "#eceae0";
            ctx.fillText(loc.name, bx + 8, by + 16);
        }

        // Footer atlas label
        ctx.fillStyle = "rgba(159, 164, 152, 0.65)";
        ctx.font = "11px ui-monospace, monospace";
        ctx.fillText(tr("FIELD ATLAS  /  WGS84"), 16, 24);
        ctx.fillText(tr("WHEEL: ZOOM   /   RIGHT DRAG: PAN   /   DOUBLE CLICK: RESET"), 16, h - 14);
        ctx.restore();
    }
}
