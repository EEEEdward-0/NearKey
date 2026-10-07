//
// THIS CODE AND INFORMATION IS PROVIDED "AS IS" WITHOUT WARRANTY OF
// ANY KIND, EITHER EXPRESSED OR IMPLIED, INCLUDING BUT NOT LIMITED TO
// THE IMPLIED WARRANTIES OF MERCHANTABILITY AND/OR FITNESS FOR A
// PARTICULAR PURPOSE.
//
// Copyright (c) Microsoft Corporation. All rights reserved.
//
// CSampleProvider implements ICredentialProvider, which is the main
// interface that logonUI uses to decide which tiles to display.
// In this sample, we will display one tile that uses each of the nine
// available UI controls.

#include <initguid.h>
#include "CSampleProvider.h"
#include "CSampleCredential.h"
#include "guid.h"
#include "proximity.h"
#include <thread>

namespace
{
    std::atomic<ULONGLONG> pendingKeyUnlock{0};
    std::atomic<ULONGLONG> lastKeyUnlock{0};
}

CSampleProvider::CSampleProvider():
    _cRef(1),
    _pCredential(nullptr),
    _pCredProviderUserArray(nullptr)
{
    DllAddRef();
}

CSampleProvider::~CSampleProvider()
{
    UnAdvise();
    if (_pCredential != nullptr)
    {
        _pCredential->Release();
        _pCredential = nullptr;
    }
    if (_pCredProviderUserArray != nullptr)
    {
        _pCredProviderUserArray->Release();
        _pCredProviderUserArray = nullptr;
    }

    DllRelease();
}

// SetUsageScenario is the provider's cue that it's going to be asked for tiles
// in a subsequent call.
HRESULT CSampleProvider::SetUsageScenario(
    CREDENTIAL_PROVIDER_USAGE_SCENARIO cpus,
    DWORD /*dwFlags*/)
{
    HRESULT hr;

    // Decide which scenarios to support here. Returning E_NOTIMPL simply tells the caller
    // that we're not designed for that scenario.
    switch (cpus)
    {
    case CPUS_LOGON:
    case CPUS_UNLOCK_WORKSTATION:
        // The reason why we need _fRecreateEnumeratedCredentials is because ICredentialProviderSetUserArray::SetUserArray() is called after ICredentialProvider::SetUsageScenario(),
        // while we need the ICredentialProviderUserArray during enumeration in ICredentialProvider::GetCredentialCount()
        _cpus = cpus;
        _fRecreateEnumeratedCredentials = true;
        hr = S_OK;
        break;

    case CPUS_CHANGE_PASSWORD:
    case CPUS_CREDUI:
        hr = E_NOTIMPL;
        break;

    default:
        hr = E_INVALIDARG;
        break;
    }

    return hr;
}

// SetSerialization takes the kind of buffer that you would normally return to LogonUI for
// an authentication attempt.  It's the opposite of ICredentialProviderCredential::GetSerialization.
// GetSerialization is implement by a credential and serializes that credential.  Instead,
// SetSerialization takes the serialization and uses it to create a tile.
//
// SetSerialization is called for two main scenarios.  The first scenario is in the credui case
// where it is prepopulating a tile with credentials that the user chose to store in the OS.
// The second situation is in a remote logon case where the remote client may wish to
// prepopulate a tile with a username, or in some cases, completely populate the tile and
// use it to logon without showing any UI.
//
// If you wish to see an example of SetSerialization, please see either the SampleCredentialProvider
// sample or the SampleCredUICredentialProvider sample.  [The logonUI team says, "The original sample that
// this was built on top of didn't have SetSerialization.  And when we decided SetSerialization was
// important enough to have in the sample, it ended up being a non-trivial amount of work to integrate
// it into the main sample.  We felt it was more important to get these samples out to you quickly than to
// hold them in order to do the work to integrate the SetSerialization changes from SampleCredentialProvider
// into this sample.]
HRESULT CSampleProvider::SetSerialization(
    _In_ CREDENTIAL_PROVIDER_CREDENTIAL_SERIALIZATION const * /*pcpcs*/)
{
    return E_NOTIMPL;
}

