#include "decision.h"
#include <cstdio>

int main()
{
    Settings settings;
    settings.devices = {{0x111111111111, L"phone"}, {0x222222222222, L"watch"}};
    settings.unlockThreshold = -65;
    settings.lockThreshold = -80;
    const uint64_t now = 100000;

    // The missing watch must not dilute a detected phone's signal.
    auto result = EvaluateProximity(settings, {{0x111111111111, -55, now}}, now);
    if (result.detected != 1 || result.meanRssi != -55 || !result.unlock) return 1;

    // A weak second device enters the average when it is actually detected.
    result = EvaluateProximity(settings,
        {{0x111111111111, -50, now}, {0x222222222222, -90, now}}, now);
    if (result.detected != 2 || result.meanRssi != -70 || result.unlock ||
        result.outsideLockRange) return 2;

    // Stale observations and unrelated devices do not count as presence.
    result = EvaluateProximity(settings,
        {{0x111111111111, -40, now - 20001}, {0x333333333333, -30, now}}, now);
    if (result.detected != 0 || result.unlock || !result.outsideLockRange) return 3;

    std::puts("Proximity decisions passed.");
    return 0;
}
