---
phase: quick-260831-vat
plan: 01
type: execute
wave: 1
depends_on: []
files_modified:
  - include/sd_storage.h
  - src/sd_storage.cpp
  - include/image_rx_manager.h
  - src/image_rx_manager.cpp
  - src/main_basestation.cpp
autonomous: true
requirements: []   # quick task - not roadmap-scoped
user_setup: []     # no external services

estimate:
  tokens: 40000
  raw_tokens: 26000
  tasks: 2
  confidence: low  # quick mode - no calibration samples

must_haves:
  truths:
    - Clicking "Clear SD Card" in the base dashboard shows a confirmation naming the destructive scope (ALL files erased); declining sends no HTTP request.
    - Confirming POSTs /sd-clear, deletes every file in /images on the base card, and the UI displays the server's file-count verdict in the existing message div.
    - While any ImageRx transfer row is non-terminal, /sd-clear refuses with a busy verdict and deletes nothing.
    - After a wipe, /gallery pagination and the /api/state galleryCount refetch signal reflect the empty card without a reboot.
    - A deliberate wipe never latches SdStorage's boot-permanent writeFailed degradation; subsequent transfers persist normally after the wipe.
  artifacts:
    - include/sd_storage.h        # uint16_t clearAllImages() + clearing guard member
    - src/sd_storage.cpp          # clearAllImages implementation (guarded walk + index reset)
    - include/image_rx_manager.h  # bool anyTransferActive() const
    - src/image_rx_manager.cpp    # anyTransferActive implementation
    - src/main_basestation.cpp    # /sd-clear route + handleSdClear + UI card in handleRoot
  key_links:
    - form action="/sd-clear" in handleRoot's section#capture matches the server.on("/sd-clear", HTTP_POST) registration
    - handleSdClear busy gate calls ImageRx().anyTransferActive() before any deletion
    - handleSdClear calls SDStorage().clearAllImages() and returns its count via sendResponse JSON {status, message}
    - clearAllImages resets galleryCount/indexVersion so the D-36 galleryCount refetch signal fires on the next poll
---

<objective>
Add a destructive-action-safe "Clear SD Card" button to the BASE STATION web interface: an explicit JS confirmation stating ALL files will be deleted, a POST /sd-clear endpoint that refuses while an image transfer is in flight, a guarded SdStorage::clearAllImages() wipe of /images with per-file logging and a removed-count verdict, and an immediate gallery/index reset so the empty card shows without a reboot.

Purpose: reset the base station's stored-image state after a test or journey without pulling the card or reflashing.

Output: working button + endpoint on the base dashboard; both firmware targets build green.

SCOPE NOTE — BALLOON UNIT DELIBERATELY EXCLUDED (escalated, not silently dropped): planning investigation found the balloon firmware (src/main_balloon.cpp) initializes NO WiFi (WiFi.h is included but never configured) and starts NO web server (startCameraServer() in src/app_httpd.cpp is dead code, zero callers). The balloon's only working clear surface is the deliberate serial SDCLEAR CONFIRM command (02.5-03, locked serial-only deletion design). A balloon web button therefore requires new operator decisions (WiFi AP activation policy, AP credentials — wifi_config.h holds only placeholders) and is routed back to the orchestrator via the Source Audit return. The balloon is UNTOUCHED by this plan (also keeps the D1 crash-debug campaign's balloon build byte-identical).

IMPORTANT — pre-existing dirty files: the worktree has unrelated uncommitted changes (include/sensor_pins.h, src/image_tx_manager.cpp, src/main_balloon.cpp, src/power_manager.cpp) from the D1 debug campaign. Stage ONLY this plan's five files per commit, by explicit path. Never stage .pio/, *.log, or the dirty files.
</objective>

<execution_context>
@$HOME/.claude/gsd-core/workflows/execute-plan.md
@$HOME/.claude/gsd-core/templates/summary.md
</execution_context>

<context>
@.planning/STATE.md

