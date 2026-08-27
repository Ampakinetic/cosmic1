# API Coverage — Phase 01

> Full coverage by default. Opt-outs are explicit, reasoned decisions.

No external API integration: first-party ESP32-S3 firmware only — E32 LoRa over UART, esp32-camera, embedded web server; no external service, SDK, or REST/gRPC/webhook surface.

The detector's "api" signal traces to plan 01-03 prose about the first-party web server's own HTTP endpoints ("Direct API posts with empty values rely on the 400 path"), not to any external service.

Re-checked at the 01-12 gap-closure round (2026-08-24): the round's scope — image-transfer scheduling (serialization hold, eviction guard, RX-settle gap) and camera framesize re-init, all first-party firmware — adds no external API surface. The detector's signal remains the 01-03 first-party-endpoint prose false positive recorded above.

Re-checked at the 01-13..01-16 gap-closure round (2026-08-24): the round's scope — a first-party wire-format change (chunk-frame kind byte), balloon TX state-machine guards, base defer-aware pass accounting, CommandSender retry semantics, and an operator bench session — adds no external API surface. The declaration above stands unchanged.

Re-checked at the 01-21..01-24 gap-closure round (2026-08-28): the round's scope — balloon/base transfer-scheduling receipt-evidence changes, review round #9 robustness fixes (sidecar guard, command drain, framer resync, JSON escaping, camera power branch), and an operator bench session — is entirely first-party firmware with no external service, SDK, or REST/gRPC/webhook surface. The declaration above stands unchanged.
