#!/usr/bin/env node
/**
 * fetch_nz_maps.mjs — offline basemap tile fetcher for the Cosmic1 base
 * station SD card (feature: offline maps).
 *
 * Downloads New Zealand XYZ WebP tiles from LINZ Basemaps
 * (https://basemaps.linz.govt.nz) into the SD layout the firmware serves:
 *
 *     <out>/{layer}/{z}/{x}/{y}.webp     +  <out>/manifest.json
 *
 * Layers:
 *   aerial     NZ Aerial Imagery (satellite view)  — KEYLESS, CC BY 4.0
 *   topo       NZ Topo50-derived raster (map view) — free LINZ API key required
 *   hillshade  DEM hillshade (terrain relief)      — free LINZ API key required
 *
 * The key is the `api` query parameter; pass it via --api-key or LINZ_API_KEY.
 * Get a free key: https://data.linz.govt.nz  (register -> My API Keys)
 *
 * Storage economy: LINZ renders ocean as full-size imagery (~17 KB/tile, no
 * placeholder to dedupe against), so a naive NZ bounding-box run would spend
 * ~80% of requests and bytes on empty sea. This script masks requests with
 * Natural Earth land polygons (3x3 point-in-polygon sampling + 1-tile dilation
 * so coastlines keep their surrounding water) — roughly a 5x cut.
 *
 * Resumable: an existing non-empty tile file is kept, so the run can be
 * Ctrl-C'd and re-launched. --fresh re-fetches.
 *
 * Usage:
 *   node scripts/fetch_nz_maps.mjs --preset pilot
 *   node scripts/fetch_nz_maps.mjs --preset country --layer aerial
 *   node scripts/fetch_nz_maps.mjs --layer topo --api-key XXXX
 *   node scripts/fetch_nz_maps.mjs --bbox 174.6,-41.45,175.1,-41.05 --zmin 12 --zmax 14
 *
 * Zero dependencies — Node >= 18 built-in fetch.
 */

import { appendFileSync, createWriteStream, existsSync, mkdirSync, readFileSync, statSync, writeFileSync } from 'node:fs';
import { open as fsOpen } from 'node:fs/promises';
import path from 'node:path';
import { fileURLToPath } from 'node:url';

const ROOT = path.dirname(path.dirname(fileURLToPath(import.meta.url)));
const DEFAULT_OUT = path.join(ROOT, 'maps-sd');
const WORK_DIR = path.join(ROOT, 'maps-work');

const BASEMAPS_HOST = 'https://basemaps.linz.govt.nz';
const TILE_MATRIX = 'WebMercatorQuad';
const USER_AGENT = 'cosmic1-balloon-tracker-offline-maps/1.0 (personal balloon project)';

// NZ mainland + Stewart Island + nearshore islands. The Chatham Islands sit
// at ~-176.5 deg (183.5 in 0..360 terms) — deliberately excluded; add
// --bbox 183.3,-44.5,184.0,-43.4 if the balloon ever flies from there.
const NZ_BBOX = { w: 166.2, s: -47.6, e: 178.6, n: -34.2 };
// Wellington region pilot: a few hundred tiles, proves the pipeline fast.
const PILOT_BBOX = { w: 174.55, s: -41.45, e: 175.15, n: -41.05 };

const PRESETS = {
    country: { bbox: NZ_BBOX, zmin: 5, zmax: 14 },
    pilot:   { bbox: PILOT_BBOX, zmin: 10, zmax: 13 },
};

const LAND_SOURCES = [
    'https://raw.githubusercontent.com/martynafford/natural-earth-geojson/master/50m/physical/ne_50m_land.json',
    'https://raw.githubusercontent.com/martynafford/natural-earth-geojson/master/10m/physical/ne_10m_land.json',
];

const KNOWN_LAYERS = new Set(['aerial', 'topo', 'hillshade']);

// Sources. "basemaps" = basemaps.linz.govt.nz (aerial keyless, webp).
// "lds" = the LINZ Data Service tile CDN (needs an LDS API key, png) — the
// topo/hillshade renders there; note the style slot MUST be omitted from
// the path (",auto" trips the CDN token check) and tiles-a..d are
// round-robin load-balancer subdomains per LDS guidance.
const LDS_LAYER_IDS = { topo: '50767', hillshade: '50765' };

// ---------------- CLI ----------------