Source files to read before editing (read ONCE, targeted regions):
- `include/sd_storage.h` (259 lines — full read: SdStorage class, private members fullFile/thumbFile/galleryCount/indexVersion/indexedVersion, degrade() policy docs)
- `src/sd_storage.cpp` — buildIndex walk idiom at lines 487-494 (SD_MMC.open("/images") + openNextFile), degrade() at line 883 (what the wipe must NOT trigger)
- `include/image_rx_manager.h` (full read — ImageRxTransfer.terminal flag, transfers[RX_TRANSFER_SLOTS=8] private array)
- `src/main_basestation.cpp` — sendResponse() at 3817 (JSON {status, message} idiom), route registration block 2195-2224, forward declarations 145-160, handleRoot() dynamic section 2267+, capture-section forms 2333-2572, delegated submit listener 1561-1617 (the `if (ev.defaultPrevented) return;` guard at 1562 is what makes the inline confirm work with zero JS changes)
- Existing precedent to mirror: `include/sd_store_balloon.h` lines 356-374 (BalloonSdStore::clearAllImages contract) and `src/main_balloon.cpp` lines 381-414 (serial SDCLEAR handler, per-file + summary logging style)

Project invariants that shape this task:
- Base env has NO ArduinoJson — responses are hand-built String JSON via sendResponse()
- SD transport is SD_MMC 1-bit (built-in slot); all paths are fixed "/images/..." built from walked entries — no user input ever reaches a path
- SdStorage disk-full policy ("no file is ever deleted to make space") is about SILENT/automatic deletion; an explicit operator wipe is a new, deliberate surface and does not violate it — document it as such in the header comment
- Single-threaded base loop: WebServer handlers run in loop context; no locking needed anywhere (mirror the serial SDCLEAR precedent: a full card takes a few hundred ms of deletes, synchronous is acceptable)
- No bench/hardware claims in the summary — code-level + build-green only (project evidence discipline)
</context>

<tasks>

<task type="auto">
  <name>Task 1: SdStorage::clearAllImages() + ImageRxManager::anyTransferActive() busy query</name>
  <files>include/sd_storage.h, src/sd_storage.cpp, include/image_rx_manager.h, src/image_rx_manager.cpp</files>
  <action>
In SdStorage (include/sd_storage.h + src/sd_storage.cpp):

1. Declare public `uint16_t clearAllImages();` and a private `bool clearing;` member (initialize false in the constructor). Header comment: the ONLY deletion surface on the base card, invoked solely by the /sd-clear HTTP handler (Task 2); the disk-full policy above it (never delete to make space) is untouched — this is an explicit operator wipe.

