---
phase: "03"
plan: "04"
subsystem: "gallery"
tags: [sd-storage, gallery, http-routes, web-ui, pagination, untrusted-parsing]
requires:
  - "D-29..D-32 flat /images layout and finalizeImage sidecar writer from 02-03 (IMG_{id}.JPG / IMG_{id}_T.JPG / IMG_{id}.JSON / IMG_{id}_T.JSON, %05u ids)"
  - "existing /img/{id} and /img/{id}_t.jpg serving routes (02-03 / 03-01)"
  - "/api/state poll pipeline and diff-render helper idiom (03-01)"
provides:
  - "SdStorage enumeration API: SdGalleryEntry, SD_GALLERY_MAX_ENTRIES (1000), SD_GALLERY_PAGE_SIZE (12), buildIndex/ensureIndexCurrent/getIndexPage/getPageCount/getTotalCount/findIndexEntry, getIndexVersion/getIndexedVersion, readSidecarMeta, SD_SC_PRESENT_* bits"
  - "GET /gallery?page=N list route (WR-07 page discipline, honest empty listing when SD unavailable) and GET /gallery/{id} detail route via handleNotFound dispatch"
  - "/api/state galleryCount — the browser's only gallery list-refresh signal (D-36)"
  - "gallery grid + pager + inline detail card UI with amber Incomplete badges in grid and detail"
affects:
  - "src/main_basestation.cpp served page: gallery section (last in D-45 order) replaces the interim Latest Capture card; renderState's latest-thumb diff-gate removed"
tech-stack:
  added: []
  patterns:
    - "boot-built RAM index with lazy version-gated rebuild — pagination slices the index, never the directory (Pitfall 5 / T-03-11)"
    - "presence-bit sidecar parsing: per-char validation, bounded copies, numeric clamps, absent-field omission (T-03-09)"
    - "strictly-numeric id dispatch in handleNotFound for parameterized routes (handleImage discipline, T-03-10)"
key-files:
  created: []
  modified:
    - include/sd_storage.h
    - src/sd_storage.cpp
    - src/main_basestation.cpp
decisions:
  - "Gallery index is a fixed static RAM array (1000 entries, ~12 KB static, no heap) built once at boot and rebuilt ONLY when finalizeImage's indexVersion advances — never per request"
  - "readSidecarMeta exposes SD_SC_PRESENT_* presence bits so /gallery/{id} serializes ONLY sidecar-carried fields; gpsValid derives from the writer's null-vs-numeric telemetry triple (null triple IS the GPS-no-fix case)"
  - "complete derives from the FULL sidecar only — an entry with just thumbnail data is not complete and carries the amber Incomplete badge in grid and detail (never hidden, D-48)"
  - "Latest Capture card retired: the newest grid item (first tile of page 1) is the latest-capture surface; latestThumbId stays in /api/state JSON, /img routes unchanged"
metrics:
  duration: "19m"
  completed: "2026-08-19"
status: complete
actuals:
  tokens: 13114
  tasks: 2
  commits: 2
---

# Phase 03 Plan 04: Image Gallery Summary

**One-liner:** Boot-built RAM index over /images with paginated /gallery list + sidecar-parsed /gallery/{id} detail routes and a grid/pager/inline-detail gallery UI that retires the interim Latest Capture card.

## What Was Built

### SdStorage enumeration (include/sd_storage.h, src/sd_storage.cpp)

- `SdGalleryEntry {id, hasThumb, hasFull, hasThumbSidecar, hasFullSidecar, fullSize}` and a fixed static index (`SD_GALLERY_MAX_ENTRIES = 1000` editable constant — beyond it the newest 1000 show, documented honest degradation; files never deleted). Sorted descending by id (ids unique + monotonic in flight → newest-first total order).
- `buildIndex()` — one `openNextFile()` walk at boot (after mount succeeds); `parseGalleryName` accepts only `IMG_` + exactly 5 per-char digits + one of `.JPG`/`_T.JPG`/`.JSON`/`_T.JSON` with strtol range 1..0xFFFF. `mergeIntoIndex` find-or-create with newest-kept eviction at cap; `sortIndexDescending` via qsort.
- Lazy refresh: `finalizeImage` (both kinds, all paths including degraded) bumps `indexVersion`; `ensureIndexCurrent()` rebuilds only when versions differ. Handlers call it at request top — no per-request directory rescan, no per-poll work.
- `readSidecarMeta(id, thumb, out)` — untrusted-sidecar parser (T-03-09): 1 KB bounded read, `jsonValuePos` exact-key location, `jsonStringAt` bounded copy rejecting overlong values, `jsonUintAt`/`jsonIntAt`/`jsonFloatAt` with per-char digit pre-checks and clamps (lat ±90, lon ±180, altitudeM [-1000, 100000], int8/uint8 fields, chunks ≤ 0xFFFF), `captureSourceFromName` locked-vocabulary reverse mapping (unknown → 0xFF → "unknown"). Telemetry triple all-or-nothing; camera 7-field block all-or-nothing; chunks pair both-or-neither. `SD_SC_PRESENT_*` bits record what the sidecar actually carried — absent fields omitted downstream, never zero-filled.

