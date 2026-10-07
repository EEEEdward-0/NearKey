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

    if (!UnlockConditionMet(0, true, false) || UnlockConditionMet(0, false, true)) return 4;
    if (!UnlockConditionMet(1, false, true) || UnlockConditionMet(1, true, false)) return 5;
    if (!UnlockConditionMet(2, true, true) || UnlockConditionMet(2, true, false)) return 6;

    // Apply thresholds to the mean, even when one individual device is strong.
    result = EvaluateProximity(settings,
        {{0x111111111111, -40, now}, {0x222222222222, -92, now}}, now);
    if (result.meanRssi != -66 || result.unlock) return 7;
    result = EvaluateProximity(settings,
        {{0x111111111111, -40, now}, {0x222222222222, -90, now}}, now);
    if (result.meanRssi != -65 || !result.unlock) return 8;

    // Equality permits unlocking but does not count as below the locking threshold.
    result = EvaluateProximity(settings,
        {{0x111111111111, -70, now}, {0x222222222222, -90, now}}, now);
    if (result.meanRssi != -80 || result.outsideLockRange) return 9;
    result = EvaluateProximity(settings,
        {{0x111111111111, -72, now}, {0x222222222222, -90, now}}, now);
    if (result.meanRssi != -81 || !result.outsideLockRange) return 10;

    std::puts("Proximity decisions passed.");
    return 0;
}
