#pragma once

#include <windows.h>

// Only a recent observation from the signed-in user's scanner enables the tile.
bool IsIPhoneNearby(PCWSTR userSid);

// Returns a CoTaskMemAlloc buffer. The caller must zero and free it promptly.
HRESULT LoadDemoPassword(PCWSTR userSid, PWSTR *password);