### Routes (src/main_basestation.cpp)

- `GET /gallery?page=N` — WR-07 discipline: absent page → 1; non-numeric or < 1 → 400; > pageCount → 400. Responds `{page, pageCount, total, entries:[{id, hasThumb, hasFull, complete}]}`; per-entry `complete` reads the FULL sidecar only. SD initFailed → honest empty listing `{page:1,pageCount:1,total:0,entries:[]}`, never an error page.
- `GET /gallery/{id}` via `handleNotFound` dispatch after the `/img/` dispatch — strictly-numeric id (handleImage discipline, 1..0xFFFF, validated before any file access, T-03-10). Full sidecar preferred, thumbnail sidecar fallback. Serializes `{id, complete, hasFull}` plus ONLY present fields (`capturedMs`, `trigger` via locked `triggerSourceName`, `gpsValid`, `altitudeM/lat/lon`, `camera{...}`, `chunksReceived/chunksTotal/percent` with percent clamped ≤ 100). 404 when neither sidecar nor any indexed file exists.
- `/api/state` gains `"galleryCount"` (after `ensureIndexCurrent()`) — the client's only list-refresh signal.

### Gallery UI (D-46/D-47/D-48)

- Image Gallery section LAST in the D-45 order with a "Gallery" nav link; `.gallery-grid` is `repeat(auto-fill, minmax(200px, 1fr))` with 16px gap; tiles load via the existing `/img/{id}_t.jpg` route (no duplicate serving path). Filenames never render on grid items.
- Amber Incomplete chip (`#713f12` bg / `#fbbf24` text / `#eab308` border, 2px vertical padding) overlaid top-right on incomplete tiles AND in the detail card — incomplete images stay listed, never hidden/filtered/recycled (D-48).
- Pager: Previous/Next standard buttons + "Page {n} of {m}" between them + numbered standard buttons, centered; hidden entirely on a single page; previous grid stays visible until the new page arrives (E4). Empty state: locked copy "No images received yet — trigger a capture to start." with no pager.
- Inline detail card (no modal): full `/img/{id}` when the sidecar says complete, thumbnail otherwise with badge still visible; definition list (Captured / Trigger / Altitude / Position / Camera / Chunks n/m · percent / Status) rendering ONLY present fields, 14px values `#e2e8f0` + 14px muted labels, values wrapping; "Position: GPS no fix" when `gpsValid` false; Back to Gallery restores the grid without a refetch unless `galleryCount` changed meanwhile.
- List refresh discipline (D-36): the 5s poll calls `onGalleryCount(data.galleryCount)`; the list fetches once on load and again ONLY when the count changes — never per poll; a stale-response guard (`galleryFetchSeq`) drops out-of-order list responses.
- Retirement: interim Latest Capture card markup, inline styles, and the renderState latest-thumb diff-gate removed; the literal "Latest Capture" is gone from the served page. `latestThumbId` JSON field and both `/img` routes remain in use.
- All sidecar-derived strings reach the DOM via textContent/createElement only (T-03-11b).

## Task Log

| Task | Name | Commit | Key files |
| ---- | ---- | ------ | --------- |
| 1 | SdStorage enumeration + RAM index + /gallery routes (D-46/D-47 data half) | 6948a5a | include/sd_storage.h, src/sd_storage.cpp, src/main_basestation.cpp |
| 2 | Gallery UI grid/pager/badge/inline detail; retire Latest Capture card (D-46/D-47/D-48) | 1c50511 | src/main_basestation.cpp |

## Verification Evidence

- Task 1 gate: `pio run -e esp32-s3-basestation` SUCCESS (RAM 20.8%, Flash 36.2%); `pio run -e esp32-s3-balloon` SUCCESS (RAM 41.0%, Flash 15.2%); all six acceptance greps pass (`getIndexPage` in header, `openNextFile` in sd_storage.cpp, `"/gallery"` + `galleryCount` + in main_basestation.cpp, `readSidecarMeta` in header); `node scripts/verify_protocol_roundtrip.mjs` — all wire-format regression checks passed.
- Task 2 gate: `pio run -e esp32-s3-basestation` SUCCESS (RAM/Flash within budget); all seven acceptance greps pass including `minmax(200px`, `Back to Gallery`, `No images received yet`, `Incomplete`, and the negative gate `! grep -q "Latest Capture"`.
- Post-retirement sweep: `thumb-img` / `thumb-label` / "Waiting for first image" have zero remaining references; `latestThumbId` remains only as the /api/state JSON field (per plan).
- Hardware UAT deferred per plan (flight with completes AND an induced incomplete; page-seam no-gap/no-duplicate check) — tracked for the phase verifier.

## Deviations from Plan

None - plan executed exactly as written.

## Requirements Coverage

- **IMG-06 — Paginated image gallery of all stored images**: delivered end-to-end — boot-enumerated newest-first index, 12-per-page grid with pager, sidecar-rich detail views, honest Incomplete badging, empty and SD-unavailable states.

## Known Stubs

None — no placeholder data paths; every rendered value derives from the index walk or a parsed sidecar.

## Self-Check: PASSED

All four key files exist on disk; both task commits (6948a5a, 1c50511) present in git history.
