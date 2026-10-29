#include <initguid.h>
#include "Lighting.h"
#include <wdmguid.h>
#include "LampArrayDescriptor.h"

DRIVER_NOTIFICATION_CALLBACK_ROUTINE MkInterfaceChange;

NTSTATUS DriverEntry(PDRIVER_OBJECT DriverObject, PUNICODE_STRING RegistryPath)
{
    WDF_DRIVER_CONFIG config;
    WDF_DRIVER_CONFIG_INIT(&config, MkDeviceAdd);
    config.DriverPoolTag = MK_TAG;
    return WdfDriverCreate(DriverObject, RegistryPath, WDF_NO_OBJECT_ATTRIBUTES,
                          &config, WDF_NO_HANDLE);
}

VOID MkQueueLocked(DEVICE_CONTEXT* Context)
{
    if (Context->Powered && !Context->Queued) {
        ULONGLONG now = KeQueryInterruptTime();
        if ((!Context->Autonomous || Context->Profile.Mode == MK_MODE_STATIC) &&
            now < Context->NextUpdateTime) {
            ULONG delayMs = (ULONG)((Context->NextUpdateTime - now + 9999) / 10000);
            (VOID)WdfTimerStart(Context->RetryTimer, WDF_REL_TIMEOUT_IN_MS(delayMs));
            return;
        }
        // Supersede an old retry when a fresh request arrives.
        (VOID)WdfTimerStop(Context->RetryTimer, FALSE);
        Context->Queued = TRUE;
        WdfWorkItemEnqueue(Context->Work);
    }
}

NTSTATUS MkInterfaceChange(PVOID NotificationStructure, PVOID Context)
{
    DEVICE_CONTEXT* ctx = (DEVICE_CONTEXT*)Context;
    PDEVICE_INTERFACE_CHANGE_NOTIFICATION notice =
        (PDEVICE_INTERFACE_CHANGE_NOTIFICATION)NotificationStructure;
    if (IsEqualGUID(&notice->Event, &GUID_DEVICE_INTERFACE_ARRIVAL) &&
        MkIsPhysicalInterface(notice->SymbolicLinkName)) {
        WdfSpinLockAcquire(ctx->Lock);
        if (!ctx->Autonomous || ctx->Profile.Mode == MK_MODE_STATIC) {
            ++ctx->Generation;
            ctx->RetryCount = 0;
            MkQueueLocked(ctx);
        }
        WdfSpinLockRelease(ctx->Lock);
    }
    return STATUS_SUCCESS;
}

