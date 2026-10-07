#pragma once

#include "settings.h"

struct Observation
{
    uint64_t address;
    int rssi;
    uint64_t receivedTick;
};

struct Decision
{
    int detected = 0;
    int meanRssi = 0;
    uint64_t newestTick = 0;
    bool unlock = false;
    bool outsideLockRange = true;
};

// Only selected devices observed in the last 20 seconds enter the mean.
Decision EvaluateProximity(const Settings& settings,
                           const std::vector<Observation>& observations,
                           uint64_t now);

// One mode is selected explicitly; LAN-only never inherits a stale Bluetooth result.
bool UnlockConditionMet(int mode, bool bluetoothNear, bool lanPresent);
