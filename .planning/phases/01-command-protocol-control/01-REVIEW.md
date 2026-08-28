---
phase: 01-command-protocol-control
reviewed: 2026-08-28T14:05:24Z
depth: standard
files_reviewed: 38
files_reviewed_list:
  - include/auto_capture.h
  - include/balloon_config.h
  - include/base_station_config.h
  - include/camera_pins.h
  - include/command_handler.h
  - include/command_protocol.h
  - include/command_sender.h
  - include/common_types.h
  - include/e32_lora.h
  - include/image_protocol.h
  - include/image_rx_manager.h
  - include/image_tx_manager.h
  - include/sd_storage.h
  - platformio.ini
  - scripts/embed_web_assets.mjs
  - scripts/verify_protocol_roundtrip.mjs
  - src/alert_engine.cpp
  - src/alert_engine.h
  - src/auto_capture.cpp
  - src/camera_manager.cpp
  - src/camera_manager.h
  - src/command_handler.cpp
  - src/command_protocol.cpp
  - src/command_sender.cpp
  - src/e32_lora.cpp
  - src/image_rx_manager.cpp
  - src/image_tx_manager.cpp
  - src/main_balloon.cpp
  - src/main_basestation.cpp
  - src/sd_storage.cpp
  - src/sensor_manager.cpp
  - src/status_display.cpp
  - src/test_lora_balloon.cpp
  - src/trajectory_buffer.cpp
  - src/trajectory_buffer.h
  - src/web_assets.h
  - src/wifi_manager.cpp
  - src/wifi_manager.h
findings:
  critical: 0
  warning: 3
  info: 7
  total: 10
status: issues_found
---

# Phase 01: Code Review Report

**Reviewed:** 2026-08-28T14:05:24Z
**Depth:** standard
**Files Reviewed:** 38
**Status:** issues_found

## Summary

Adversarial standard-depth review of the Phase 01 command-protocol surface plus the balloon/base managers it now spans: wire protocol (command_protocol, image_protocol), both firmwares (main_balloon, main_basestation), the TX/RX image pipeline (image_tx_manager, image_rx_manager), command send/handle, E32 driver, camera/SD/sensor/trajectory/alert/wifi/status modules, and both Node tooling scripts.

The core protocol machinery holds up under adversarial reading: CRC16 and all framing arithmetic match the JS harness; the WR-03 duplicate-response guard prevents the pending-count underflow; `handleWindowRequest`'s tail clamp, `acceptChunk`'s bounds, `serviceWindowChunk`'s span math, and `startTransfer`'s manifest validation all hold by construction; the SD sidecar parser clamps every untrusted field into range before use; `verifyStoredCrc32` is correctly preceded by the CR-02 flush; and the D-24/D-21 stall/settle/deadline machinery is internally consistent.

Findings: no critical issues. Three warnings — a real logic defect in the [STACK] instrument's zero sentinel, a validation-order defect in `handleWindowRequest`'s receipt stamp (an *additional* defect in the known supersede path, not the already-routed capacity-rejection issue), and a fail-open gap in the web-asset supply-chain lock. Seven info findings, mostly dead code with embedded hazards and stale wire-size comments.

**Out of scope here:** the known open defect where `evictEntriesOlderThan`'s honest-rejection fires regardless of capacity need (rejections at 2/5 occupancy) is already routed to debug round #15 and is not re-litigated. Known-benign conventions ([MEM]/[IDLE0] print conventions, E32 AUX-low tolerance, mojibake serial output, CMD_TX_CHANNEL_QUIET_MS / uart_ll_is_tx_idle) were respected.

## Narrative Findings (AI reviewer)

## Warnings

### WR-01: [STACK] instrument's zero sentinel hides the exact overflow signature it exists to catch

**File:** `src/main_balloon.cpp:155-165` (hook), `src/main_balloon.cpp:427-432` (loop print)
**Issue:** `s_idle0StackMinWords` uses `0` as both "no sample yet" sentinel and a legitimate measurement. `uxTaskGetStackHighWaterMark` returns 0 precisely when the IDLE0 stack has overflowed into its guard — the §10.5 signature the round-#14 instrument was designed to surface ("new lows accelerating toward 0"). The hook's latch `if ((s_idle0StackMinWords == 0) || (idle0Watermark < s_idle0StackMinWords))` does record a genuine 0, but the loop-side print guard `s_idle0StackMinWords != 0 && ...` (line 427) can never print it: once the minimum reaches 0 the condition is permanently false, so the single most diagnostic sample the instrument can produce is the one sample it can never report. This is a logic defect in the instrument itself, beyond the (benign, respected) new-low print convention.
**Fix:** Separate the has-sample latch from the value:

