#!/usr/bin/env node
/**
 * embed_web_assets.mjs — vendored dist -> gzip -> C-header generator (03-02)
 *
 * Embeds the Leaflet 1.9.4 JS/CSS dist into firmware as gzipped PROGMEM
 * byte arrays so the served page works in AP mode with no internet (only
 * the OSM tiles are allowed to be internet-dependent — D-37 / Registry
 * Safety provenance lock).
 *
 * Usage:
 *   node scripts/embed_web_assets.mjs --fetch   # ONE-TIME: download the
 *                                              # pinned dist into vendor/
 *                                              # + record provenance.json
 *   node scripts/embed_web_assets.mjs           # regenerate src/web_assets.h
 *                                              # from vendor/ (idempotent)
 *
 * Supply-chain lock (T-03-SC): --fetch downloads ONLY from unpkg pinned to
 * leaflet@1.9.4 (never a mirror), records the sha256 of every file, and the
 * generator FAILS if a vendor file no longer matches its recorded hash.
 * The generated header carries the pinned URL, sha256, retrieval date and
 * the BSD 2-Clause license notice so the committed artifact is reviewable.
 */

import { createHash } from 'node:crypto';
import { readFile, writeFile, mkdir } from 'node:fs/promises';
import { dirname, join } from 'node:path';
import { fileURLToPath } from 'node:url';
import { gzipSync, constants as zlibConstants } from 'node:zlib';

const ROOT = dirname(dirname(fileURLToPath(import.meta.url)));
const VENDOR_DIR = join(ROOT, 'vendor');
const HEADER_PATH = join(ROOT, 'src', 'web_assets.h');

// Pinned source — leaflet@1.9.4 on unpkg (official distribution channel;
// leafletjs.com links here). NEVER change to a mirror or an unpinned tag.
const LEAFLET_VERSION = '1.9.4';
const LEAFLET_BASE = `https://unpkg.com/leaflet@${LEAFLET_VERSION}/dist/`;
const LEAFLET_URLS = {
    'leaflet.js': `${LEAFLET_BASE}leaflet.js`,
    'leaflet.css': `${LEAFLET_BASE}leaflet.css`,
};
const LEAFLET_LICENSE_URL = `https://unpkg.com/leaflet@${LEAFLET_VERSION}/LICENSE`;
const PROVENANCE_PATH = join(VENDOR_DIR, 'provenance.json');

const sha256 = (buf) => createHash('sha256').update(buf).digest('hex');

// ---------------- one-time download (--fetch) ----------------

async function fetchPinned() {
    await mkdir(VENDOR_DIR, { recursive: true });
    console.log(`Fetching Leaflet ${LEAFLET_VERSION} dist (pinned, once):`);
    const files = {};
    for (const [name, url] of Object.entries(LEAFLET_URLS)) {
        console.log(`  ${url}`);
        const res = await fetch(url);
        if (!res.ok) throw new Error(`download failed (${res.status}) for ${url}`);
        const buf = Buffer.from(await res.arrayBuffer());
        await writeFile(join(VENDOR_DIR, name), buf);
        files[name] = { sha256: sha256(buf), bytes: buf.length };
    }
    // License notice (BSD 2-Clause — confirmed on the distribution page)
    console.log(`  ${LEAFLET_LICENSE_URL}`);
    const lic = await fetch(LEAFLET_LICENSE_URL);
    if (!lic.ok) throw new Error(`license download failed (${lic.status})`);
    const licBuf = Buffer.from(await lic.arrayBuffer());
    await writeFile(join(VENDOR_DIR, 'LICENSE'), licBuf);

    const provenance = {
        source: LEAFLET_BASE,
        version: LEAFLET_VERSION,
        retrieved: new Date().toISOString().slice(0, 10),
        license: 'BSD 2-Clause',
        files,
    };
    await writeFile(PROVENANCE_PATH, JSON.stringify(provenance, null, 2) + '\n');
    console.log('vendor/ populated + vendor/provenance.json written');
}

// ---------------- header generation ----------------