function parseArgs(argv) {
    const opts = {
        layers: ['aerial'],
        preset: 'country',
        bbox: null,
        zmin: null,
        zmax: null,
        out: DEFAULT_OUT,
        apiKey: process.env.LINZ_API_KEY || '',
        rps: 10,
        dryRun: false,
        fresh: false,
        noMask: false,
        source: 'basemaps',
        skipList: '',
    };
    for (let i = 2; i < argv.length; i++) {
        const a = argv[i];
        const val = () => argv[++i];
        if (a === '--layer') opts.layers = val().split(',').map(s => s.trim()).filter(Boolean);
        else if (a === '--preset') opts.preset = val();
        else if (a === '--bbox') {
            const [w, s, e, n] = val().split(',').map(Number);
            if ([w, s, e, n].some(v => !Number.isFinite(v)) || !(e > w) || !(n > s)) {
                fail('--bbox expects W,S,E,N in degrees, e.g. --bbox 166.2,-47.6,178.6,-34.2');
            }
            opts.bbox = { w, s, e, n };
        }
        else if (a === '--zmin') opts.zmin = intVal(a, val());
        else if (a === '--zmax') opts.zmax = intVal(a, val());
        else if (a === '--out') opts.out = path.resolve(val());
        else if (a === '--api-key') opts.apiKey = val();
        else if (a === '--rps') opts.rps = Math.max(1, intVal(a, val()));
        else if (a === '--dry-run') opts.dryRun = true;
        else if (a === '--fresh') opts.fresh = true;
        else if (a === '--no-mask') opts.noMask = true;
        else if (a === '--source') opts.source = val();
        else if (a === '--skip-list') opts.skipList = val();
        else fail(`Unknown option: ${a}`);
    }
    if (opts.source !== 'basemaps' && opts.source !== 'lds') {
        fail("--source must be 'basemaps' (keyless aerial, webp) or 'lds' (LDS key, png)");
    }
    if (!opts.layers.length || opts.layers.some(l => !KNOWN_LAYERS.has(l))) {
        fail(`--layer must be one or more of ${[...KNOWN_LAYERS].join(', ')}`);
    }
    for (const l of opts.layers) {
        if (opts.source === 'lds') {
            if (!LDS_LAYER_IDS[l]) {
                fail(`Layer "${l}" has no LDS layer id — LDS serves: ${Object.keys(LDS_LAYER_IDS).join(', ')}`);
            }
            if (!opts.apiKey) {
                fail(`--source lds needs an LDS API key (--api-key or LINZ_API_KEY env). ` +
                     'Free key: https://data.linz.govt.nz (register -> My API Keys)');
            }
        } else if (l !== 'aerial' && !opts.apiKey) {
            fail(`Layer "${l}" on basemaps needs a developer key (email basemaps@linz.govt.nz), ` +
                 `or use --source lds with a free LDS key (serves: ${Object.keys(LDS_LAYER_IDS).join(', ')})`);
        }
    }
    return opts;
}

function intVal(flag, raw) {
    const n = Number(raw);
    if (!Number.isInteger(n)) fail(`${flag} expects an integer, got "${raw}"`);
    return n;
}

function fail(msg) {
    console.error(`error: ${msg}`);
    process.exit(2);
}

// ---------------- slippy-tile math ----------------

function lonToX(lon, z) { return ((lon + 180) / 360) * 2 ** z; }

function latToY(lat, z) {
    const rad = (lat * Math.PI) / 180;
    return ((1 - Math.log(Math.tan(rad) + 1 / Math.cos(rad)) / Math.PI) / 2) * 2 ** z;
}

// Tile bounds in lon/lat for (z,x,y) — top-left origin (XYZ, matching both
// Leaflet's default tms:false and LINZ's WebMercatorQuad).
function tileBounds(z, x, y) {
    const n = 2 ** z;
    const lonMin = (x / n) * 360 - 180;
    const lonMax = ((x + 1) / n) * 360 - 180;
    const yMax = y, yMin = y + 1;
    const invY = (yy) => Math.atan(Math.sinh(Math.PI * (1 - (2 * yy) / n))) * (180 / Math.PI);
    return { lonMin, lonMax, latMax: invY(yMin), latMin: invY(yMax) };
}

// ---------------- land mask ----------------

