#include "Lighting.h"

EVT_WDF_IO_QUEUE_IO_DEVICE_CONTROL MkProfileIoctl;

static BOOLEAN ValidProfile(const MK_PROFILE* Profile)
{
    return Profile->Version == MK_API_VERSION &&
           Profile->Count == MK_KEY_COUNT &&
           Profile->Mode <= MK_MODE_STATIC && Profile->Reserved == 0;
}

VOID MkLoadProfile(DEVICE_CONTEXT* Context)
{
    WDFKEY key;
    MK_PROFILE profile;
    ULONG length = 0, type = 0, i;
    NTSTATUS status;
    DECLARE_CONST_UNICODE_STRING(valueName, L"StaticProfileV1");
    Context->Profile.Version = MK_API_VERSION;
    Context->Profile.Count = MK_KEY_COUNT;
    Context->Profile.Mode = MK_MODE_WINDOWS;
    for (i = 0; i < MK_KEY_COUNT; ++i) {
        Context->Profile.Colors[i].R = Context->Profile.Colors[i].G =
            Context->Profile.Colors[i].B = Context->Profile.Colors[i].Intensity = 255;
    }
    status = WdfDeviceOpenRegistryKey(Context->Device, PLUGPLAY_REGKEY_DEVICE,
        KEY_QUERY_VALUE, WDF_NO_OBJECT_ATTRIBUTES, &key);
    if (!NT_SUCCESS(status)) return;
    RtlZeroMemory(&profile, sizeof(profile));
    status = WdfRegistryQueryValue(key, &valueName, sizeof(profile), &profile, &length, &type);
    WdfRegistryClose(key);
    if (NT_SUCCESS(status) && type == REG_BINARY && length == sizeof(profile) && ValidProfile(&profile))
        Context->Profile = profile;
}

NTSTATUS MkCreateProfileQueue(DEVICE_CONTEXT* Context)
{
    WDF_IO_QUEUE_CONFIG config;
    WDF_OBJECT_ATTRIBUTES attributes;
    WDF_IO_QUEUE_CONFIG_INIT_DEFAULT_QUEUE(&config, WdfIoQueueDispatchSequential);
    config.EvtIoDeviceControl = MkProfileIoctl;
    WDF_OBJECT_ATTRIBUTES_INIT(&attributes);
    attributes.ExecutionLevel = WdfExecutionLevelPassive;
    return WdfIoQueueCreate(Context->Device, &config, &attributes, WDF_NO_HANDLE);
}

VOID MkProfileIoctl(WDFQUEUE Queue, WDFREQUEST Request, size_t OutputSize,
                    size_t InputSize, ULONG Code)
{
    DEVICE_CONTEXT* ctx = DeviceContext(WdfIoQueueGetDevice(Queue));
    NTSTATUS status = STATUS_INVALID_DEVICE_REQUEST;
    ULONG_PTR information = 0;
    MK_STATE* state;
    MK_PROFILE* input;
    MK_PROFILE profile;
    WDFKEY key;
    DECLARE_CONST_UNICODE_STRING(valueName, L"StaticProfileV1");

    if (Code == IOCTL_MK_GET_DIAGNOSTICS) {
        MK_DIAGNOSTICS* diagnostics;
        status = WdfRequestRetrieveOutputBuffer(Request, sizeof(MK_DIAGNOSTICS),
            (PVOID*)&diagnostics, NULL);
        if (NT_SUCCESS(status)) {
            WdfSpinLockAcquire(ctx->Lock);
            *diagnostics = ctx->Diagnostics;
            diagnostics->Version = 1;
            diagnostics->Mode = ctx->Profile.Mode;
            diagnostics->Autonomous = ctx->Autonomous;
            RtlCopyMemory(diagnostics->WindowsColors, ctx->Committed, sizeof(ctx->Committed));
            WdfSpinLockRelease(ctx->Lock);
            information = sizeof(*diagnostics);
        }
    } else if (Code == IOCTL_MK_GET_STATE) {
        if (OutputSize < sizeof(MK_STATE)) {
            status = STATUS_BUFFER_TOO_SMALL;
        } else {
            status = WdfRequestRetrieveOutputBuffer(Request, sizeof(MK_STATE), (PVOID*)&state, NULL);
            if (NT_SUCCESS(status)) {
                RtlZeroMemory(state, sizeof(*state));
                WdfSpinLockAcquire(ctx->Lock);
                state->Profile = ctx->Profile;
                state->LastTransportStatus = ctx->LastTransportStatus;
                state->Generation = ctx->Generation;
                state->AppliedGeneration = ctx->AppliedGeneration;
                state->Powered = ctx->Powered;
                WdfSpinLockRelease(ctx->Lock);
                information = sizeof(*state);
            }
        }
    } else if (Code == IOCTL_MK_SET_PROFILE) {
        if (InputSize != sizeof(MK_PROFILE)) {
            status = STATUS_INFO_LENGTH_MISMATCH;
        } else {
            status = WdfRequestRetrieveInputBuffer(Request, sizeof(MK_PROFILE), (PVOID*)&input, NULL);
            if (NT_SUCCESS(status)) {
                profile = *input;
                if (!ValidProfile(&profile)) {
                    status = STATUS_INVALID_PARAMETER;
                } else {
                    // One bounded binary value, written only on explicit Save.
                    // Persist first; report failure rather than claim an unsaved profile.
                    status = WdfDeviceOpenRegistryKey(ctx->Device, PLUGPLAY_REGKEY_DEVICE,
                        KEY_SET_VALUE, WDF_NO_OBJECT_ATTRIBUTES, &key);
                    if (NT_SUCCESS(status)) {
                        status = WdfRegistryAssignValue(key, &valueName, REG_BINARY,
                            sizeof(profile), &profile);
                        WdfRegistryClose(key);
                    }
                    if (NT_SUCCESS(status)) {
                        WdfSpinLockAcquire(ctx->Lock);
                        ctx->Profile = profile;
                        ++ctx->Generation;
                        ctx->RetryCount = 0;
                        MkQueueLocked(ctx);
                        WdfSpinLockRelease(ctx->Lock);
                    }
                }
            }
        }
    }
    WdfRequestCompleteWithInformation(Request, status, information);
}
