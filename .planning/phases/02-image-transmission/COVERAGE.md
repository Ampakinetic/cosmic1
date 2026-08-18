# API Coverage — Phase 02 (Image Transmission)

No external API integration: this phase is ESP32 firmware exchanging framed packets over a UART-attached LoRa radio (E32-900T30D) plus local SD-card persistence — no third-party API, SDK, REST/gRPC endpoint, webhook, or OAuth surface is consumed or exposed. The detector's `true` signal traced to the word "surface" in a sidecar-truth sentence, not to any API integration.