async function loadLandMask() {
    const cache = path.join(WORK_DIR, 'ne_land.json');
    if (existsSync(cache) && statSync(cache).size > 1000) {
        console.log('land mask: cached Natural Earth polygons');
        return JSON.parse(readFileSync(cache, 'utf8'));
    }
    mkdirSync(WORK_DIR, { recursive: true });
    for (const url of LAND_SOURCES) {
        process.stdout.write(`land mask: fetching ${url.split('/').slice(-2).join('/')} ... `);
        try {
            const res = await fetch(url, { headers: { 'User-Agent': USER_AGENT } });
            if (!res.ok) { console.log(`HTTP ${res.status}, trying next source`); continue; }
            const text = await res.text();
            const geo = JSON.parse(text);
            if (geo.type !== 'FeatureCollection' || !geo.features?.length) {
                console.log('unexpected GeoJSON shape, trying next source');
                continue;
            }
            writeFileSync(cache, text);
            console.log(`ok (${(text.length / 1e6).toFixed(1)} MB, ${geo.features.length} polygons)`);
            return geo;
        } catch (e) {
            console.log(`failed (${e.message}), trying next source`);
        }
    }
    return null;
}

// Compact polygon records: [outerRing, hole?, ...] -> { bbox, rings }
function buildMaskPolygons(geo, bbox) {
    const polys = [];
    const addRings = (geom) => {
        if (geom.type === 'Polygon') {
            push(geom.coordinates);
        } else if (geom.type === 'MultiPolygon') {
            for (const p of geom.coordinates) push(p);
        }
    };
    const push = (rings) => {
        // bbox over the outer ring only (ring 0); holes are handled by PIP
        let bx0 = Infinity, by0 = Infinity, bx1 = -Infinity, by1 = -Infinity;
        for (const [lon, lat] of rings[0]) {
            if (lon < bx0) bx0 = lon;
            if (lon > bx1) bx1 = lon;
            if (lat < by0) by0 = lat;
            if (lat > by1) by1 = lat;
        }
        // keep only polygons that can possibly touch our bbox
        if (bx1 < bbox.w || bx0 > bbox.e || by1 < bbox.s || by0 > bbox.n) return;
        polys.push({ bbox: [bx0, by0, bx1, by1], rings });
    };
    for (const f of geo.features) addRings(f.geometry);
    return polys;
}

function pointInRings(lon, lat, rings) {
    // even-odd ray cast; ring 0 = outer (inside), inner rings = holes (outside)
    let inside = false;
    for (let r = 0; r < rings.length; r++) {
        const ring = rings[r];
        let ringHit = false;
        for (let i = 0, j = ring.length - 1; i < ring.length; j = i++) {
            const [xi, yi] = ring[i], [xj, yj] = ring[j];
            if ((yi > lat) !== (yj > lat) &&
                lon < ((xj - xi) * (lat - yi)) / (yj - yi) + xi) {
                ringHit = !ringHit;
            }
        }
        if (ringHit) inside = !inside;
    }
    return inside;
}

function tileIsLand(z, x, y, polys) {
    const b = tileBounds(z, x, y);
    // 3x3 sample grid: a sliver of land in any ninth keeps the tile, so
    // coastlines render fully instead of getting bitten out
    for (let sy = 0; sy < 3; sy++) {
        const lat = b.latMin + ((b.latMax - b.latMin) * (sy + 0.5)) / 3;
        for (let sx = 0; sx < 3; sx++) {
            const lon = b.lonMin + ((b.lonMax - b.lonMin) * (sx + 0.5)) / 3;
            for (const p of polys) {
                const [bx0, by0, bx1, by1] = p.bbox;
                if (lon < bx0 || lon > bx1 || lat < by0 || lat > by1) continue;
                if (pointInRings(lon, lat, p.rings)) return true;
            }
        }
    }
    return false;
}

// ---------------- tile enumeration + dilation ----------------

function* enumerateTiles(bbox, zmin, zmax) {
    for (let z = zmin; z <= zmax; z++) {
        const x0 = Math.floor(lonToX(bbox.w, z));
        const x1 = Math.floor(lonToX(bbox.e, z));
        const y0 = Math.floor(latToY(bbox.n, z));
        const y1 = Math.floor(latToY(bbox.s, z));
        for (let x = x0; x <= x1; x++) {
            for (let y = y0; y <= y1; y++) {
                yield { z, x, y };
            }
        }
    }
}

