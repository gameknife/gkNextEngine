import { createReadStream } from "node:fs";
import { stat } from "node:fs/promises";
import { extname, resolve, sep } from "node:path";
import { fileURLToPath } from "node:url";
import { defineConfig } from "vite";

const output = fileURLToPath(new URL("../../out/web/nextdayz/", import.meta.url));
const mime = {
    ".mjs": "text/javascript", ".wasm": "application/wasm", ".json": "application/json",
    ".webp": "image/webp", ".glb": "model/gltf-binary", ".bin": "application/octet-stream", ".wav": "audio/wav",
    ".scad": "text/plain", ".hmap": "application/octet-stream", ".ttf": "font/ttf",
};
function sendFiles(prefix, root) {
    return async (request, response, next) => {
        const url = new URL(request.url ?? "/", "http://localhost");
        if (!url.pathname.startsWith(prefix)) return next();
        try {
            const relative = decodeURIComponent(url.pathname.slice(prefix.length));
            const file = resolve(root, relative);
            const extension = extname(file);
            if (!file.startsWith(root + sep) || !mime[extension]) {
                response.writeHead(404).end();
                return;
            }
            const immutable = prefix === "/game-assets/" && relative !== "manifest.json";
            const useCompression = immutable || prefix === "/scad-probe/";
            const acceptsGzip = (request.headers["accept-encoding"] ?? "").split(",").some(value => {
                const [token, ...options] = value.trim().split(";");
                return token === "gzip" && !options.some(option => /^\s*q\s*=\s*0(?:\.0*)?\s*$/.test(option));
            });
            const compressed = useCompression && acceptsGzip && await stat(file + ".gz").then(f => f.isFile(), () => false);
            const path = compressed ? file + ".gz" : file;
            const info = await stat(path);
            if (!info.isFile()) {
                response.writeHead(404).end();
                return;
            }
            response.writeHead(200, {
                "Content-Type": mime[extension],
                "Content-Length": info.size,
                "Cache-Control": immutable ? "public, max-age=31536000, immutable" : "no-store",
                ...(useCompression && { Vary: "Accept-Encoding" }),
                ...(compressed && { "Content-Encoding": "gzip" }),
            });
            if (request.method === "HEAD") response.end();
            else createReadStream(path).on("error", () => response.destroy()).pipe(response);
        } catch (error) {
            if (!response.headersSent) response.writeHead(error.code === "ENOENT" ? 404 : 500);
            response.end();
        }
    };
}
function middleware(server) {
    server.middlewares.use(sendFiles("/ndcore/", resolve(output, "core")));
    server.middlewares.use(sendFiles("/game-assets/", resolve(output, "assets")));
    server.middlewares.use(sendFiles("/scad-probe/", resolve(output, "scad-probe", "public")));
}
const proxy = {
    "/api/": { target: process.env.ND_MASTER_URL ?? "http://127.0.0.1:7784" },
    "/direct/rtc/offer": {
        target: process.env.ND_DIRECT_URL ?? "http://127.0.0.1:7781",
        rewrite: path => path.slice("/direct".length),
    },
};
export default defineConfig({
    base: "./",
    plugins: [{
        name: "nextdayz-assets",
        configureServer(server) {
            middleware(server);
            const manifest = resolve(output, "assets", "manifest.json");
            server.watcher.add(manifest);
            server.watcher.on("change", file => {
                if (file === manifest) server.ws.send({ type: "full-reload" });
            });
        },
        configurePreviewServer: middleware,
    }],
    server: { host: "127.0.0.1", port: Number(process.env.ND_WEB_PORT ?? 4179), strictPort: true, proxy },
    preview: { host: "127.0.0.1", port: 4180, strictPort: true, proxy },
    build: { outDir: output + "site", emptyOutDir: true, chunkSizeWarningLimit: 800 },
});
