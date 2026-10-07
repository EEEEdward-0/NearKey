#pragma once

#include <windows.h>

// Only a recent observation from the signed-in user's scanner enables the tile.
bool IsUnlockConditionMet(PCWSTR userSid);

// Returns a CoTaskMemAlloc buffer. The caller must zero and free it promptly.
HRESULT LoadDemoPassword(PCWSTR userSid, PWSTR *password);

// Records non-secret login-flow diagnostics for the local test machine.
void RecordLoginFlow(PCWSTR valueName, DWORD value);

// True only if the configured user already owns the active console session.
bool IsExistingSessionForUser(PCWSTR userSid);
