---
created: 2026-08-27T11:23:49Z
title: Thumb-first image delivery with on-demand full retrieval
area: general
severity: major
files:
  - src/image_tx_manager.cpp
  - include/image_protocol.h
  - src/command_handler.cpp
  - src/main_basestation.cpp
---

## Problem

Today the balloon auto-transmits the full-resolution image after every capture. On a
LoRa link of 240-byte packets, a known-bad (blurry) capture burns long airtime for an
image the operator will discard on sight. The operator wants: balloon transmits ONLY
the thumbnail initially; the base web UI shows a button to request/retrieve the full
image on demand if the thumb looks good.

User-confirmed routing (2026-08-27): this becomes a NEW roadmap phase (two separate
phases agreed for this + the antenna-pointing feature), planned AFTER bench session #6
/ plan 01-20 completes.

## Solution

Behavior change on the balloon auto-transmit path (thumb-only after capture) plus a
new on-demand full-image request command and a web UI button. The protocol already
has the machinery: imageKind byte in the 0x13 chunk frame, THUMB_MAX_BYTES bound,
thumb/full manifests, and the window-request path (IMAGE_WINDOW_REQUEST with
NACK_BUSY deferral from 01-15) — the full-retrieval request likely rides the existing
window/manifest mechanism rather than a new frame type. Considerations for planning:
interaction with the 01-18 channel-quiet gate (full-image bursts are exactly the
chunk storms commands must wait out), retention policy for un-requested fulls on the
balloon (buffer lifetime), and how the button states map to manifest states
(thumb-complete-but-full-not-requested).

Related but separate: pending todo 2026-08-23-full-resolution-gallery-image-viewer
(presentation-side lightbox for fulls already on the base — not superseded by this).
