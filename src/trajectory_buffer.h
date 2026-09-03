#ifndef TRAJECTORY_BUFFER_H
#define TRAJECTORY_BUFFER_H

#include <Arduino.h>
#include "image_rx_manager.h"   // TelemetrySnapshot (pull-based, no rx changes)

// ===========================
// Trajectory Buffer
// Base Station - Full-flight GPS track in a capped ring buffer (D-38)
// Phase 3: Enhanced Web Interface (plan 03-02)
// ===========================
// Records ONE point per received 0x14 telemetry beacon (only while the fix
// is valid) into a fixed static ring — the whole flight is always visible in
// bounded memory. The base env has no PSRAM, so the ring lives in internal
// RAM: 500 points x 12 B = ~6 KB (TRAJ_MAX_POINTS is editable in the
// CONTEXT D-38 discretion range 500-1000; the /api/state payload was
// measured at the 500-point cap before locking it).
//
// Pull-based by design: process() polls ImageRx().getTelemetrySnapshot()
// from the base loop — image_rx_manager is never modified for this. A
// duplicate snapshot (same beacon seq) never double-appends.

// D-38 cap — one point per 5 s beacon = ~42 min of flight at 500 points
static constexpr uint16_t TRAJ_MAX_POINTS = 500;

// One recorded GPS fix, stored at wire precision (degrees x 1e6 like the
// beacon's latE6/lonE6; altitude in whole meters) — no per-point heap.
struct TrajectoryPoint {
    int32_t latE6;   // latitude degrees * 1e6
    int32_t lonE6;   // longitude degrees * 1e6
    int32_t altM;    // altitude, whole meters
};

class TrajectoryBuffer {
public:
    TrajectoryBuffer();

    // Initialization — no hardware to bring up; zeroes the ring and logs
    // the module banner like the other base managers.
    bool begin();

    // Loop-driven: poll the telemetry snapshot and append exactly one
    // point when the beacon seq advances with a valid GPS fix.
    void process();

    // Mission start (feature: missions): wipe the ring so the live track
    // shows THIS flight only — the previous flight lives in its mission's
    // track.jsonl on the card
    void reset();

    // Live point count (0..TRAJ_MAX_POINTS)
    uint16_t getCount() const { return count; }

    // Chronological access (index 0 = OLDEST recorded fix) — wraparound-safe
    TrajectoryPoint getPoint(uint16_t index) const;

    // Chronological copy of up to maxPoints points (index 0 = oldest);
    // returns the number of points actually written.
    uint16_t getSnapshot(TrajectoryPoint* out, uint16_t maxPoints) const;

private:
    TrajectoryPoint points[TRAJ_MAX_POINTS];
    uint16_t head;             // next write slot (wraparound-safe arithmetic)
    uint16_t count;            // valid points, 0..TRAJ_MAX_POINTS
    uint16_t lastRecordedSeq;  // beacon seq of the last appended point
    bool     hasLastSeq;       // false until the first append (seq 0 beacons)

    void append(int32_t latE6, int32_t lonE6, int32_t altM);
};

// ===========================
// Global Instance Access
// ===========================

extern TrajectoryBuffer& Trajectory();

#endif // TRAJECTORY_BUFFER_H
