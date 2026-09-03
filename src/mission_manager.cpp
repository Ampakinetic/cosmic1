#include "mission_manager.h"

// ===========================
// Static Instance
// ===========================

static MissionManager missionInstance;
MissionManager& MISSIONS() {
    return missionInstance;
}

// Registry read cap: the last 8 KB folds into the status listing — dozens
// of missions at one short line each; beyond that, oldest missions drop
// off the picker (documented honest degradation, files stay on the card).
static constexpr size_t MISSION_REGISTRY_READ_CAP = 8192;
static constexpr uint32_t MISSIONS_FOLD_MAX = 64;

// ===========================
// Helpers
// ===========================

// Name allowlist: letters, digits, space, dash, underscore, apostrophe.
// Everything else is dropped; empty results fall back to Mission-<id>.
static void sanitizeMissionName(const char* in, char* out, size_t cap) {
    size_t o = 0;
    for (const char* p = in; *p && o < cap - 1; p++) {
        const char c = *p;
        const bool ok = (c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z') ||
                        (c >= '0' && c <= '9') || c == ' ' || c == '-' ||
                        c == '_' || c == '\'';
        if (ok) {
            out[o++] = c;
        }
    }
    out[o] = '\0';
}

// Parse one registry line: {"id":N,"ev":"start"|"end","name":"..","t":N}
// Machine-written fixed field order — strstr/strtol extraction is exact.
struct MissionRegistryLine {
    uint32_t    id = 0;
    bool        isStart = false;
    bool        valid = false;
    char        name[MISSION_NAME_MAX + 1] = {0};
    int64_t     t = 0;
};

static bool parseRegistryLine(const char* line, MissionRegistryLine& out) {
    const char* idP = strstr(line, "\"id\":");
    const char* evP = strstr(line, "\"ev\":\"");
    if (!idP || !evP) {
        return false;
    }
    out.id = static_cast<uint32_t>(strtoul(idP + 5, nullptr, 10));
    out.isStart = strncmp(evP + 6, "start", 5) == 0;
    out.valid = out.id > 0;
    if (out.isStart) {
        const char* nameP = strstr(line, "\"name\":\"");
        if (nameP) {
            nameP += 8;
            size_t o = 0;
            while (*nameP && *nameP != '"' && o < MISSION_NAME_MAX) {
                out.name[o++] = *nameP++;
            }
            out.name[o] = '\0';
        }
    }
    const char* tP = strstr(line, "\"t\":");
    if (tP) {
        out.t = strtoll(tP + 4, nullptr, 10);
    }
    return out.valid;
}

// ===========================
// Lifecycle
// ===========================

bool MissionManager::begin() {
    // Card mounted by SdStorage::begin() before this in the base setup
    if (!SD_MMC.exists("/missions")) {
        SD_MMC.mkdir("/missions");
    }

    // Active marker: text id, "0"/absent = none
    uint32_t id = 0;
    File af = SD_MMC.open("/missions/ACTIVE", FILE_READ);
    if (af) {
        char buf[16] = {0};
        const size_t got = af.readBytes(buf, sizeof(buf) - 1);
        af.close();
        if (got > 0) {
            id = static_cast<uint32_t>(strtoul(buf, nullptr, 10));
        }
    }

    // Fold the registry: names per id, the original start t (the replay
    // time anchor), nextId, and the resume anchor for an active mission
    char nameForId[MISSION_NAME_MAX + 1] = {0};
    int64_t startTForId = 0;
    bool resumeActive = false;
    File rf = SD_MMC.open("/missions/registry.jsonl", FILE_READ);
    if (rf) {
        // Bounded tail read: the last MISSION_REGISTRY_READ_CAP bytes are
        // the recent history (honest degradation, documented above)
        const size_t fileSize = rf.size();
        if (fileSize > MISSION_REGISTRY_READ_CAP) {
            rf.seek(fileSize - MISSION_REGISTRY_READ_CAP);
        }
        uint32_t maxId = 0;
        while (rf.available()) {
            String line = rf.readStringUntil('\n');
            line.trim();
            if (line.length() == 0) {
                continue;
            }
            MissionRegistryLine rl;
            if (!parseRegistryLine(line.c_str(), rl)) {
                continue;
            }
            if (rl.id > maxId) {
                maxId = rl.id;
            }
            if (rl.isStart) {
                snprintf(nameForId, sizeof(nameForId), "%s", rl.name);
                startTForId = rl.t;
            } else {
                nameForId[0] = '\0';
                startTForId = 0;
            }
            if (id != 0 && rl.id == id) {
                // Resume anchor: the original start t of THIS mission
                if (rl.isStart) {
                    startTForId = rl.t;
                    resumeActive = true;
                } else {
                    resumeActive = false;   // a later end line closed it
                }
            }
        }
        rf.close();
        nextId_ = maxId + 1;
    }

    if (id != 0 && resumeActive) {
        // Continue the interrupted mission: reopen append handles and anchor
        // the timeline so new lines stay monotonic with the pre-reboot ones
        active_ = true;
        activeId_ = id;
        snprintf(activeName_, sizeof(activeName_), "%s",
                 nameForId[0] ? nameForId : "Resumed");
        startAbsMs_ = static_cast<int64_t>(startTForId) - static_cast<int64_t>(millis());
        if (openMissionFiles()) {
            Serial.printf("MissionManager: resumed mission %u '%s' — track continues\n",
                          static_cast<unsigned>(activeId_), activeName_);
        } else {
            Serial.println("MissionManager: resume FAILED to open track files — mission marked ended");
            appendRegistryLine(String("{\"id\":") + String(activeId_) +
                               ",\"ev\":\"end\",\"t\":" + String(static_cast<long long>(missionNowMs())) + "}");
            active_ = false;
            activeId_ = 0;
            SD_MMC.remove("/missions/ACTIVE");
        }
    } else {
        active_ = false;
        activeId_ = 0;
    }

    Serial.printf("MissionManager: ready (%s, next id %u)\n",
                  active_ ? "mission ACTIVE" : "idle",
                  static_cast<unsigned>(nextId_));
    return true;
}

bool MissionManager::start(const char* name, uint32_t* outId) {
    if (active_) {
        return false;
    }
    if (!SD_MMC.exists("/missions")) {
        SD_MMC.mkdir("/missions");
    }

    const uint32_t id = nextId_++;
    char safe[MISSION_NAME_MAX + 1];
    sanitizeMissionName(name, safe, sizeof(safe));
    if (safe[0] == '\0') {
        snprintf(safe, sizeof(safe), "Mission-%u", static_cast<unsigned>(id));
    }

    char dir[32];
    snprintf(dir, sizeof(dir), "/missions/%u", static_cast<unsigned>(id));
    if (!SD_MMC.exists(dir)) {
        SD_MMC.mkdir(dir);
    }

    startAbsMs_ = static_cast<int64_t>(millis());
    active_ = true;
    activeId_ = id;
    snprintf(activeName_, sizeof(activeName_), "%s", safe);

    if (!openMissionFiles()) {
        // Roll the lifecycle back — an unopenable track means no mission
        active_ = false;
        activeId_ = 0;
        nextId_--;
        return false;
    }

    const String startLine = String("{\"id\":") + String(static_cast<unsigned>(id)) +
                             ",\"ev\":\"start\",\"name\":\"" + String(safe) +
                             "\",\"t\":" + String(static_cast<long long>(missionNowMs())) + "}";
    appendLine(eventFile_, startLine);
    appendRegistryLine(startLine);

    File af = SD_MMC.open("/missions/ACTIVE", FILE_WRITE);
    if (af) {
        af.print(String(static_cast<unsigned>(id)) + "\n");
        af.close();
    }

    if (outId) {
        *outId = id;
    }
    Serial.printf("MissionManager: mission %u '%s' STARTED\n",
                  static_cast<unsigned>(id), safe);
    return true;
}

bool MissionManager::end() {
    if (!active_) {
        return false;
    }
    const int64_t now = missionNowMs();
    appendLine(eventFile_, String("{\"t\":") + String(static_cast<long long>(now)) +
                           ",\"k\":\"end\"}");
    appendRegistryLine(String("{\"id\":") + String(static_cast<unsigned>(activeId_)) +
                       ",\"ev\":\"end\",\"t\":" + String(static_cast<long long>(now)) + "}");
    closeMissionFiles();
    SD_MMC.remove("/missions/ACTIVE");
    Serial.printf("MissionManager: mission %u '%s' ended (%.1f min)\n",
                  static_cast<unsigned>(activeId_), activeName_, now / 60000.0f);
    active_ = false;
    activeId_ = 0;
    activeName_[0] = '\0';
    return true;
}

// ===========================
// Tees
// ===========================

int64_t MissionManager::missionNowMs() const {
    return static_cast<int64_t>(millis()) - startAbsMs_;
}

void MissionManager::logTrackPoint(int32_t latE6, int32_t lonE6, int32_t altM) {
    if (!active_ || !trackFile_) {
        return;
    }
    const int64_t now = missionNowMs();
    String line = String("{\"t\":") + String(static_cast<long long>(now)) +
                  ",\"lat\":" + String(static_cast<float>(latE6) / 1e6f, 6) +
                  ",\"lon\":" + String(static_cast<float>(lonE6) / 1e6f, 6) +
                  ",\"alt\":" + String(static_cast<long>(altM)) + "}";
    appendLine(trackFile_, line);
}

void MissionManager::logCapture(uint16_t imageId) {
    if (!active_ || !eventFile_) {
        return;
    }
    appendLine(eventFile_, String("{\"t\":") + String(static_cast<long long>(missionNowMs())) +
                           ",\"k\":\"cap\",\"id\":" + String(static_cast<unsigned>(imageId)) + "}");
}

void MissionManager::logAlert(uint8_t type, bool ack) {
    if (!active_ || !eventFile_) {
        return;
    }
    appendLine(eventFile_, String("{\"t\":") + String(static_cast<long long>(missionNowMs())) +
                           ",\"k\":\"" + (ack ? "ack" : "alert") + "\"" +
                           ",\"ty\":" + String(static_cast<unsigned>(type)) + "}");
}

// ===========================
// Status Serialization
// ===========================

String MissionManager::serializeStatus() {
    String json = "{\"active\":";
    if (active_) {
        json += "{\"id\":" + String(static_cast<unsigned>(activeId_)) +
                ",\"name\":\"" + String(activeName_) + "\"}";
    } else {
        json += "null";
    }
    json += ",\"missions\":[";

    // Fold the registry (bounded tail read) into per-id rows, newest first
    struct Row {
        uint32_t id;
        char     name[MISSION_NAME_MAX + 1];
        int64_t  startT;
        int64_t  endT;      // -1 = still open per the folded lines
    };
    static Row rows[MISSIONS_FOLD_MAX];
    uint16_t rowCount = 0;
    uint32_t maxId = 0;

    File rf = SD_MMC.open("/missions/registry.jsonl", FILE_READ);
    if (rf) {
        const size_t fileSize = rf.size();
        if (fileSize > MISSION_REGISTRY_READ_CAP) {
            rf.seek(fileSize - MISSION_REGISTRY_READ_CAP);
        }
        while (rf.available()) {
            String line = rf.readStringUntil('\n');
            line.trim();
            if (line.length() == 0) {
                continue;
            }
            MissionRegistryLine rl;
            if (!parseRegistryLine(line.c_str(), rl)) {
                continue;
            }
            if (rl.id > maxId) {
                maxId = rl.id;
            }
            int found = -1;
            for (uint16_t i = 0; i < rowCount; i++) {
                if (rows[i].id == rl.id) {
                    found = static_cast<int>(i);
                    break;
                }
            }
            if (found < 0 && rowCount < MISSIONS_FOLD_MAX) {
                found = static_cast<int>(rowCount++);
                rows[found].id = rl.id;
                rows[found].name[0] = '\0';
                rows[found].startT = -1;
                rows[found].endT = -1;
            }
            if (found >= 0) {
                if (rl.isStart) {
                    snprintf(rows[found].name, sizeof(rows[found].name), "%s", rl.name);
                    rows[found].startT = rl.t;
                } else {
                    rows[found].endT = rl.t;
                }
            }
        }
        rf.close();
    }
    if (nextId_ <= maxId) {
        nextId_ = maxId + 1;
    }

    bool first = true;
    for (int i = static_cast<int>(rowCount) - 1; i >= 0; i--) {
        if (rows[i].startT < 0) {
            continue;   // end line without a start in the folded window
        }
        if (!first) {
            json += ",";
        }
        first = false;
        const int64_t dur =
            (rows[i].endT >= 0 ? rows[i].endT - rows[i].startT : -1);
        json += "{\"id\":" + String(static_cast<unsigned>(rows[i].id)) +
                ",\"name\":\"" + String(rows[i].name) + "\"" +
                ",\"durMs\":" + String(static_cast<long long>(dur)) + "}";
    }
    json += "]}";
    return json;
}

// ===========================
// File Plumbing
// ===========================

bool MissionManager::appendLine(File& f, const String& line) {
    if (!f) {
        return false;
    }
    const bool ok = f.print(line + "\n") == line.length() + 1;
    if (!ok) {
        // One-line warning per failure; the mission keeps running — the
        // live RAM surfaces are unaffected, this archive line is lost
        Serial.println("MissionManager: log line write FAILED (disk full?)");
    }
    return ok;
}

bool MissionManager::appendRegistryLine(const String& line) {
    File f = SD_MMC.open("/missions/registry.jsonl", FILE_APPEND);
    if (!f) {
        Serial.println("MissionManager: registry open FAILED");
        return false;
    }
    const bool ok = appendLine(f, line);
    f.close();
    return ok;
}

bool MissionManager::openMissionFiles() {
    char path[48];
    snprintf(path, sizeof(path), "/missions/%u/track.jsonl",
             static_cast<unsigned>(activeId_));
    trackFile_ = SD_MMC.open(path, FILE_APPEND);
    snprintf(path, sizeof(path), "/missions/%u/events.jsonl",
             static_cast<unsigned>(activeId_));
    eventFile_ = SD_MMC.open(path, FILE_APPEND);
    if (!trackFile_ || !eventFile_) {
        closeMissionFiles();
        return false;
    }
    return true;
}

void MissionManager::closeMissionFiles() {
    if (trackFile_) {
        trackFile_.close();
    }
    if (eventFile_) {
        eventFile_.close();
    }
}