// Land-mask a zoom's candidates, then dilate by one tile so every coastal
// tile keeps a ring of water around the shoreline (LINZ renders ocean as
// imagery, and a hard mask edge at the coast looks saw-toothed).
function maskZoom(tiles, polys, maskBelowZoom) {
    if (!polys || tiles.z < maskBelowZoom) return tiles.list;
    const land = new Set();
    for (const t of tiles.list) {
        if (tileIsLand(t.z, t.x, t.y, polys)) land.add(t.x * 1e6 + t.y);
    }
    const kept = [];
    for (const t of tiles.list) {
        const isLand = land.has(t.x * 1e6 + t.y);
        let keep = isLand;
        if (!keep) {
            outer: for (let dx = -1; dx <= 1; dx++) {
                for (let dy = -1; dy <= 1; dy++) {
                    if (land.has((t.x + dx) * 1e6 + (t.y + dy))) { keep = true; break outer; }
                }
            }
        }
        if (keep) kept.push({ z: t.z, x: t.x, y: t.y, land: isLand });
    }
    return kept;
}

// ---------------- fetch pipeline ----------------

class RateLimiter {
    constructor(rps) {
        this.intervalMs = 1000 / rps;
        this.next = 0;
    }
    async take() {
        const now = Date.now();
        this.next = Math.max(this.next, now);
        this.next += this.intervalMs;
        const wait = this.next - now;
        if (wait > 0) await sleep(wait);
    }
}

const sleep = (ms) => new Promise((r) => setTimeout(r, ms));

// Round-robin across the LDS load-balancer subdomains (tiles-a..d)
let ldsHostCursor = 0;

// A tile under this size is never meaningful imagery: LINZ CDN glitches
// occasionally return a ~116 B blank tile with HTTP 200, and storing one
// would bake a permanent gray hole into the card. Treated as retryable.
const BLANK_BYTES = 300;

function tileUrl(source, layer, z, x, y, apiKey) {
    if (source === 'lds') {
        const sub = 'abcd'[ldsHostCursor++ % 4];
        return `https://tiles-${sub}.data-cdn.linz.govt.nz/services;key=${encodeURIComponent(apiKey)}` +
               `/tiles/v4/layer=${LDS_LAYER_IDS[layer]}/EPSG:3857/${z}/${x}/${y}.png`;
    }
    let url = `${BASEMAPS_HOST}/v1/tiles/${layer}/${TILE_MATRIX}/${z}/${x}/${y}.webp`;
    if (apiKey) url += `?api=${encodeURIComponent(apiKey)}`;
    return url;
}

function tileLooksValid(source, buf) {
    if (buf.length < BLANK_BYTES) return 'blank/glitched tile';
    if (source === 'lds') {
        return (buf.length >= 8 && buf[0] === 0x89 && buf[1] === 0x50 && buf[2] === 0x4e && buf[3] === 0x47)
            ? null : 'not a png payload';
    }
    return (buf.length >= 16 && buf.readUInt32LE(0) === 0x46464952 && buf.readUInt32LE(8) === 0x50424557)
        ? null : 'not a webp payload';
}

async function fetchTile(source, layer, z, x, y, apiKey, isLand) {
    const url = tileUrl(source, layer, z, x, y, apiKey);
    let lastErr = '';
    for (let attempt = 1; attempt <= 3; attempt++) {
        try {
            const res = await fetch(url, {
                headers: { 'User-Agent': USER_AGENT },
                signal: AbortSignal.timeout(30000),
            });
            if (res.status === 429) {
                lastErr = 'HTTP 429';
                await sleep(30000);
                continue;
            }
            if (!res.ok) {
                lastErr = `HTTP ${res.status}`;
                // 400s from LINZ are deterministic coverage holes — retrying
                // just burns the three-attempt tax on every discovery; the
                // skip list makes them one-shot. 429/5xx/timeouts still retry.
                if (res.status === 400) break;
                await sleep(1000 * attempt);
                continue;
            }
            const buf = Buffer.from(await res.arrayBuffer());
            const invalid = tileLooksValid(source, buf);
            if (invalid) {
                // LDS topo: a <300B response is always STORED, never
                // retried — open water and outlying islands not in this
                // layer legitimately render as transparent near-empty PNGs
                // (Campbell/Snares/etc. live in separate Topo25/250 layers),
                // and a retry could never clear them. The one real glitch
                // seen (urban z11 blank) self-heals via a --fresh re-run.
                if (invalid === 'blank/glitched tile' && source === 'lds') {
                    return { ok: true, buf };
                }
                lastErr = invalid;
                await sleep(1000 * attempt);
                continue;
            }
            return { ok: true, buf };
        } catch (e) {
            lastErr = e.name === 'TimeoutError' ? 'timeout' : e.message;
            await sleep(1000 * attempt);
        }
    }
    return { ok: false, err: lastErr };
}

