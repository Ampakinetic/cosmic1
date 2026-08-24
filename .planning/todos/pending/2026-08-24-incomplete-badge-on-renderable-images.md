---
created: 2026-08-24T13:16:00Z
title: Incomplete badge on visually-complete images (operator-perceived false positive)
area: ui
severity: minor
files:
  - src/main_basestation.cpp:1682-1689
  - src/main_basestation.cpp:1751-1766
---

## Problem

Operator bench report (01-12 session #4, 2026-08-24): "the incomplete overlay is showing over pictures that are actually complete" — gallery tiles and the detail view show the D-48 Incomplete chip on images that render as perfectly good JPEGs, which reads as a UI bug.

Log evidence says the badge logic is CORRECT: every image flagged this session was flagged rightly. The flagged images are bytes-corrupt-but-renderable JPEGs — image 7 full received 36/36 chunks but failed stored-bytes CRC ("mismatch (got FCAFC250, manifest 9DE9EFA0)" base4.log:854 -> INCOMPLETE :855), image 11 full received 144/144 but failed CRC (base4.log:3267 -> INCOMPLETE :3268), and image 9's thumb was full-sized (11223 B, G-01-9 defect A) and stopped at 56/57 (base4.log:1297). JPEG recovery renders them fine, so the chip and the pixels disagree visually. The corruption itself is G-01-9 defect B (balloon-source-side, 01-UAT.md G-01-9 / WINDOWS entry 5) — the badge is the honest messenger, not the defect.

Routing: root cause -> G-01-9 round (fix the source-side corruption and the badge stops firing on clean transfers). The UI-side residual (worth doing once G-01-9 lands): the badge gives no hint WHY an image is flagged, which invites exactly this operator misread.

## Solution

Two parts. (1) Gated on G-01-9: after the source-side corruption fix, a clean bench session should produce zero Incomplete verdicts — verify that, and the perceived false positive disappears. (2) UI polish in the Phase 03 gallery backlog: enrich the Incomplete presentation with the reason the sidecar recorded (chunk count received/total vs stored-bytes CRC mismatch — the sidecar/finalize path already knows which), e.g. badge text "Incomplete" plus a detail-row caption "received 36/36, stored bytes failed verification". Constraint: D-48 stays — incomplete images remain listed, badged, never hidden or filtered; only the explanatory copy is added.