```cpp
static volatile bool s_idle0HasSample = false;
static volatile UBaseType_t s_idle0StackMinWords = 0;
// hook:
if (!s_idle0HasSample || (idle0Watermark < s_idle0StackMinWords)) {
    s_idle0StackMinWords = idle0Watermark;
    s_idle0HasSample = true;
}
// loop print:
if (s_idle0HasSample &&
    (idle0StackLastPrintedWords == 0 ||
     s_idle0StackMinWords < idle0StackLastPrintedWords)) { ... }
```

(Or print on `<=` transitions including 0.) Remember to reset both in the same places the old sentinel was reset.

### WR-02: Per-entry receipt stamp fires before range validation — malformed window requests promote entries to the protected eviction class

**File:** `src/image_tx_manager.cpp:1137` (stamp), `:1149` (range validation), `:1128-1136` (contract comment)
**Issue:** In `handleWindowRequest`, `target->lastWindowRequestMs = millis()` executes before the per-kind range validation at line 1149. A CRC-valid request that matches `imageId` + kind but fails range checks (`count == 0`, `count > IMG_WINDOW_MAX_CHUNKS`, `startChunk >= totalChunks`) still stamps the entry nonzero — which `evictionClassOf` reads as receipt evidence (`lastWindowRequestMs != 0` → protected class 5) and which re-arms the bounded re-announce budget (lines 1138-1143). A hostile or buggy base can therefore keep an otherwise-evictable stale entry pinned against supersede indefinitely by repeatedly sending it invalid-range requests, and can indefinitely re-arm its re-announce budget. This contradicts the site's own trust-boundary comment ("reached only after the kind enum check and entry match above — a crafted frame cannot stamp an entry it did not fully address, T-01-21-01"): T-01-21-01's full-validation clause names the per-kind range bounds too, and they have not run yet at the stamp site. The channel-liveness stamp (`lastInboundWindowRequestMs`, pre-kind-validation) is intentionally early and is *not* the problem — the per-entry stamp is. Note this is an additional defect in the supersede path; the known capacity-rejection issue routed to round #15 is unchanged.
**Fix:** Move the per-entry stamp block (lines 1128-1143) to after the range validation and tail clamp (after line 1160), so only a fully validated, armed request counts as receipt evidence. The comment above it then matches the code.

### WR-03: Supply-chain hash lock fails open when a vendored file has no recorded provenance

**File:** `scripts/embed_web_assets.mjs:105`
**Issue:** The generator verifies embedded assets with `if (provenance.files[name] && provenance.files[name].sha256 !== h)`. When `provenance.json` has no entry for a file present in `vendor/` (renamed file, new vendored asset, deleted/corrupt provenance for that entry), the mismatch check is skipped and the bytes are embedded unverified — silently defeating the "supply-chain lock (T-03-SC)" the generated header and file header document. A lock that fails open on exactly the "unknown content" case protects only against tampering with files it already knows about.
**Fix:** Fail closed for unknown entries:

```js
const recorded = provenance.files[name];
if (!recorded) {
    console.error(`sha256 provenance missing for vendor/${name} — refusing to embed`);
    process.exit(1);
}
if (recorded.sha256 !== h) { /* existing mismatch path */ }
```

(If a genuinely new vendor asset is intended, the provenance update becomes an explicit, reviewable step.) Both current files are recorded, so today's output is unaffected — this is hardening the invariant.

## Info

### IN-01: Stale "17-byte" telemetry-beacon comments (body is 19 bytes)

**File:** `include/command_protocol.h:263`, `src/command_sender.cpp:364`, `scripts/verify_protocol_roundtrip.mjs:778`
**Issue:** Three comments still describe the 0x14 beacon body as 17 bytes; `IMG_TELEMETRY_BEACON_BODY_SIZE` has been 19 since the D-41 `batteryMilliV` extension. Behavior is correct everywhere (the constant is used, not the comment); the legacy-17 references in `verify_protocol_roundtrip.mjs:973/994/996` are legitimate (they deliberately build a rejected legacy frame). Note: the `command_protocol.h:263` half is a repeat of an INFO from the phase-03 review that was never fixed.
**Fix:** Update the three comments to "19".

### IN-02: Dead `transmitToAddress` carries an unbounded VLA

**File:** `src/e32_lora.cpp:304-322` (VLA at `:308`), declared `include/e32_lora.h:138`
**Issue:** `transmitToAddress` has no callers (grep-verified across cpp/h). Its `uint8_t buffer[length + 4]` is a runtime-sized stack allocation with no bound — if ever called with a large frame it overflows the calling task's stack with no diagnostic. Dead code that is also a trap.
**Fix:** Delete the function and its declaration; if it is ever revived, bound `length` or allocate from the heap.