NTSTATUS MkDeviceAdd(WDFDRIVER Driver, PWDFDEVICE_INIT DeviceInit)
{
    WDFDEVICE device;
    DEVICE_CONTEXT* ctx;
    WDF_OBJECT_ATTRIBUTES attributes;
    WDF_PNPPOWER_EVENT_CALLBACKS power;
    WDF_WORKITEM_CONFIG work;
    WDF_TIMER_CONFIG timer;
    VHF_CONFIG vhf;
    NTSTATUS status;
    ULONG i;
    DECLARE_CONST_UNICODE_STRING(deviceName, L"\\Device\\Mk4400LightingControl");
    DECLARE_CONST_UNICODE_STRING(symbolicName, L"\\DosDevices\\Global\\Mk4400LightingControl");
    DECLARE_CONST_UNICODE_STRING(sddl, L"D:P(A;;GA;;;SY)(A;;GA;;;BA)(A;;GRGW;;;LS)");

    WdfDeviceInitSetDeviceType(DeviceInit, FILE_DEVICE_UNKNOWN);
    WdfDeviceInitSetCharacteristics(DeviceInit, FILE_DEVICE_SECURE_OPEN, FALSE);
    status = WdfDeviceInitAssignName(DeviceInit, &deviceName);
    if (!NT_SUCCESS(status)) return status;
    status = WdfDeviceInitAssignSDDLString(DeviceInit, &sddl);
    if (!NT_SUCCESS(status)) return status;
    WDF_PNPPOWER_EVENT_CALLBACKS_INIT(&power);
    power.EvtDeviceD0Entry = MkD0Entry;
    power.EvtDeviceD0Exit = MkD0Exit;
    WdfDeviceInitSetPnpPowerEventCallbacks(DeviceInit, &power);
    WDF_OBJECT_ATTRIBUTES_INIT_CONTEXT_TYPE(&attributes, DEVICE_CONTEXT);
    attributes.EvtCleanupCallback = MkCleanup;
    attributes.ExecutionLevel = WdfExecutionLevelPassive;
    status = WdfDeviceCreate(&DeviceInit, &attributes, &device);
    if (!NT_SUCCESS(status)) return status;
    ctx = DeviceContext(device);
    ctx->Device = device;
    ctx->Autonomous = TRUE;
    for (i = 0; i < MK_KEY_COUNT; ++i)
        ctx->Committed[i].Intensity = ctx->Staged[i].Intensity = 255;
    ctx->LastTransportStatus = STATUS_DEVICE_NOT_CONNECTED;

    WDF_OBJECT_ATTRIBUTES_INIT(&attributes);
    attributes.ParentObject = device;
    status = WdfSpinLockCreate(&attributes, &ctx->Lock);
    if (!NT_SUCCESS(status)) return status;
    WDF_WORKITEM_CONFIG_INIT(&work, MkWork);
    work.AutomaticSerialization = FALSE;
    status = WdfWorkItemCreate(&work, &attributes, &ctx->Work);
    if (!NT_SUCCESS(status)) return status;
    WDF_TIMER_CONFIG_INIT(&timer, MkRetry);
    timer.AutomaticSerialization = FALSE;
    status = WdfTimerCreate(&timer, &attributes, &ctx->RetryTimer);
    if (!NT_SUCCESS(status)) return status;

    MkLoadProfile(ctx);
    status = MkCreateProfileQueue(ctx);
    if (!NT_SUCCESS(status)) return status;
    status = WdfDeviceCreateSymbolicLink(device, &symbolicName);
    if (!NT_SUCCESS(status)) return status;
    VHF_CONFIG_INIT(&vhf, WdfDeviceWdmGetDeviceObject(device),
                    (USHORT)sizeof(LampArrayReportDescriptor), LampArrayReportDescriptor);
    vhf.VhfClientContext = ctx;
    // Software identity; never borrow a physical USB VID/PID for this child.
    vhf.VersionNumber = 3;
    vhf.EvtVhfAsyncOperationGetFeature = MkGetFeature;
    vhf.EvtVhfAsyncOperationSetFeature = MkSetFeature;
    status = VhfCreate(&vhf, &ctx->Vhf);
    if (!NT_SUCCESS(status)) return status;
    status = VhfStart(ctx->Vhf);
    if (!NT_SUCCESS(status)) return status;
    status = IoRegisterPlugPlayNotification(EventCategoryDeviceInterfaceChange,
                0, (PVOID)&GUID_DEVINTERFACE_HID,
                WdfDriverWdmGetDriverObject(Driver), MkInterfaceChange,
                ctx, &ctx->Notification);
    return status;
}

NTSTATUS MkD0Entry(WDFDEVICE Device, WDF_POWER_DEVICE_STATE PreviousState)
{
    DEVICE_CONTEXT* ctx = DeviceContext(Device);
    UNREFERENCED_PARAMETER(PreviousState);
    WdfSpinLockAcquire(ctx->Lock);
    ctx->Powered = TRUE;
    ctx->RetryCount = 0;
    ++ctx->Generation;
    if (!ctx->Autonomous || ctx->Profile.Mode == MK_MODE_STATIC) MkQueueLocked(ctx);
    WdfSpinLockRelease(ctx->Lock);
    return STATUS_SUCCESS;
}

