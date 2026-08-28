# G01-7 Lever-Selection Record (round #14, plan 01-33 Task 1)

**Plan:** 01-command-protocol-control / 01-33 (gap_closure)
**Record date:** 2026-08-28
**Purpose:** The lever-selection audit for the G-01-7 burst full-delivery residual NAMED by the session-10 discriminator census — the supersede-path source reading, the depth-raise arithmetic, and the ranked menu with the selected option and its provenance. Every claim carries a file:line or log-line quote so the checker can re-derive it.

---

## 1. EVIDENCE BASE — the session-10 census, quoted

All lines verifier-reproduced (01-UAT.md G-01-7 SESSION-10 UPDATE; bench session #10, 2026-08-28, balloon4.log/base4.log, round-#13 firmware, balloon ELF SHA256 acce78241). Series A finally ran: 6/8 verdicts COMPLETE — image 40 2/2, image 42 2/2, image 41 0/2.

The burst sequence that killed image 41, step by step:

1. **Base deadline activation (the 01-22 lever firing live):**
   `base4.log:428 — "ImageRx: full-pull activation deadline reached - activating image 41 despite thumbnail heal pending for image 41"`
   (Citation note: the plan frontmatter's must_haves truth 1 says "balloon4.log:428"; the deadline-activation line is a base-side `ImageRx:` line and lives at **base4.log:428** — balloon4.log:428 is an `E32: AUX-low missed` line. The objective and the UAT census both cite base4.log; this record uses the verified citation.)

2. **The supersede evictions (the killing step):**
   `balloon4.log:816 — "ImageTx: window request for image 42 supersedes older entry image 40; evicted"`
   `balloon4.log:817 — "ImageTx: window request for image 42 supersedes older entry image 41; evicted"`
   Image 41 was receipt-evidenced at this instant: its re-announce budget had been re-armed by a genuine matching window request (`balloon4.log:737 — "ImageTx: re-announce budget re-armed - window request received for image 41"`), i.e. `lastWindowRequestMs != 0` — class 5 by the evictionClassOf definition (src/image_tx_manager.cpp:392).

3. **Seven honest rejections burned the base's pass budget:**
   `balloon4.log:1003, :1022, :1044, :1068, :1136, :1142, :1162 — "ImageTx: window request for unknown/evicted image 41 rejected"` (7 occurrences, grep-verified)

4. **The verdict the rejections produced (the base's D-24 machinery exhausting):**
   `base4.log:484 — "ImageRx: image 41 kind 1 finalized INCOMPLETE (retransmit passes exhausted): 0/31 chunks after 3 passes"`
   `base4.log:485 — "SdStorage: finalized /images/IMG_00041.JPG (0/31 chunks, 0 B persisted, complete=false)"`
   While the newest capture's full (42) completed 2/2 (`base4.log:269-:270`, `:425-:426`).

5. **Series-A command-terminal context (third session with this terminal):**
   `base4.log:250 — "CommandSender: Command seq=9 timeout after 3 retries"` — the forbidden CAPTURE_NOW-adjacent terminal class the lever must not reproduce (see menu option (b)).

**The ledger-named residual, verbatim** (01-UAT.md G-01-7 SESSION-10 UPDATE):

> "The levers work as designed; the DESIGN's queue depth vs the burst pattern is the mismatch (the newest capture always wins the slot; the previous capture's base-activated full is sacrificed, and its thumb died 6/8 behind the same churn). Lever class for the next round: admission control (refuse/queue a new capture while an earlier full-pull is base-activated) or oldest-queued-full eviction instead of previous-capture-active-entry eviction."

The round-#10 levers are BENCH-VERIFIED ENGAGED (budget re-arms x2 at :737/:815, re-announces x3 at :690/:799/:803, genuine holds x4, the base deadline release at base4.log:428) — the defect is no longer lever absence but the design's queue policy under the burst.

---

## 2. SOURCE READING — what the supersede path actually does

**The supersede path is `ImageTxManager::evictEntriesOlderThan(const ImageTxEntry& reference)`, src/image_tx_manager.cpp:1338-1361** (the plan's ":1332-1343" drifted to :1338-1361 at HEAD — same function, same code).

**Caller (exactly one):** `handleWindowRequest`, src/image_tx_manager.cpp:1172 — `evictEntriesOlderThan(*target)` — inside the `if (!thumbWindow)` branch (:1162), i.e. FULL window requests ONLY (a THUMBNAIL heal request never evicts, :1165-1167), reached only AFTER the target entry matched (:1102-1126) and range-validated (:1145-1160), and BEFORE the one-active-window BUSY check (:1178-1187).

**Victim selection, verbatim mechanics (:1348-1359):** the path scans every used entry with `enqueueSeq < reference.enqueueSeq` (strictly OLDER than the incoming request's target) and, per entry, applies exactly ONE guard — the 01-12 mid-service deferral (:1350-1354): if the entry's window is armed and incomplete (`windowArmed && windowNextIndex < windowStart + windowCount`), the supersede DEFERS with `"ImageTx: supersede of image %u deferred - window mid-service"` (:1352). Otherwise the entry is evicted unconditionally with `"ImageTx: window request for image %u supersedes older entry image %u; evicted"` (:1356-1357) — it does not stop at one victim; it flushes EVERY older non-mid-service entry in slot order.

**THE AUDIT'S CENTRAL ANSWER: evictionClassOf's class-5 receipt-evidenced protection is NOT consulted in the supersede path at all.** `evictionClassOf` (src/image_tx_manager.cpp:385-399) is called from exactly one site — the enqueue-overflow two-pass scan, src/image_tx_manager.cpp:560-578 (the class read at :571). `sweepExpiredEntries` (:1363-1374) is TTL-only, no class ranking. The supersede path's only protection is the mid-service armed-window guard. **The protection is ABSENT, not legitimately exhausted** — this is what the census proves: images 40/41 were receipt-evidenced (windowEverArmed / lastWindowRequestMs != 0 — image 41's re-announce budget re-arm at :737 is the receipt stamp) and neither was mid-service at the :816-817 instant, so the path flushed both. Had the enqueue-overflow scan met the same entries, it would have ranked them class 5 (LAST resort, :381-384, :392) and evicted them only when nothing lower existed. Two different eviction paths, two different protection standards — the supersede path is the unprotected one, and session 10's killing evictions rode exactly that path.

**D-24 consequence (base side, src/image_rx_manager.cpp):** a rejected window request is not free — the base's pass accounting charges a pass per unanswered request round (:177, :271; finalize at the IMG_RETRANSMIT_MAX_PASSES 3 bound, :167, :262), and `acceptChunk` is the sole passCount zero-writer (:707-711). The seven honest rejections (:1003-:1162) are what burned image 41 to 0/31 after 3 passes (base4.log:484). **Therefore the lever must PREVENT the eviction, not soften the rejection** — once the entry is gone, every subsequent honest rejection spends the base's bounded budget on a target that no longer exists.

**How many queue slots the burst consumes:** slots are PER CAPTURE, not per kind — both kinds of one capture share a single ImageTxEntry (`fullBuffer` + `thumbBuffer`, include/image_tx_manager.h:85-168). The series-A pattern is 3 captures = exactly 3 entries = 100% of IMG_TX_QUEUE_DEPTH 3 (include/image_protocol.h:61). **"Completed-and-freed thumbs" do not free slots:** buffers are KEPT after push completion by design — THUMB_PUSHED parks with thumbBuffer alive for heals, SERVED keeps BOTH buffers for tail re-requests (include/image_tx_manager.h:50-55, image_tx_manager.cpp:1315-1329) — so a slot frees only via eviction (overflow/supersede/TTL), never via kind completion. During the session-10 burst the queue was exactly full at all times; every new arrival (enqueue or supersede) had zero headroom and had to evict. At depth 5 the same burst leaves 2 spare slots and the enqueue-overflow scan engages only from a 4th concurrent capture.

---

## 3. ARITHMETIC — the depth raise 3 -> 5, bounded

**Constants (real, from source):**
- `IMG_MAX_IMAGE_SIZE = 50000` B — include/image_protocol.h:138 (the enqueue gate caps every full buffer, image_tx_manager.cpp:450-456)
- `THUMB_MAX_BYTES = 8192` B — src/camera_manager.cpp:12 (the CR-03 thumbnail payload bound, :393-399)
- Per-entry PSRAM worst case = 50,000 + 8,192 = **58,192 B** (one entry holds at most one full buffer AND one thumbnail buffer simultaneously; both are ps_malloc'd copies, image_tx_manager.cpp:462, :488)

**PSRAM residency at the raised depth:**
- Depth 5 worst case: 5 x 58,192 = **290,960 B (~284 KB)**
- Depth 3 (today): 3 x 58,192 = 174,576 B
- Increment: **+116,384 B worst case** (+2 slots)
- Observed free PSRAM at the bench (session 10, [MEM], balloon4.log:138): **psram=8,347,144 B (~8.35 MB, the ~8.1 MB class the plan cites)** — the worst case is **3.5%** of observed free PSRAM; the increment alone is 1.4%. Margin ~28x even at the worst case.
- Allocation-failure behavior is unchanged and honest: ps_malloc failure at enqueue drops the kind with a named log and keeps the other (WR-01, image_tx_manager.cpp:463-474, :489-507) — a deeper queue cannot turn exhaustion silent.

**Internal RAM cost:**
- `sizeof(ImageTxEntry)` = **88 B** on this 32-bit target (Xtensa LX7, 4-byte-aligned field walk over the struct at include/image_tx_manager.h:85-168 — 2 pointers, 13 scalar words, ~20 sub-word fields)
- `entries[]` grows 3 x 88 = 264 B -> 5 x 88 = 440 B: **+176 B static internal RAM** in the ImageTx singleton. Negligible against the internal-heap floor the [MEM] instrument tracks (minHeap=8,502,560 B, balloon4.log:138 — includes PSRAM; internal-only floor remains MB-class).

**Buffer-lifetime effect on sweepExpiredEntries:** none structural. The TTL is per-entry `lastActivityMs > IMG_ENTRY_TTL_MS` (900,000 ms = 15 min, image_protocol.h:174; sweep at image_tx_manager.cpp:1363-1374) — the depth raise does not extend any single entry's lifetime, it raises the count of SIMULTANEOUSLY resident entries. `freeEntry` (:1424-1455) frees both buffers on eviction/TTL exactly as before; `sweepExpiredEntries`' TTL and `evictionClassOf`'s ranking keep their full rights (the 01-12/01-14 carried prohibitions are untouched by this lever).

**Justified ceiling: 5.** The burst pattern is 3 concurrent captures (session 10's series-A shape; session 10 additionally proved CIF-class 119-chunk captures ride the same burst — image 43 — and the lever must hold for QVGA series A without regressing spaced captures, sessions 5/8's 4/4-kinds pattern). Depth 5 = 3 burst entries + 2 headroom; 290,960 B worst case is 3.5% of observed free PSRAM and the internal-RAM increment is 176 B. Deeper values would also fit arithmetically (8 x 58,192 = 465,536 B = 5.6%), but nothing in the evidence names a pattern needing more than 3-concurrent-plus-headroom, so the record justifies exactly **IMG_TX_QUEUE_DEPTH = 5** as the ceiling this selection ships.

---

## 4. RANKED MENU + SELECTION

### Option (a) — depth raise + supersede-path receipt-evidenced protection — **SELECTED (evidence-ranked DEFAULT)**

`IMG_TX_QUEUE_DEPTH` 3 -> 5 (include/image_protocol.h:61, arithmetic comment citing this record's section 3) **AND** the supersede path's victim selection ranked through `evictionClassOf`: a supersede must never evict a receipt-evidenced entry (class 5: `windowEverArmed || lastWindowRequestMs != 0`, src/image_tx_manager.cpp:392) while any lower-class candidate exists — when it avoids one, the engagement discriminator line `ImageTx: supersede of receipt-evidenced image %u avoided - evicting class %u entry image %u instead (G-01-7)` proves it; when ONLY receipt-evidenced entries exist as supersede candidates, the path does NOT evict — the incoming request is honestly rejected through the existing unknown/evicted rejection class extended with its own named line `ImageTx: window request for image %u rejected - queue holds only receipt-evidenced entries (G-01-7)`, and the base's existing D-24 pass machinery retries within its existing bound — **no new protocol surface**.

**Why it is the default — every element maps onto a quoted line:**
- The depth raise acts directly on the ledger-named mismatch ("the design's queue depth vs the burst pattern", section 1) — 3 slots cannot hold a 3-capture burst, and the overflow label prints the depth verbatim (image_tx_manager.cpp:591) so the constant is self-evidencing in future logs.
- The supersede protection acts directly on the killing step (balloon4.log:817: image 42's request evicted receipt-evidenced image 41) — section 2 establishes the class-5 protection is ABSENT from exactly this path, so ranking the victim selection through evictionClassOf closes the two-standards gap between the overflow scan and the supersede path. Replayed against the census, option (a) spares image 41 (class 5) by evicting image 40 (SERVED, class 2) instead — the seven rejections never fire, 41's full pull continues on its already-activated schedule.
- Changes no operator-visible command semantics: CAPTURE_NOW behavior, NACK vocabulary, ACK budgets, and the wire format are untouched; the rejection line rides the EXISTING unknown/evicted NACK_INVALID class.
- Composes with every round-#10 lever: the receipt-informed re-announce ladder (01-17/01-21), the budget re-arm, the 15 s busy hold, the base full-arm deadline release (01-22, base4.log:428), the 01-18 quiet gate, the class-ranked overflow scan's two-pass structure (01-14), and the D2 receipt-ever flag — the lever TIGHTENS receipt-evidenced protection and never demotes it.

### Option (b) — admission control (NACK_BUSY a CAPTURE_NOW while a receipt-evidenced unserved full exists) — **REJECTED as default**

Operator-visible by construction: the operator's capture trigger would return BUSY. Worse, it risks reproducing the forbidden terminal the campaign has hit in three sessions (5, 7, 10 — base4.log:250 carries session 10's): CAPTURE_NOW's ack budget is 2000 ms x 3 retries (D-05, `ackTimeoutFor()`), and under an active burst the chunk service is exactly the traffic class that starves command transmits (session 10's seq=9 burned retries 1/3 and 2/3 BEFORE the 01-18 hold engaged, then died on the third). Refusing captures under a busy balloon lengthens the busy window the command must outlive. The plan's prohibition stands: no operator-visible command-semantics change unless the Task 2 checkpoint explicitly accepts this risk by name.

### Option (c) — depth raise alone (no supersede protection) — **INSUFFICIENT**

Session 10's killing evictions rode the SUPERSEDE path (balloon4.log:816-817), not the enqueue-overflow scan. A depth raise makes supersede flushes rarer (2 spare slots delay the newest capture's pressure) but does not guard the victim selection: the moment the queue is again full of live entries, the unprotected supersede path evicts receipt-evidenced entries exactly as it did. The census's mechanism (section 2's two-standards finding) survives option (c) intact.

---

## LEVER SELECTED (round #14 Task 2 checkpoint record)

**Selected option: (a) — depth raise (IMG_TX_QUEUE_DEPTH 3 -> 5) + supersede-path receipt-evidenced protection, with the two engagement discriminator lines.**

**Evidence that selected it:** the session-10 census (section 1) names the mechanism with quoted lines at every step — base4.log:428 (deadline activation of image 41's full-pull), balloon4.log:817 (the supersede eviction of the receipt-evidenced entry), balloon4.log:1003-:1162 (the seven honest rejections), base4.log:484-:485 (0/31 INCOMPLETE) — and the source reading (section 2) establishes the protection gap is structural: evictionClassOf is consulted ONLY in the enqueue-overflow scan (image_tx_manager.cpp:571), never in the supersede path (:1338-1361). Option (a) is the only menu entry that acts on both named halves (depth AND victim selection) without touching operator-visible semantics. Options (b) is rejected as default for the forbidden-terminal interaction (2000 ms x 3 ack budget under active chunk service, base4.log:250); option (c) leaves the killing path unguarded.

**Provenance (house convention):** `workflow.auto_advance = true` (.planning/config.json) — the evidence-ranked option is auto-selected at this checkpoint per the campaign convention (01-23/01-25/01-26/01-30/01-32 predecessors) and recorded as **pending end-of-phase operator confirmation — the sixth pending confirmation joins this round's Task 2 gate**. No bench claims are made by this record; G-01-7 flips only on 01-34's operator-observed series-A verdicts.

**SHIPPED DEPTH: 5**

*(Task 3 records the implementation against this section: the shipped constant must equal the ceiling this record justifies, and the two discriminator lines must appear verbatim in src/image_tx_manager.cpp.)*

**Checkpoint resolution (recorded at Task 3 open):** the Task 2 checkpoint was returned and RESOLVED in auto mode by the execute-phase orchestrator (`workflow.auto_advance = true`, .planning/config.json) — option (a) approved as the evidence-ranked default, verbatim: "approved — option (a): depth raise IMG_TX_QUEUE_DEPTH 3→5 + supersede-path receipt-evidenced protection (victim selection ranked through evictionClassOf; class-5 entry never the victim while a lower-class candidate exists; honest rejection with named line when only receipt-evidenced entries exist)". The selection stands as the SIXTH pending end-of-phase operator confirmation.

---

## IMPLEMENTED (round #14 Task 3, plan 01-33)

- **SHIPPED DEPTH: 5** — `include/image_protocol.h` `IMG_TX_QUEUE_DEPTH = 5` with the section-3 arithmetic comment beside it (cites this record; the overflow label prints the constant verbatim, so the depth stays self-evidencing in future logs).
- **Supersede path** — `ImageTxManager::evictEntriesOlderThan` (src/image_tx_manager.cpp) rewritten per option (a): victim selection ranked through `evictionClassOf` — REUSED VERBATIM, the function body is untouched (two call sites now: the enqueue-overflow scan and this path; no forked ranking, T-01-33-02 held). The 01-12 mid-service deferral keeps its guard verbatim (predicate + line, unconditional — the overflow scan's pass-2 drop-the-skip does not apply to supersede). Lower-class candidates evict in ascending class order (oldest-first tie-break, the overflow scan's), preserving the original "supersedes older entry ... evicted" line. A class-5 remainder after any lower-class eviction spares each receipt-evidenced entry with the engagement line and the request proceeds; class-5-only candidates with nothing yet evicted return false and the caller (`handleWindowRequest`) rejects through the EXISTING `UNKNOWN_IMAGE`/NACK_INVALID class with the second named line — no new protocol surface. The receipt stamp + budget re-arm above the supersede stay (a rejected request still genuinely matched its target).
- **Discriminator lines (verbatim in source, both unconditional event lines):**
  - `ImageTx: supersede of receipt-evidenced image %u avoided - evicting class %u entry image %u instead (G-01-7)` — engagement proof
  - `ImageTx: window request for image %u rejected - queue holds only receipt-evidenced entries (G-01-7)` — honest-rejection proof
- **Census replay preserved:** replayed against session 10, the new path evicts image 40 (SERVED, class 2) under the original supersede-evicted line, then spares receipt-evidenced image 41 with the avoided line naming "class 2 entry image 40" — the seven rejections never fire and 41's full pull continues on its already-activated schedule (section 4's option-(a) replay, realized).
- **Verification (2026-08-28):** `pio run -e esp32-s3-balloon -e esp32-s3-basestation` = 2/2 SUCCESS; `node scripts/verify_protocol_roundtrip.mjs` = exit 0 (54/54 PASS); PRI-01 node order-gates green (beacon early-return precedes pushPending() in process(); IMG_WINDOW_SERVICE_PREEMPT_MS check precedes findActiveEntry() in pushPending()); protected surfaces grep-clean (CMD_TX_CHANNEL_QUIET_MS = 750; uart_ll_is_tx_idle x3; the 01-32 [STACK]/[IDLE0]/[TWDT]/A-B instruments). NO bench claims — G-01-7 flips only on 01-34's operator-observed series-A verdicts.
