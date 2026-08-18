# API Coverage — Phase 01

> Full coverage by default. Opt-outs are explicit, reasoned decisions.

No external API integration: this phase's scope is first-party ESP32-S3 firmware — a UART-driven E32 LoRa radio, the esp32-camera sensor, and a first-party embedded HTTP server. The detector's "api" signal traces to plan 01-03 prose about the first-party web server's own HTTP endpoints ("Direct API posts with empty values rely on the 400 path"), not to any external service, SDK, or REST/gRPC/webhook surface.
