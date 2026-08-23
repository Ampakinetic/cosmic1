---
created: 2026-08-23T01:49:02Z
title: Full-resolution gallery image viewer
area: ui
severity: minor
files:
  - src/main_basestation.cpp:1744-1761
  - src/main_basestation.cpp:3441-3451
---

## Problem

Operator bench feedback (01-09 hardware session, iteration 5): "It would be nice to see the full image at full resolution, it currently doesn't look much bigger than the thumbnail."

The Phase 3 gallery detail view (D-47, `renderGalleryDetail` at src/main_basestation.cpp:1744-1761) renders the full via an `img.gallery-detail-img` whose card layout constrains it to near-thumbnail dimensions, so the full-resolution capture (up to SVGA 800x600 per the frame-size ladder) is displayed barely larger than its QQVGA thumbnail. The bytes are served correctly — `/img/{id}` (src/main_basestation.cpp:3441-3451) streams the SD file — so this is purely a presentation gap, not a data-path fault.

Routing: Phase 03-enhanced-web-interface backlog (gallery UX territory). Surfaced at the Phase 01 UAT bench because full-resolution viewing only became possible once transfers completed end-to-end (G-01-3 resolved).

## Solution

Enlarge the detail-view presentation, e.g. a lightbox/overlay (viewport-sized image, click-to-zoom, ESC/backdrop to close) or a width-unconstrained render with a dimension/file-size caption sourced from the sidecar. Constraints to keep: D-47 (full-size image only when the sidecar verified the image complete; thumbnail otherwise) and the D-48 Incomplete badge behavior. No firmware data-path changes expected — HTML/CSS in the PROGMEM footer + possibly a query param on /img/{id}.
