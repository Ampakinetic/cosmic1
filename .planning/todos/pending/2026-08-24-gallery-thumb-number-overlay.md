---
created: 2026-08-24T13:16:00Z
title: Gallery tile image-number overlay
area: ui
severity: minor
files:
  - src/main_basestation.cpp:1674-1691
---

## Problem

Operator bench friction (01-12 session #4, 2026-08-24): correlating a gallery tile with the console logs requires opening the detail view — the grid tile itself carries no visible image id. `renderGalleryList` (src/main_basestation.cpp:1674-1691) builds each `gallery-item` from just the thumbnail `img` (the id lives only in the `alt` text) plus the optional D-48 Incomplete chip. During bench rounds the operator constantly cross-references "image 7 thumb 35/36" style console lines against what the gallery shows, and had to click through tiles to map them.

Routing: Phase 03-enhanced-web-interface backlog (gallery UX territory, sibling of the 2026-08-23 full-resolution-viewer todo). Surfaced at the 01-12 bench because unspaced multi-capture sessions made tile-to-log correlation a per-session chore.

## Solution

Add a small always-visible id label to each tile (e.g. a `#N` corner badge absolutely positioned over the thumbnail, `N` from `e.id`), styled to coexist with the D-48 Incomplete chip without overlapping it (stack them in opposite corners). Pure presentation: HTML/CSS in the PROGMEM footer inside `renderGalleryList`, no firmware data-path or API change — `/gallery` already returns `id` per entry. Keep the D-48 constraint untouched: the Incomplete badge behavior itself does not change.