### IN-03: `enterConfigMode` saves `previousMode` and never uses it; exit always restores MODE_NORMAL

**File:** `src/e32_lora.cpp:642-655` (`:644`), `:657-660`
**Issue:** `E32Mode previousMode = currentMode;` is dead, and `exitConfigMode` unconditionally restores `MODE_NORMAL` rather than the saved mode. Latent wrong-restore if the pair is ever called around a config sequence from a non-NORMAL mode; today it is harmless.
**Fix:** Either restore the saved mode in `exitConfigMode` (and pass/return it), or delete the dead local and note the NORMAL-only contract.

### IN-04: Dead `CommandSender::findOldestCommand`

**File:** `src/command_sender.cpp:534`, declared `include/command_sender.h:161`
**Issue:** No callers anywhere (grep-verified); supersession logic now uses the ranked `evictionClassOf` path instead.
**Fix:** Delete both the definition and the declaration.

### IN-05: Dead camera adaptive-control cluster, including operator-setting overrides-in-waiting

**File:** `src/camera_manager.cpp:655-722` (`enterLowPowerMode` :655, `exitLowPowerMode` :665, `updateForConditions` :679, `optimizeForBandwidth` :700, `optimizeForQuality` :712), `:863` (`getOptimalFrameSize`); dead members `src/camera_manager.h:74-75`
**Issue:** `updateForConditions`, `optimizeForBandwidth`, `optimizeForQuality`, and `getOptimalFrameSize` have no callers (grep-verified); `enter/exitLowPowerMode` are reachable only from the dead `updateForConditions`. `imageBuffer`/`imageBufferSize` are declared, zeroed, freed, and accounted in `getMemoryUsage` but never allocated. Two traps if ever wired up: `updateForConditions` unconditionally calls `setBrightness`/`setContrast`/`setQuality`/`setFrameSize` on every invocation, silently fighting operator SET_* commands; and `optimizeForQuality`'s `FRAMESIZE_VGA`/`optimizeForBandwidth`'s `FRAMESIZE_QVGA` ignore the G-01-8 `allocatedFrameSize` capacity discipline.
**Fix:** Delete the cluster and the unused members; if adaptive behavior is planned, design it to respect operator-override state and the G-01-8 bound first.

### IN-06: Hardcoded AP credentials, plus a dead duplicate credential set in a config header

**File:** `src/wifi_manager.cpp:16-17` (live: `Cosmic1-BaseStation` / `balloontrack`), `include/base_station_config.h:49-50` (dead: `BalloonBaseStation` / `balloon123`)
**Issue:** The base's fallback AP credentials are compile-time constants in source — a documented, deliberate D-40 design (lockout-proof fallback, never persisted, never UI-editable), so this is informational, not a violation. Two notes for the record: (1) the entire web server is unauthenticated, so anyone in radio range who joins the AP can drive the camera and read the balloon's live GPS position — the AP password is the only gate on location data; (2) `base_station_config.h:49-50` still defines an older, divergent AP SSID/password pair that nothing references (grep-verified) — a maintainer "rotating" the password there would change nothing and silently believe they had.
**Fix:** Delete the dead `WIFI_AP_SSID`/`WIFI_AP_PASSWORD` macros from `base_station_config.h`. For the live pair, at minimum document the exposure (unauthenticated control + location) beside the constants; an optional build-time credential override (e.g. `-DWIFI_AP_PASSWORD=...`) would let deployments differ without code edits.

### IN-07: Same-(id,kind) re-manifest drops the tracked window request without cancel

**File:** `src/image_rx_manager.cpp:555-565` (seq cleared at `:563`), contrast `:136-138`, `:812-814`, `:843-845`
**Issue:** Every other teardown path cancels a transfer's outstanding window request before discarding `windowRequestSeq` (the WR-04 discipline: process() advance, `finalizeTransfer`, `finalizeIncomplete`, both allocateSlot eviction paths). The same-(id,kind) re-manifest branch does `*t = ImageRxTransfer{}` — zeroing `windowRequestSeq` — without `CmdSender().cancelCommand(...)`. An orphaned tracked command can keep retrying a span against the restarted (bitmap-cleared) reassembly until its own retry/timeout budget expires: idempotent duplicate chunk traffic on the half-duplex link plus one command-table slot held for the leftover budget. Impact is churn, not corruption — the balloon re-arms windows idempotently and the bitmap drops duplicates — but it is an inconsistency in an otherwise uniform invariant.
**Fix:** Capture `t->windowRequestSeq` before the reset and `CmdSender().cancelCommand(seq)` when nonzero, matching the finalize paths.

---

_Reviewed: 2026-08-28T14:05:24Z_
_Reviewer: Claude (gsd-code-reviewer)_
_Depth: standard_