function cArray(name, bytes) {
    const lines = [];
    for (let i = 0; i < bytes.length; i += 16) {
        lines.push('    ' + Array.from(bytes.slice(i, i + 16),
            (b) => `0x${b.toString(16).padStart(2, '0')}`).join(', '));
    }
    // Join deterministically: one row per line, trailing comma on every row
    // except handled by the join below (rows joined with ',\n')
    const body = lines.join(',\n');
    return `static const uint8_t ${name}[] PROGMEM = {\n${body}\n};\n`;
}

async function generate() {
    const provenance = JSON.parse(await readFile(PROVENANCE_PATH, 'utf8'));
    const licenseText = (await readFile(join(VENDOR_DIR, 'LICENSE'), 'utf8')).trimEnd();
    const licenseBlock = licenseText.split('\n').map((l) => `//   ${l}`).join('\n');

    const jsBuf = await readFile(join(VENDOR_DIR, 'leaflet.js'));
    const cssBuf = await readFile(join(VENDOR_DIR, 'leaflet.css'));

    // Supply-chain lock: the vendor bytes must still hash to what was
    // fetched from the pinned URL — a swapped file fails the build.
    for (const [name, buf] of [['leaflet.js', jsBuf], ['leaflet.css', cssBuf]]) {
        const h = sha256(buf);
        if (provenance.files[name] && provenance.files[name].sha256 !== h) {
            throw new Error(
                `sha256 mismatch for vendor/${name}: recorded ` +
                `${provenance.files[name].sha256}, found ${h} — refusing to embed`);
        }
    }

    // Max-level gzip — node zlib emits no timestamp, so identical input
    // regenerates the identical header byte-for-byte (idempotent).
    const jsGz = gzipSync(jsBuf, { level: zlibConstants.Z_BEST_COMPRESSION });
    const cssGz = gzipSync(cssBuf, { level: zlibConstants.Z_BEST_COMPRESSION });

    const header =
`// ===========================
// web_assets.h — GENERATED, DO NOT EDIT BY HAND
// Regenerate with: node scripts/embed_web_assets.mjs
// ===========================
// Vendored Leaflet ${LEAFLET_VERSION} (map library for WEB-02), gzipped and
// embedded in PROGMEM so the dashboard fully loads in AP mode with no
// internet — the page NEVER references a CDN at runtime. Only the OSM
// tiles are internet-dependent (browser-fetched); when they fail the
// D-37 offline canvas fallback renders the identical trajectory.
//
// Provenance (supply-chain lock, T-03-SC):
//   Pinned source : ${LEAFLET_BASE}
//   Retrieved     : ${provenance.retrieved} (one-time download, never a mirror)
//   leaflet.js    sha256 ${sha256(jsBuf)}  (${jsBuf.length} B raw -> ${jsGz.length} B gzip)
//   leaflet.css   sha256 ${sha256(cssBuf)}  (${cssBuf.length} B raw -> ${cssGz.length} B gzip)
//
// License — ${provenance.license} (notice retained verbatim below):
${licenseBlock}
//

#ifndef WEB_ASSETS_H
#define WEB_ASSETS_H

#include <Arduino.h>

${cArray('LEAFLET_JS_GZ', jsGz)}
static constexpr size_t LEAFLET_JS_GZ_LEN = sizeof(LEAFLET_JS_GZ);

${cArray('LEAFLET_CSS_GZ', cssGz)}
static constexpr size_t LEAFLET_CSS_GZ_LEN = sizeof(LEAFLET_CSS_GZ);

#endif // WEB_ASSETS_H
`;

    await writeFile(HEADER_PATH, header);
    console.log(`src/web_assets.h regenerated:`);
    console.log(`  leaflet.js  ${jsBuf.length} -> ${jsGz.length} B gzipped`);
    console.log(`  leaflet.css ${cssBuf.length} -> ${cssGz.length} B gzipped`);
}

// ---------------- entry ----------------

if (process.argv.includes('--fetch')) {
    await fetchPinned();
}
await generate();