async function fetchLayer(layer, plan, opts) {
    const { zmin, zmax, bbox } = plan;
    const maskLabel = opts.noMask ? 'no mask' : 'land mask + 1-tile dilation';
    console.log(`\n[${layer}] zooms ${zmin}-${zmax}, bbox ${bbox.w},${bbox.s} .. ${bbox.e},${bbox.n} (${maskLabel})`);

    // Enumerate + mask per zoom (memory-safe: one zoom at a time)
    const maskPolys = opts.noMask ? null : (globalThis.__landPolys ?? null);
    const all = [];
    for (let z = zmin; z <= zmax; z++) {
        const rect = [...enumerateTiles(bbox, z, z)];
        const kept = maskZoom({ z, list: rect }, maskPolys, 8);
        all.push(...kept);
        console.log(`[${layer}] z${z}: ${rect.length} tiles in bbox -> ${kept.length} kept`);
    }
    console.log(`[${layer}] total tiles to ensure: ${all.length}`);

    if (opts.dryRun) return { tiles: all.length, bytes: 0, failed: 0, skipped: 0 };

    const layerDir = path.join(opts.out, layer);
    mkdirSync(layerDir, { recursive: true });

    const limiter = new RateLimiter(opts.rps);
    const stats = { tiles: all.length, bytes: 0, failed: 0, skipped: 0, fetched: 0 };
    let cursor = 0;
    const t0 = Date.now();

    async function worker() {
        while (true) {
            const i = cursor++;
            if (i >= all.length) return;
            const { z, x, y, land } = all[i];
            const tileKey = `${z}/${x}/${y}`;
            if (opts.skipSet && opts.skipSet.has(tileKey)) {
                stats.skipped++;
                continue;
            }
            const file = path.join(layerDir, String(z), String(x), `${y}.webp`);
            try {
                if (!opts.fresh && existsSync(file) && statSync(file).size > 0) {
                    stats.skipped++;
                    stats.bytes += statSync(file).size;
                } else {
                    await limiter.take();
                    const r = await fetchTile(opts.source, layer, z, x, y, opts.apiKey, land);
                    if (!r.ok) {
                        stats.failed++;
                        if (opts.skipList) {
                            opts.skipSet.add(tileKey);
                            appendFileSync(opts.skipList, tileKey + '\n');
                        }
                        console.warn(`[${layer}] FAIL ${z}/${x}/${y}: ${r.err}`);
                        continue;
                    }
                    mkdirSync(path.dirname(file), { recursive: true });
                    const fh = await fsOpen(file, 'w');
                    await fh.writeFile(r.buf);
                    await fh.close();
                    stats.bytes += r.buf.length;
                    stats.fetched++;
                }
            } catch (e) {
                stats.failed++;
                if (opts.skipList) {
                    opts.skipSet.add(tileKey);
                    appendFileSync(opts.skipList, tileKey + '\n');
                }
                console.warn(`[${layer}] FAIL ${z}/${x}/${y}: ${e.message}`);
            }
            const done = stats.skipped + stats.fetched + stats.failed;
            if (done % 250 === 0 || done === all.length) {
                const elapsed = (Date.now() - t0) / 1000;
                const rate = done / Math.max(elapsed, 1);
                const etaMin = ((all.length - done) / Math.max(rate, 0.01) / 60).toFixed(0);
                process.stdout.write(
                    `\r[${layer}] ${done}/${all.length} ` +
                    `(new ${stats.fetched}, kept ${stats.skipped}, fail ${stats.failed}) ` +
                    `${(stats.bytes / 1e6).toFixed(0)} MB, ${rate.toFixed(1)}/s, ETA ${etaMin} min   `);
            }
        }
    }
    await Promise.all(Array.from({ length: opts.rps >= 8 ? 8 : opts.rps }, worker));
    process.stdout.write('\n');
    console.log(`[${layer}] done: ${stats.fetched} fetched, ${stats.skipped} already on disk, ` +
                `${stats.failed} failed, ${(stats.bytes / 1e6).toFixed(1)} MB`);
    return stats;
}

// ---------------- manifest ----------------

