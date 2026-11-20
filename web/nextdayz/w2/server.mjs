import { createReadStream } from "node:fs";
import { stat } from "node:fs/promises";
import { createServer } from "node:http";
import { resolve, extname, sep } from "node:path";
import { fileURLToPath } from "node:url";

const here = fileURLToPath(new URL(".", import.meta.url));
const assets = resolve(here, "../../../out/web/nextdayz/assets");
const three = resolve(here, "../w0/node_modules/three");
const port = Number(process.env.ND_WEB_REVIEW_PORT ?? 4178);
const types = {
    ".html": "text/html; charset=utf-8", ".css": "text/css; charset=utf-8",
    ".js": "text/javascript; charset=utf-8", ".mjs": "text/javascript; charset=utf-8",
    ".json": "application/json", ".glb": "model/gltf-binary", ".webp": "image/webp",
    ".bin": "application/octet-stream", ".wav": "audio/wav",
};

createServer(async (request, response) => {
    try {
        const pathname = decodeURIComponent(new URL(request.url, "http://localhost").pathname);
        if (request.method !== "GET" && request.method !== "HEAD") {
            response.writeHead(405).end();
            return;
        }
        const baked = pathname.startsWith("/assets/");
        const dependency = pathname.startsWith("/three/");
        const root = baked ? assets : dependency ? three : resolve(here);
        const relative = baked || dependency ? pathname.slice(pathname.indexOf("/", 1) + 1) : pathname.slice(1) || "index.html";
        const extension = extname(relative);
        const path = resolve(root, relative);
        if (!path.startsWith(root + sep) || !Object.hasOwn(types, extension)) {
            response.writeHead(404).end();
            return;
        }
        const immutable = baked && relative !== "manifest.json";
        if (immutable && !/^(maps|rigs|shared)\//.test(relative)) {
            response.writeHead(404).end();
            return;
        }
        let file = path;
        let encoding;
        if (immutable && /(?:^|[, ])gzip(?:[, ;]|$)/.test(request.headers["accept-encoding"] ?? "")) {
            if (await stat(path + ".gz").then(info => info.isFile(), () => false)) {
                file += ".gz";
                encoding = "gzip";
            }
        }
        const info = await stat(file);
        if (!info.isFile()) {
            response.writeHead(404).end();
            return;
        }
        response.writeHead(200, {
            "Content-Type": types[extension],
            "Content-Length": info.size,
            "Cache-Control": immutable ? "public, max-age=31536000, immutable" : "no-store",
            ...(immutable ? { Vary: "Accept-Encoding" } : {}),
            ...(encoding ? { "Content-Encoding": encoding } : {}),
        });
        if (request.method === "HEAD") response.end();
        else createReadStream(file).on("error", () => response.destroy()).pipe(response);
    } catch (error) {
        if (!response.headersSent) response.writeHead(error.code === "ENOENT" ? 404 : 500);
        response.end();
    }
}).listen(port, "127.0.0.1", () => {
    process.stdout.write(`NextDayz W2 acceptance · http://127.0.0.1:${port}/\n`);
});
