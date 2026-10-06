#include "decision.h"

#include <algorithm>

Decision EvaluateProximity(const Settings& settings,
                           const std::vector<Observation>& observations,
                           uint64_t now)
{
    Decision result;
    int sum = 0;
    for (const SelectedDevice& device : settings.devices)
    {
        auto found = std::find_if(observations.begin(), observations.end(),
            [&](const Observation& entry) { return entry.address == device.address; });
        if (found == observations.end() || found->receivedTick > now ||
            now - found->receivedTick > 20000) continue;
        ++result.detected;
        sum += found->rssi;
        result.newestTick = std::max(result.newestTick, found->receivedTick);
    }
    if (result.detected)
    {
        result.meanRssi = sum / result.detected;
        result.unlock = result.meanRssi >= settings.unlockThreshold;
        result.outsideLockRange = result.meanRssi < settings.lockThreshold;
    }
    return result;
}
