import assert from "node:assert/strict";
import { readdir, readFile } from "node:fs/promises";
import { test } from "node:test";

// UI text is keyed by English in the desktop's translation table (design 8.2); the Web
// client may not carry its own Chinese.
const root = new URL("../", import.meta.url);
const table = new Map((await readFile(new URL("../../src/Application/Game/NextDayz/UI/Translations.inl", root), "utf8"))
    .matchAll(/ND_TEXT\("((?:[^"\\]|\\.)*)",\s*"((?:[^"\\]|\\.)*)"\)/g).map(([, en, zh]) => [en, zh]));
const sources = (await readdir(new URL("src/", root))).filter(name => name.endsWith(".ts"));
const read = name => readFile(new URL(`src/${name}`, root), "utf8");
const withoutComments = text => text.replace(/\/\*[\s\S]*?\*\//g, "").replace(/(^|[^:])\/\/.*$/gm, "$1");

test("every UI key the Web client uses is in the translation table", async () => {
    const missing = [];
    for (const name of sources) {
        for (const [, key] of withoutComments(await read(name)).matchAll(/\btr\("((?:[^"\\]|\\.)*)"/g))
            if (!table.has(key)) missing.push(`${name}: ${key}`);
    }
    const html = await readFile(new URL("index.html", root), "utf8");
    for (const [, key] of html.matchAll(/data-i18n(?:-label)?="([^"]+)"/g))
        if (!table.has(key)) missing.push(`index.html: ${key}`);
    assert.deepEqual(missing, []);
});

test("the game UI source has no hard-coded Chinese", async () => {
    const found = [];
    for (const name of sources) {
        withoutComments(await read(name)).split("\n").forEach((line, index) => {
            if (/[一-鿿]/.test(line) && !line.includes('"简体中文"')) found.push(`${name}:${index + 1}`);
        });
    }
    assert.deepEqual(found, []);
});