2. Implement clearAllImages() in this exact order:
   a. Not-mounted honesty: if `status.initFailed` is true, log one "SdStorage: clear skipped - card not mounted" line and return 0.
   b. Set `clearing = true`.
   c. Close both open write handles directly (fullFile.close(), thumbFile.close()) and reset fullFileId/thumbFileId to 0 and both persisted-byte counters to 0 — deleting a file under an open FAT handle is the hazard this step removes.
   d. Walk /images mirroring the buildIndex idiom (src/sd_storage.cpp:487-494): SD_MMC.open("/images"), then openNextFile() loop. For each entry: close the walked File first, build the full path as String("/images/") + the entry name, skip with a named log if the entry is a directory (flat layout — never recurse), otherwise SD_MMC.remove(path). Count successes; log each removal ("SdStorage: cleared IMG_...") and each failed remove; after the loop log one summary line with the count (mirror the balloon module's per-file + summary style).
   e. Reset the RAM gallery index in place: `galleryCount = 0; indexVersion++; indexedVersion = indexVersion;` — no stale entries, and the version bump plus alignment makes /gallery and the /api/state galleryCount refetch signal (D-36) reflect the empty card on the next poll without a directory re-walk.
   f. Clear `clearing = false` (run in ALL paths including early returns — simplest: set it false before each return after step b, or restructure with a single exit). Return the removed count.

3. Guard against wipe races WITHOUT latching the permanent degrade state: at the top of openTransfer(), writeChunk(), and writeSidecar(), add `if (clearing) return false;` BEFORE any failure handling — these returns must NOT call degrade() and must NOT log per-call errors (a deliberate wipe must not set status.writeFailed for the rest of the boot, and must not flood the console). One comment at each site naming the wipe-hold reason. Do not touch any other degrade() call site.

In ImageRxManager (include/image_rx_manager.h + src/image_rx_manager.cpp):

4. Add public `bool anyTransferActive() const` — returns true when any slot in the private transfers[] array has `used && !terminal` (QUEUED/RECEIVING/RETRYING rows may still write chunks or sidecars to the card; terminal rows write nothing more). Implement inline in the header or in the .cpp beside getTransferSnapshot; header comment: the /sd-clear busy gate.
  </action>
  <verify>
    <automated>cd "C:\Work\Prog\Cosmic1" && pio run -e esp32-s3-basestation 2>&1 | tail -3 && grep -c "clearAllImages" include/sd_storage.h src/sd_storage.cpp && grep -c "anyTransferActive" include/image_rx_manager.h src/image_rx_manager.cpp && grep -c "if (clearing)" src/sd_storage.cpp</automated>
  </verify>
  <done>SdStorage::clearAllImages declared+implemented with handle-close, guarded walk, index reset, and clearing guard on openTransfer/writeChunk/writeSidecar that bypasses degrade(); ImageRxManager::anyTransferActive declared+implemented on the used-and-not-terminal predicate; esp32-s3-basestation build exits 0.</done>
</task>

<task type="auto">
  <name>Task 2: POST /sd-clear route + handler + confirm-gated UI card in the base dashboard</name>
  <files>src/main_basestation.cpp</files>
  <action>
1. Forward-declare `void handleSdClear();` beside the other handler declarations (lines ~145-160 block).

2. Register the route beside the other control routes (~line 2205 area): `server.on("/sd-clear", HTTP_POST, handleSdClear);`

3. Implement handleSdClear near the other handlers (e.g. after handleAutoCaptureDisable, ~line 2967):
   - Busy gate FIRST: if `ImageRx().anyTransferActive()`, respond sendResponse(409, "Busy", "Image transfer in progress - wait for transfers to finish, then clear again") and return. Nothing is deleted.
   - Otherwise call `const uint16_t removed = SDStorage().clearAllImages();` then, if `SDStorage().getStatus().initFailed`, respond 500 "Error" with an honest not-mounted message; otherwise respond sendResponse(200, "OK", <message that includes the removed count, e.g. "SD card cleared - N file(s) removed">). Use the existing sendResponse JSON idiom (hand-built String; base env has no ArduinoJson). Log one Serial line for the operator console too.
   - The UI's delegated submit listener reads only the JSON `message` field (src/main_basestation.cpp:1601), which sendResponse always provides.

4. UI card in handleRoot()'s dynamic section#capture (insert immediately before the `</section>` that precedes the section id="queue" line ~2582, i.e. after the WiFi card): add one card following the exact markup idiom of the neighboring cards (div class="card", h2, form, button): h2 labeled with an SD/storage glyph + "SD Card", and a form with `action="/sd-clear"` `method="POST"` and an inline attribute `onsubmit="return confirm('This deletes ALL files on the base station SD card. Every stored image and sidecar file will be permanently erased. Continue?')"`. The submit button is the form's only control, labeled "Clear SD Card". Confirm wording MUST state all files are deleted (destructive-action requirement).
   - Why the inline confirm is sufficient with ZERO footer JS changes: the delegated submit listener (line 1561) starts with `if (ev.defaultPrevented) return;` (line 1562); an inline onsubmit attribute returning false sets defaultPrevented, so declining the confirm skips the fetch entirely. Accepting falls through to the existing generic machinery: empty body (the submit button is skipped by the serializer), POST to the form's action, verdict rendered into the lazy js-capture-msg div (lines 1578-1608). Do NOT modify HTML_FOOTER or the listener.
   - Do NOT add a visible message div in the markup — the listener creates/reuses it.

5. Build BOTH targets: `pio run -e esp32-s3-basestation` and `pio run -e esp32-s3-balloon` (balloon must stay green untouched — it is deliberately not in this plan's file set).

6. Commit in two atomic commits, staging by explicit path ONLY (the worktree has unrelated dirty files — see objective):
   - Commit 1 (after Task 1): message `feat(quick-260831): base SD clear - SdStorage::clearAllImages + ImageRx busy query` — stage exactly: include/sd_storage.h src/sd_storage.cpp include/image_rx_manager.h src/image_rx_manager.cpp
   - Commit 2 (after step 4-5): message `feat(quick-260831): base dashboard clear-SD button with confirm + /sd-clear route` — stage exactly: src/main_basestation.cpp
  </action>
  <verify>
    <automated>cd "C:\Work\Prog\Cosmic1" && pio run -e esp32-s3-basestation 2>&1 | tail -2 && pio run -e esp32-s3-balloon 2>&1 | tail -2 && grep -c "sd-clear" src/main_basestation.cpp && grep -c "anyTransferActive" src/main_basestation.cpp && git status --porcelain include/sensor_pins.h src/image_tx_manager.cpp src/main_balloon.cpp src/power_manager.cpp | grep -c "^ M" && git log --oneline -2</automated>
  </verify>
  <done>/sd-clear registered as HTTP_POST with handleSdClear that busy-gates on anyTransferActive() then wipes via SDStorage().clearAllImages() and returns the count; handleRoot's section#capture carries the confirm-gated Clear SD Card card whose action matches the route; both firmware targets build exit 0; the four pre-existing dirty debug files remain unstaged and modified; two commits contain only this plan's five files.</done>
</task>

</tasks>

<threat_model>
## Trust Boundaries

| Boundary | Description |
|----------|-------------|
| browser → base HTTP | Unauthenticated LAN web surface (existing project posture — every control route from /capture to /wifi shares it) |

## STRIDE Threat Register

| Threat ID | Category | Component | Severity | Disposition | Mitigation Plan |
|-----------|----------|-----------|----------|-------------|-----------------|
| T-Q01-01 | Tampering/Elevation | POST /sd-clear on the unauthenticated dashboard | medium | accept | Consistent with the existing posture: all control routes (capture, settings, WiFi switch) already mutate device state with no auth layer; the JS confirm is UX safety, not a security boundary. Documented here rather than silently assumed. |
| T-Q01-02 | Information disclosure | clearAllImages file walk | low | mitigate | No user input reaches any file path — the walk is the fixed /images directory (numeric-id path discipline preserved); only walked entry names are joined. |
| T-Q01-03 | Denial of service | Wipe concurrent with image writes | high | mitigate | Busy gate (anyTransferActive) refuses during non-terminal transfers; clearing guard makes any race-window writes fail fast WITHOUT latching the boot-permanent writeFailed state; open handles closed before removal. |
</threat_model>

<verification>
- `pio run -e esp32-s3-basestation` exit 0 and `pio run -e esp32-s3-balloon` exit 0 (untouched balloon regression).
- Wiring greps: "sd-clear" and "anyTransferActive" and "clearAllImages" present in src/main_basestation.cpp / headers / implementations as listed per task.
- Structural honesty: /sd-clear busy path sends the Busy verdict before any deletion call; initFailed path responds 500; success path reports the removed count.
- Git: exactly two commits, each containing only its listed files; the four pre-existing dirty files remain unstaged; no *.log, .pio/, or .planning/ files swept in.
- NO hardware claims: the button's on-device behavior is bench-verifiable later; the summary records code-level + build evidence only.
</verification>

<success_criteria>
1. The base dashboard's Capture section shows a "Clear SD Card" button gated by a confirm dialog whose text states ALL files on the card are erased; declining produces no network request.
2. Confirming wipes /images on the base card and surfaces the server's file-count verdict in the page (message div), and the gallery updates to empty on the next poll without reboot.
3. Transfers in progress block the wipe with a visible busy message; a wipe never degrades SdStorage into its permanent write-failed state.
4. Both firmware targets build green; the balloon build is byte-identical in behavior (no balloon file modified).
</success_criteria>

<output>
Create `.planning/quick/260831-vat-add-a-clear-sd-card-function-as-a-button/260831-vat-SUMMARY.md` when done
</output>