function updateManifest(outDir, layer, zmin, zmax, stats, source) {
    const file = path.join(outDir, 'manifest.json');
    let manifest = {};
    if (existsSync(file)) {
        try { manifest = JSON.parse(readFileSync(file, 'utf8')); } catch { /* rewrite below */ }
    }
    manifest.format = 'linz-xyz-tiles';
    manifest.generated = new Date().toISOString();
    manifest.attribution = 'Sourced from LINZ. NZ Aerial Imagery and NZ Topo Maps, (c) LINZ, CC BY 4.0.';
    manifest.layers = manifest.layers || {};
    manifest.layers[layer] = {
        zmin,
        zmax,
        ext: source === 'lds' ? 'png' : 'webp',
        source: source === 'lds'
            ? `tiles.data-cdn.linz.govt.nz v4 layer=${LDS_LAYER_IDS[layer]} (LDS API key)`
            : `${BASEMAPS_HOST}/v1/tiles/${layer}/${TILE_MATRIX}/{z}/{x}/{y}.webp`,
        tiles: stats.tiles,
        bytes: stats.bytes,
        updated: manifest.generated,
    };
    // Trim stale layers the card no longer carries (manifest is re-writable
    // per layer; layers present on disk but absent from this run are kept)
    writeFileSync(file, JSON.stringify(manifest, null, 2) + '\n');
    console.log(`manifest: ${file}`);
}

// ---------------- main ----------------

const opts = parseArgs(process.argv);
const preset = PRESETS[opts.preset] && !opts.bbox ? PRESETS[opts.preset] : null;
if (!opts.bbox && !preset) fail(`unknown preset "${opts.preset}" (country | pilot) or pass --bbox`);
const plan = {
    bbox: opts.bbox ?? preset.bbox,
    zmin: opts.zmin ?? preset.zmin,
    zmax: opts.zmax ?? preset.zmax,
};
if (plan.zmin < 5 || plan.zmax > 16 || plan.zmin > plan.zmax) {
    fail('zoom range must satisfy 5 <= zmin <= zmax <= 16 (5..13 country-scale, 14 regional detail)');
}

console.log('Cosmic1 offline maps fetcher');
console.log(`  out   : ${opts.out}`);
console.log(`  layers: ${opts.layers.join(', ')}${opts.dryRun ? ' (DRY RUN)' : ''}`);
console.log(`  plan  : z${plan.zmin}-z${plan.zmax}, bbox ${plan.bbox.w},${plan.bbox.s},${plan.bbox.e},${plan.bbox.n}`);

if (!opts.noMask) {
    const geo = await loadLandMask();
    if (geo) {
        globalThis.__landPolys = buildMaskPolygons(geo, {
            w: plan.bbox.w - 1, s: plan.bbox.s - 1, e: plan.bbox.e + 1, n: plan.bbox.n + 1,
        });
        console.log(`land mask: ${globalThis.__landPolys.length} polygons near NZ`);
    } else {
        console.warn('land mask: UNAVAILABLE — continuing UNMASKED (expect ~5x more tiles/bytes)');
    }
}

mkdirSync(opts.out, { recursive: true });
console.log(`  source: ${opts.source}`);

// Skip list: coordinates (z/x/y per line) that permanently fail server-side
// (LINZ coverage holes — Fiordland z14 etc.). Skipped before any network
// call, and new hard failures are appended live so the NEXT run skips them
// too. Exists to stop the retry storm that poisoned the fetch pool twice.
opts.skipSet = new Set();
if (opts.skipList) {
    if (existsSync(opts.skipList)) {
        for (const line of readFileSync(opts.skipList, 'utf8').split('\n')) {
            const t = line.trim();
            if (t) opts.skipSet.add(t);
        }
        console.log(`skip list: ${opts.skipSet.size} known-missing tiles at ${opts.skipList}`);
    } else {
        writeFileSync(opts.skipList, '');
        console.log(`skip list: creating ${opts.skipList}`);
    }
}
let hadFailures = false;
for (const layer of opts.layers) {
    const stats = await fetchLayer(layer, plan, opts);
    if (stats.failed > 0) hadFailures = true;
    if (!opts.dryRun) updateManifest(opts.out, layer, plan.zmin, plan.zmax, stats, opts.source);
}
console.log(hadFailures ? '\nDONE with failures — re-run the same command to retry just the gaps.' : '\nDONE.');
process.exit(hadFailures ? 1 : 0);
