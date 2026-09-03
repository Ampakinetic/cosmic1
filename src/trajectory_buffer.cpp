#include "trajectory_buffer.h"
#include "mission_manager.h"   // mission track tee (feature: missions)

// ===========================
// Static Instance
// ===========================

static TrajectoryBuffer trajectoryInstance;
TrajectoryBuffer& Trajectory() {
    return trajectoryInstance;
}

// ===========================
// Constructor
// ===========================

TrajectoryBuffer::TrajectoryBuffer()
    : head(0)
    , count(0)
    , lastRecordedSeq(0)
    , hasLastSeq(false)
{
}

// ===========================
// Initialization
// ===========================

bool TrajectoryBuffer::begin() {
    // No hardware behind this module — a no-op init that keeps the manager
    // lifecycle symmetric with the other base modules. The ring is already
    // zeroed by static storage; reset the indices in case of a re-begin.
    head = 0;
    count = 0;
    lastRecordedSeq = 0;
    hasLastSeq = false;

    Serial.printf("TrajectoryBuffer: ready (%u points max, one per valid GPS beacon)\n",
                  static_cast<unsigned>(TRAJ_MAX_POINTS));
    return true;
}

// ===========================
// Recording
// ===========================

void TrajectoryBuffer::process() {
    // Pull the newest beacon snapshot — image_rx_manager keeps its own
    // latched copy, so polling from the base loop sees every seq exactly
    // once it arrives (and re-reading the same snapshot is a no-op).
    const TelemetrySnapshot& snap = ImageRx().getTelemetrySnapshot();

    // Absent telemetry or an invalid fix NEVER appends — a guessed or
    // stale position is never recorded as current track (WEB-02 truth).
    if (!snap.valid || !snap.gpsValid) {
        return;
    }

    // Exactly one point per beacon seq: a duplicate snapshot (same seq,
    // re-polled between beacons) never double-appends.
    if (hasLastSeq && snap.seq == lastRecordedSeq) {
        return;
    }

    // Store at wire precision: degrees x 1e6 (same scale the beacon
    // carries) and whole meters — no float kept per point.
    const int32_t latE6 = lroundf(snap.lat * 1e6f);
    const int32_t lonE6 = lroundf(snap.lon * 1e6f);
    const int32_t altM  = lroundf(snap.altitudeM);
    append(latE6, lonE6, altM);

    // Mission tee (feature: missions): an active mission persists every
    // fix to its track.jsonl — the flight log survives reboots, unlike
    // this RAM ring
    MISSIONS().logTrackPoint(latE6, lonE6, altM);

    lastRecordedSeq = snap.seq;
    hasLastSeq = true;
}

void TrajectoryBuffer::reset() {
    head = 0;
    count = 0;
    lastRecordedSeq = 0;
    hasLastSeq = false;
}

void TrajectoryBuffer::append(int32_t latE6, int32_t lonE6, int32_t altM) {
    points[head].latE6 = latE6;
    points[head].lonE6 = lonE6;
    points[head].altM = altM;

    // Overwrite-oldest on wrap: head always points at the next free (or
    // oldest) slot; once full, the count stays pinned at the cap.
    head = static_cast<uint16_t>((head + 1) % TRAJ_MAX_POINTS);
    if (count < TRAJ_MAX_POINTS) {
        count++;
    }
}

// ===========================
// Chronological Access
// ===========================

TrajectoryPoint TrajectoryBuffer::getPoint(uint16_t index) const {
    // Out-of-range reads return a zeroed point — callers iterate
    // 0..getCount()-1, so this is a defensive floor, never a data source.
    if (index >= count) {
        TrajectoryPoint zero = {0, 0, 0};
        return zero;
    }
    // Oldest live slot: head has advanced past it (or sits at 0 while the
    // ring is still filling). All arithmetic wraps TRAJ_MAX_POINTS.
    uint16_t oldest = static_cast<uint16_t>((head + TRAJ_MAX_POINTS - count)
                                            % TRAJ_MAX_POINTS);
    uint16_t slot = static_cast<uint16_t>((oldest + index) % TRAJ_MAX_POINTS);
    return points[slot];
}

uint16_t TrajectoryBuffer::getSnapshot(TrajectoryPoint* out, uint16_t maxPoints) const {
    uint16_t n = (count < maxPoints) ? count : maxPoints;
    for (uint16_t i = 0; i < n; i++) {
        out[i] = getPoint(i);
    }
    return n;
}
