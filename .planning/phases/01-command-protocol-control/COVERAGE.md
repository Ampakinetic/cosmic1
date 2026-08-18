# API Coverage — Phase 01

> Full coverage by default. Opt-outs are explicit, reasoned decisions.

No external API integration: first-party ESP32-S3 firmware only — E32 LoRa over UART, esp32-camera, embedded web server; no external service, SDK, or REST/gRPC/webhook surface.

The detector's "api" signal traces to plan 01-03 prose about the first-party web server's own HTTP endpoints ("Direct API posts with empty values rely on the 400 path"), not to any external service.
