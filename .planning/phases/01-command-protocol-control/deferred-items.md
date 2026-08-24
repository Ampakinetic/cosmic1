# Deferred Items — Phase 01

Out-of-scope discoveries logged during plan execution. Not fixed in-round (scope boundary: only issues directly caused by the current task's changes are auto-fixed); recorded here and routed.

## 2026-08-24 — 01-12 Task 3 (bench session #4 log analysis)

### CommandSender retry-bound overrun (WINDOWS entry 6)

- **Found during:** 01-12 bench session #4 log analysis (base4.log / balloon4.log)
- **Issue:** `CommandSender` fires one retry beyond the documented D-05/D-07 3-attempt bound — 13 occurrences of `Retrying command seq=N (attempt 4/3)` in base4.log (e.g. :115, :290, :372, :2330) before the terminal guard stops the sequence. Benign in effect (the terminal guard does fire; the session's only FAILED commands were the 2 image-7 heal BUSY deferrals, unrelated), but the off-by-one means one extra command transmission per exhausted sequence, and the late retries re-arm finalized kind-0 windows post-terminal (16x `chunk for finalized image 11 kind 0 ignored`, base4.log:3286-3310 — correctly ignored, but the re-arm traffic should not exist).
- **Scope ruling:** pre-existing (01-12 touched image_tx/rx_manager and camera_manager only; `git diff` for Tasks 1-2 contains no src/command_sender.cpp change). Not auto-fixed.
- **Route:** WINDOWS.md entry 6 (open). Candidate fix class: an off-by-one in the attempt counter check (`attempt <= maxRetries` vs `<`), or an ACK race that re-arms after the count is read. Verify against src/command_sender.cpp retry loop; fold into the G-01-9 round's CommandSender-adjacent work or a standalone micro-fix.