// Called by LogonUI to give you a callback.  Providers often use the callback if they
// some event would cause them to need to change the set of tiles that they enumerated.
HRESULT CSampleProvider::Advise(
    _In_ ICredentialProviderEvents *pcpe,
    _In_ UINT_PTR upAdviseContext)
{
    UnAdvise();
    if ((_cpus != CPUS_UNLOCK_WORKSTATION && _cpus != CPUS_LOGON) || pcpe == nullptr) return S_OK;
    IStream *stream = nullptr;
    HRESULT hr = CoMarshalInterThreadInterfaceInStream(IID_ICredentialProviderEvents, pcpe, &stream);
    if (FAILED(hr)) return hr;
    auto stop = std::make_shared<std::atomic<bool>>(false);
    _stopProximityWatch = stop;
    HDESK desktop = GetThreadDesktop(GetCurrentThreadId());
    std::thread([stream, stop, upAdviseContext, desktop]()
    {
        const bool desktopSet = desktop != nullptr && SetThreadDesktop(desktop);
        const DWORD desktopError = desktopSet ? ERROR_SUCCESS : GetLastError();
        RecordLoginFlow(L"LastEnterProbeDesktopSet", desktopSet ? 1 : 0);
        RecordLoginFlow(L"LastEnterProbeDesktopError", desktopError);
        if (FAILED(CoInitializeEx(nullptr, COINIT_MULTITHREADED)))
        {
            stream->Release();
            return;
        }
        ICredentialProviderEvents *events = nullptr;
        if (SUCCEEDED(CoGetInterfaceAndReleaseStream(stream, IID_PPV_ARGS(&events))))
        {
            wchar_t sid[256] = {};
            DWORD bytes = sizeof(sid);
            const bool hasSid = RegGetValueW(HKEY_LOCAL_MACHINE,
                L"SOFTWARE\\BluetoothUnlockDemo", L"UserSid", RRF_RT_REG_SZ,
                nullptr, sid, &bytes) == ERROR_SUCCESS;
            DWORD unlockKey = VK_RETURN;
            if (hasSid && IsSupportedLoginForUser(sid))
                IsUnlockConditionMet(sid, &unlockKey);
            DWORD keyCount = 0;
            DWORD countBytes = sizeof(keyCount);
            RegGetValueW(HKEY_LOCAL_MACHINE, L"SOFTWARE\\BluetoothUnlockDemo",
                L"LastUnlockKeyProbeCount", RRF_RT_REG_DWORD, nullptr, &keyCount, &countBytes);
            bool keyWasDown = (GetAsyncKeyState(unlockKey) & 0x8000) != 0;
            unsigned keySettingPolls = 0;
            RecordLoginFlow(L"LastUnlockKeyProbeStartTick", GetTickCount());
            while (!stop->load())
            {
                Sleep(50);
                if (stop->load()) break;
                const bool keyIsDown = (GetAsyncKeyState(unlockKey) & 0x8000) != 0;
                // Trigger on a new press, not while the key is held. Recheck the live key
                // and proximity together so a recently changed setting cannot submit.
                if (keyIsDown && !keyWasDown)
                {
                    RecordLoginFlow(L"LastUnlockKeyProbeTick", GetTickCount());
                    RecordLoginFlow(L"LastUnlockKeyProbeCount", ++keyCount);
                    DWORD liveKey = VK_RETURN;
                    if (hasSid && IsSupportedLoginForUser(sid) &&
                        IsUnlockConditionMet(sid, &liveKey) && liveKey == unlockKey)
                    {
                        const ULONGLONG now = GetTickCount64();
                        ULONGLONG previous = lastKeyUnlock.load();
                        if (now - previous > 3000 &&
                            lastKeyUnlock.compare_exchange_strong(previous, now))
                        {
                            pendingKeyUnlock.store(now);
                            RecordLoginFlow(L"LastUnlockKeyRequestTick", GetTickCount());
                            events->CredentialsChanged(upAdviseContext);
                        }
                    }
                }
                keyWasDown = keyIsDown;
                if (++keySettingPolls < 30) continue;
                keySettingPolls = 0;
                DWORD refreshedKey = VK_RETURN;
                if (hasSid && IsSupportedLoginForUser(sid))
                    IsUnlockConditionMet(sid, &refreshedKey);
                if (refreshedKey != unlockKey)
                {
                    unlockKey = refreshedKey;
                    // A key already held when settings change must not count as a new press.
                    keyWasDown = (GetAsyncKeyState(unlockKey) & 0x8000) != 0;
                }
            }
            events->Release();
        }
        CoUninitialize();
    }).detach();
    return S_OK;
}

