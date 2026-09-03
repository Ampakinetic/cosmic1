#ifndef MISSION_MANAGER_H
#define MISSION_MANAGER_H

#include <Arduino.h>
#include <SD_MMC.h>

// ===========================
// Mission Manager
// Base Station - Named flight missions: bundle the GPS track, capture and
// alert events, and (via the sidecar missionId) the images of one flight
// (feature: missions, 2026-09-03)
// ===========================
// Storage layout on the SD card (all timestamps are base millis() — the
// base has no RTC; a mission's replay timeline is relative to its own
// start, maintained monotonically across reboots via the registry):
//
//   /missions/registry.jsonl    append-only lifecycle log, one line each:
//                               {"id":N,"ev":"start","name":"..","t":ms}
//                               {"id":N,"ev":"end","t":ms}
//                               The last start line per id carries the
//                               name; the last end line closes the mission.
//   /missions/ACTIVE            text id of the active mission (absent or
//                               "0" = none). Rewritten at start/end —
//                               survives power cycles, so a field mission
//                               keeps logging into its track after a reboot.
//   /missions/<id>/track.jsonl  one line per GPS fix while active:
//                               {"t":ms,"lat":deg,"lon":deg,"alt":m}
//   /missions/<id>/events.jsonl one line per capture / alert / lifecycle
//                               event: {"t":ms,"k":"cap|alert|ack|start|end"
//                               ,"id":N,"ty":N}
//
// Event scope (locked with the operator): captures, alert triggers and
// acknowledgements, and mission lifecycle. Command/link chatter is
// deliberately excluded.
//
// /sd-clear leaves /missions untouched — mission history is the archive,
// not scratch space. Names are sanitized to an allowlist (no path
// characters exist in the layout at all: ids are numeric, names live only
// inside JSON lines).

static constexpr size_t MISSION_NAME_MAX = 24;   // chars, sanitized

class MissionManager {
public:
    bool begin();

    bool     active() const { return active_; }
    uint32_t activeId() const { return activeId_; }
    const char* activeName() const { return activeName_; }

    // Lifecycle (called from the web handlers). start() fails honestly if
    // a mission is already active or the card is unwritable; on success
    // the caller resets the live trajectory ring so the RAM track shows
    // THIS flight only.
    bool start(const char* name, uint32_t* outId);
    bool end();

    // Tees — all no-ops when no mission is active
    void logTrackPoint(int32_t latE6, int32_t lonE6, int32_t altM);
    void logCapture(uint16_t imageId);
    void logAlert(uint8_t type, bool ack);

    // {"active":{...}|null,"missions":[... newest first]}
    String serializeStatus();

private:
    bool   openMissionFiles();
    void   closeMissionFiles();
    bool   appendLine(File& f, const String& line);
    bool   appendRegistryLine(const String& line);
    int64_t missionNowMs() const;   // mission-relative, reboot-monotonic

    bool     active_ = false;
    uint32_t activeId_ = 0;
    char     activeName_[MISSION_NAME_MAX + 1] = {0};
    int64_t  startAbsMs_ = 0;   // absolute base-millis of mission t=0
    uint32_t nextId_ = 1;       // folded from the registry at begin()
    File     trackFile_;
    File     eventFile_;
};

// ===========================
// Global Instance Access
// ===========================

extern MissionManager& MISSIONS();

#endif // MISSION_MANAGER_H
