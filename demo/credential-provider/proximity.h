#pragma once

#include <windows.h>

// Only a fresh condition packet from the bound user scanner or installed SYSTEM service permits submission.
bool IsUnlockConditionMet(PCWSTR userSid, DWORD* unlockKey = nullptr);

// Returns a CoTaskMemAlloc buffer. The caller must zero and free it promptly.
HRESULT LoadDemoPassword(PCWSTR userSid, PWSTR *password);

// Records non-secret login-flow diagnostics for the local test machine.
void RecordLoginFlow(PCWSTR valueName, DWORD value);

// True only if the configured user already owns the active console session.
bool IsExistingSessionForUser(PCWSTR userSid);

// Bound account: existing console session, or administrator-enabled pre-logon service.
bool IsSupportedLoginForUser(PCWSTR userSid);