// Called by LogonUI when the ICredentialProviderEvents callback is no longer valid.
HRESULT CSampleProvider::UnAdvise()
{
    if (_stopProximityWatch) _stopProximityWatch->store(true);
    _stopProximityWatch.reset();
    return S_OK;
}

// Called by LogonUI to determine the number of fields in your tiles.  This
// does mean that all your tiles must have the same number of fields.
// This number must include both visible and invisible fields. If you want a tile
// to have different fields from the other tiles you enumerate for a given usage
// scenario you must include them all in this count and then hide/show them as desired
// using the field descriptors.
HRESULT CSampleProvider::GetFieldDescriptorCount(
    _Out_ DWORD *pdwCount)
{
    *pdwCount = SFI_NUM_FIELDS;
    return S_OK;
}

// Gets the field descriptor for a particular field.
HRESULT CSampleProvider::GetFieldDescriptorAt(
    DWORD dwIndex,
    _Outptr_result_nullonfailure_ CREDENTIAL_PROVIDER_FIELD_DESCRIPTOR **ppcpfd)
{
    HRESULT hr;
    *ppcpfd = nullptr;

    // Verify dwIndex is a valid field.
    if ((dwIndex < SFI_NUM_FIELDS) && ppcpfd)
    {
        hr = FieldDescriptorCoAllocCopy(s_rgCredProvFieldDescriptors[dwIndex], ppcpfd);
    }
    else
    {
        hr = E_INVALIDARG;
    }

    return hr;
}

// Sets pdwCount to the number of tiles that we wish to show at this time.
// Sets pdwDefault to the index of the tile which should be used as the default.
// The default tile is the tile which will be shown in the zoomed view by default. If
// more than one provider specifies a default the last used cred prov gets to pick
// the default. If *pbAutoLogonWithDefault is TRUE, LogonUI will immediately call
// GetSerialization on the credential you've specified as the default and will submit
// that credential for authentication without showing any further UI.
HRESULT CSampleProvider::GetCredentialCount(
    _Out_ DWORD *pdwCount,
    _Out_ DWORD *pdwDefault,
    _Out_ BOOL *pbAutoLogonWithDefault)
{
    *pdwDefault = CREDENTIAL_PROVIDER_NO_DEFAULT;
    *pbAutoLogonWithDefault = FALSE;

    if (_fRecreateEnumeratedCredentials)
    {
        _fRecreateEnumeratedCredentials = false;
        _ReleaseEnumeratedCredentials();
        _CreateEnumeratedCredentials();
    }

    const bool eligible = _pCredential != nullptr && _pCredential->IsAvailableForUnlock();
    *pdwCount = eligible ? 1 : 0;
    if (eligible)
    {
        *pdwDefault = 0;
        const ULONGLONG request = pendingKeyUnlock.exchange(0);
        if (request != 0 && GetTickCount64() - request < 5000)
        {
            *pbAutoLogonWithDefault = TRUE;
            RecordLoginFlow(L"LastUnlockKeyAutoSubmitTick", GetTickCount());
        }
    }
    RecordLoginFlow(L"LastAutoDefault", *pbAutoLogonWithDefault);
    RecordLoginFlow(L"LastAutoScenario", _cpus);

    return S_OK;
}