NTSTATUS MkD0Exit(WDFDEVICE Device, WDF_POWER_DEVICE_STATE TargetState)
{
    DEVICE_CONTEXT* ctx = DeviceContext(Device);
    UNREFERENCED_PARAMETER(TargetState);
    WdfSpinLockAcquire(ctx->Lock);
    ctx->Powered = FALSE;
    WdfSpinLockRelease(ctx->Lock);
    (VOID)WdfTimerStop(ctx->RetryTimer, TRUE);
    WdfWorkItemFlush(ctx->Work);
    // The physical device can already be gone during surprise removal.
    ctx->LastTransportStatus = MkReleaseKeyboard(ctx);
    return STATUS_SUCCESS;
}

VOID MkCleanup(WDFOBJECT Object)
{
    DEVICE_CONTEXT* ctx = DeviceContext(Object);
    if (ctx->Notification != NULL) {
        (VOID)IoUnregisterPlugPlayNotificationEx(ctx->Notification);
        ctx->Notification = NULL;
    }
    // Stops and drains VHF callbacks before child synchronization objects die.
    if (ctx->Vhf != NULL) {
        VhfDelete(ctx->Vhf, TRUE);
        ctx->Vhf = NULL;
    }
    if (ctx->Lock != NULL) {
        WdfSpinLockAcquire(ctx->Lock);
        ctx->Powered = FALSE;
        WdfSpinLockRelease(ctx->Lock);
    }
    if (ctx->RetryTimer != NULL) (VOID)WdfTimerStop(ctx->RetryTimer, TRUE);
    if (ctx->Work != NULL) WdfWorkItemFlush(ctx->Work);
}

VOID MkRetry(WDFTIMER Timer)
{
    DEVICE_CONTEXT* ctx = DeviceContext(WdfTimerGetParentObject(Timer));
    WdfSpinLockAcquire(ctx->Lock);
    MkQueueLocked(ctx);
    WdfSpinLockRelease(ctx->Lock);
}

VOID MkWork(WDFWORKITEM WorkItem)
{
    DEVICE_CONTEXT* ctx = DeviceContext(WdfWorkItemGetParentObject(WorkItem));
    MK_COLOR colors[MK_KEY_COUNT];
    ULONG generation;
    BOOLEAN autonomous;
    NTSTATUS status;
    WdfSpinLockAcquire(ctx->Lock);
    if (!ctx->Powered) {
        ctx->Queued = FALSE;
        WdfSpinLockRelease(ctx->Lock);
        return;
    }
    generation = ctx->Generation;
    RtlCopyMemory(colors, ctx->Profile.Mode == MK_MODE_STATIC
        ? ctx->Profile.Colors : ctx->Committed, sizeof(colors));
    autonomous = ctx->Autonomous && ctx->Profile.Mode != MK_MODE_STATIC;
    ctx->NextUpdateTime = KeQueryInterruptTime() + (ULONGLONG)MK_INTERVAL_US * 10;
    WdfSpinLockRelease(ctx->Lock);

    status = autonomous ? MkReleaseKeyboard(ctx) : MkApplyColors(ctx, colors);
    WdfSpinLockAcquire(ctx->Lock);
    ctx->LastTransportStatus = status;
    if (NT_SUCCESS(status)) ctx->AppliedGeneration = generation;
    ctx->Queued = FALSE;
    if (ctx->Powered) {
        if (ctx->Generation != generation) {
            (VOID)WdfTimerStart(ctx->RetryTimer, WDF_REL_TIMEOUT_IN_MS(250));
        } else if (!NT_SUCCESS(status) && ctx->RetryCount < 3) {
            ++ctx->RetryCount;
            (VOID)WdfTimerStart(ctx->RetryTimer, WDF_REL_TIMEOUT_IN_MS(2000));
        }
    }
    WdfSpinLockRelease(ctx->Lock);
    if (!NT_SUCCESS(status)) {
        KdPrintEx((DPFLTR_IHVDRIVER_ID, DPFLTR_WARNING_LEVEL,
                   "Mk4400Lighting: transport status 0x%08X\n", status));
    }
}
