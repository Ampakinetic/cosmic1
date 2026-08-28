# Phase 2.5 Context: Balloon SD-Card File-Based Image Store

**Gathered:** 2026-08-29 (operator decisions via structured questioning after bench session #11)
**Status:** Context gathered — ready for planning

## Phase Goal

Re-architect the balloon's image pipeline around a FAT32 SD-card file store so that capture persists to file before manifest, window service reads chunks from the file, and undelivered images survive reboot — eliminating the volatile-queue eviction/supersede defect class that has generated gap-closure rounds #5–#14.

## Why Now (Evidence, bench session #11 — balloon5.log / base5.log, 2026-08-29)

- Round-#14's lever (IMG_TX_QUEUE_DEPTH 3→5 + supersede-path receipt-evidenced protection) ENGAGED and FAILED via a NEW over-rejection mode: `evictEntriesOlderThan` rejects window requests regardless of capacity need — image 45's SVGA FULL starved 0/165 chunks after 3 base retransmit passes behind a fully-SERVED COMPLETE entry at 2/5 queue occupancy (balloon5.log:883–:1056 ↔ base5.log:403–:522 NACK_INVALID seq 16/17/18/20/21/23/24/25).
- 14 consecutive rounds of gap closure have all lived in the volatile queue/eviction machinery (D-19/D-24 completion-aware eviction, supersede path, queue depth, slot pressure). The file-backed design retires the defect class instead of patching it again.
- The round-#14 instrument suite ([STACK]/[TWDT]/[IDLE0]/[I2C]) is proven and CARRIES OVER to the new architecture — the queue-liveness crash class (D1) becomes moot on the file-backed path, and the instruments remain valid if any crash class recurs.

## Operator Decisions (locked 2026-08-29)

| Decision | Choice |
|---|---|
| SD slot wiring | Same as base station board family — built-in slot, SDMMC 1-bit, CLK=39 / CMD=38 / D0=40 (mirrors `base_station_config.h` constants) |
| Retention | **Keep everything** — the card is the flight archive; nothing auto-deletes; card-full stops new captures with an honest error (base already holds delivered copies) |
| SD-write failure at capture | **Fall back to the volatile PSRAM queue** — legacy path survives ONLY as an honestly-labeled degradation fallback; file-backed is the sole normal path |
| Volatile queue fate | **File-backed only** in normal operation — capture→file, chunk reads from file, RAM holds only a small index; the queue-liveness defect class dies with the normal path |

Reconciliation of the last two: normal operation never touches the volatile queue; on SD-write failure the capture uses the legacy volatile entry and the record/console labels the degradation honestly.

## Codebase Facts (verified 2026-08-29)

- Base station SD stack to mirror: `src/sd_storage.cpp` — `SD_MMC.setPins(CLK, CMD, D0)` + `SD_MMC.begin("/sdcard", true, false, SDMMC_FREQ_DEFAULT)` (1-bit, mount=true, no format, 20 MHz), FAT32 via FS/VFS. Constants: SD_CLK_PIN 39, SD_CMD_PIN 38, SD_DATA_PIN 40 (`include/base_station_config.h:42-44`).
- `sd_storage.cpp` is currently **base-only** via platformio.ini `<sd_storage.cpp>` exclusion — the balloon env needs its own module or a shared one; GPIO 39 is LED-owned on the base (LED moved to 41) — verify the balloon board's pin ownership at bench (01-09-style operator checkpoint).
- Base ImageRx/gallery/sd_storage side is UNCHANGED by this phase (LoRa wire protocol frozen; `SdStorage: gallery index built` + `/images/IMG_*.JPG` flow already works).
- Balloon capture path today: camera → PSRAM fb → thumb+full extracted into ImageTxEntry (fixed-depth RAM queue, `include/image_tx_manager.h`) → window/chunk service reads from the entry. That entry pipeline is what becomes file-backed.
- Bench session #11 artifacts to reuse: round-#14 instruments in `src/main_balloon.cpp` ([STACK] watermark, [TWDT] handle, [IDLE0] registered printf, [I2C] disposition) and the `G01_D1_IDLE_HOOK_DISABLED` A/B guard.

## Out of Scope

- Base station firmware changes (wire protocol frozen)
- Event-based triggers (CTRL-05, Phase 2 scope)
- Any D1 root-cause chase on the volatile path beyond what the fallback honestly inherits (the fallback keeps today's known failure modes, documented, not fixed here)
- Deleting/clearing card contents automatically (keep-everything archive; manual clear only)

## Claude's Discretion

- File naming/index format on the balloon card (may mirror the base's IMG_*.JPG + sidecar convention or simpler — implementer's call, must support boot-time rescan)
- Whether the balloon SD module is shared with `sd_storage.cpp` or a new `sd_store_balloon` module (platformio env wiring is implementer's call)
- Chunk-read granularity/caching (200 B reads vs buffered reads) provided PRI-03 ordering holds