// Returns the credential at the index specified by dwIndex. This function is called by logonUI to enumerate
// the tiles.
HRESULT CSampleProvider::GetCredentialAt(
    DWORD dwIndex,
    _Outptr_result_nullonfailure_ ICredentialProviderCredential **ppcpc)
{
    HRESULT hr = E_INVALIDARG;
    *ppcpc = nullptr;

    if ((dwIndex == 0) && ppcpc && _pCredential != nullptr)
    {
        hr = _pCredential->QueryInterface(IID_PPV_ARGS(ppcpc));
    }
    return hr;
}

// This function will be called by LogonUI after SetUsageScenario succeeds.
// Sets the User Array with the list of users to be enumerated on the logon screen.
HRESULT CSampleProvider::SetUserArray(_In_ ICredentialProviderUserArray *users)
{
    if (_pCredProviderUserArray)
    {
        _pCredProviderUserArray->Release();
    }
    _pCredProviderUserArray = users;
    _pCredProviderUserArray->AddRef();
    return S_OK;
}

void CSampleProvider::_CreateEnumeratedCredentials()
{
    switch (_cpus)
    {
    case CPUS_LOGON:
    case CPUS_UNLOCK_WORKSTATION:
        {
            _EnumerateCredentials();
            break;
        }
    default:
        break;
    }
}

void CSampleProvider::_ReleaseEnumeratedCredentials()
{
    if (_pCredential != nullptr)
    {
        _pCredential->Release();
        _pCredential = nullptr;
    }
}

HRESULT CSampleProvider::_EnumerateCredentials()
{
    HRESULT hr = E_UNEXPECTED;
    if (_pCredProviderUserArray != nullptr)
    {
        DWORD dwUserCount;
        _pCredProviderUserArray->GetCount(&dwUserCount);
        if (dwUserCount > 0)
        {
            ICredentialProviderUser *pCredUser = nullptr;
            // Bind the credential to the configured account, not the first array entry.
            hr = HRESULT_FROM_WIN32(ERROR_NOT_FOUND);
            for (DWORD index = 0; index < dwUserCount; ++index)
            {
                ICredentialProviderUser* candidate = nullptr;
                if (FAILED(_pCredProviderUserArray->GetAt(index, &candidate))) continue;
                PWSTR sid = nullptr;
                const bool matches = SUCCEEDED(candidate->GetSid(&sid)) && IsSupportedLoginForUser(sid);
                CoTaskMemFree(sid);
                if (matches) { pCredUser = candidate; hr = S_OK; break; }
                candidate->Release();
            }
            if (SUCCEEDED(hr))
            {
                _pCredential = new(std::nothrow) CSampleCredential();
                if (_pCredential != nullptr)
                {
                    hr = _pCredential->Initialize(_cpus, s_rgCredProvFieldDescriptors, s_rgFieldStatePairs, pCredUser);
                    if (FAILED(hr))
                    {
                        _pCredential->Release();
                        _pCredential = nullptr;
                    }
                }
                else
                {
                    hr = E_OUTOFMEMORY;
                }
                pCredUser->Release();
            }
        }
    }
    return hr;
}

// Boilerplate code to create our provider.
HRESULT CSample_CreateInstance(_In_ REFIID riid, _Outptr_ void **ppv)
{
    HRESULT hr;
    CSampleProvider *pProvider = new(std::nothrow) CSampleProvider();
    if (pProvider)
    {
        hr = pProvider->QueryInterface(riid, ppv);
        pProvider->Release();
    }
    else
    {
        hr = E_OUTOFMEMORY;
    }
    return hr;
}
