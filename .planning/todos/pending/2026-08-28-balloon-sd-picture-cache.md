---
created: 2026-08-28T16:45:00Z
title: Balloon SD-card picture cache (write-through + boot re-serve)
area: firmware
severity: major
files:
  - src/main_balloon.cpp
  - src/image_tx_manager.cpp
  - include/image_protocol.h
  - src/command_handler.cpp
---

## Problem

Every captured image lives only in volatile PSRAM buffers (ImageTxManager entries)
whose sole purpose is streaming over LoRa. Any crash, reboot, or power loss destroys
them: images 35, 36, 38, 39, and 43 were all lost exactly this way across the D1
crash sessions (G-01-10 / WINDOWS 15). Post-crash, the base keeps sending window
requests for the lost images and the rebooted balloon can only reject them
("window request for unknown/evicted image 43 rejected", balloon4.log:1640/:1648/:1674)
— a NACK storm with zero recovery value.

Independent of crashes: a high-altitude balloon that is lost or recovered damaged
currently loses every picture that was not already delivered to the base. Volatile-only
storage is a mission-level operational risk.

## Proposal

1. **Write-through at capture:** persist the full (and thumbnail) image bytes to the
   balloon's on-board SD card when the image is captured/enqueued, before or alongside
   the PSRAM buffer being staged for TX.
2. **Boot-time re-scan:** at boot, scan the SD cache for images whose delivery never
   completed (persist a tiny delivered/unserved marker per image — sidecar or marker file).
3. **Re-announce + re-serve from disk:** re-announce unserved entries (bounded, reusing
   the 01-17/01-21 receipt-informed re-announce machinery) and serve window requests for
   rehydrated entries by streaming reads from SD instead of rejecting them.

## What this does NOT do (honest scoping)

- It will NOT fix D1. Session #10 re-confirmed memory is healthy (heap 8.55 MB,
  PSRAM 8.35 MB) and the crash is a stack stomp in the CPU0 tick/interrupt path
  (IDLE0 canary + memset). SD is a durability feature, not a crash fix.
- Sequencing: do NOT add the SD transport while D1's discriminator rounds are active —
  a new ISR/DMA source would contaminate the system under test. Land this AFTER the
  D1 stack-overflow question is dispositioned (round #14 brief, debug doc §10).

## Hardware notes (operator-confirmed 2026-08-28)

- The balloon board HAS a wired SD slot (operator confirmation at session-#10 close-out).
- BEFORE planning: audit the slot's pin mapping against the OPI-PSRAM conflict —
  GPIO 35/36/37 are forbidden on this board (the GPS-on-35 bootloop lesson, fixed 3f2c2c6).
  Prefer SD_MMC 1-bit if the slot maps to safe pins (the BASE board uses SD_MMC 1-bit
  CLK=39/CMD=38/DATA=40 — that mapping is the base's, not necessarily the balloon's);
  otherwise SPI-SD on free GPIOs. Also verify SDMMC/camera coexistence on this module.
- Balloon flash budget: firmware is at 39.0% — a FAT + transport stack fits, but confirm
  no PSRAM/DRAM regression with the camera pipeline.
