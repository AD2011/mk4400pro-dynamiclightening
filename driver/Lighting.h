#pragma once
#include <ntddk.h>
#include <wdf.h>
#include <hidport.h>
#include <vhf.h>
#include "../shared/Protocol.h"

#define MK_TAG '44kM'
#define MK_INTERVAL_US 250000UL

typedef struct _DEVICE_CONTEXT {
    WDFDEVICE Device;
    VHFHANDLE Vhf;
    WDFSPINLOCK Lock;
    WDFWORKITEM Work;
    WDFTIMER RetryTimer;
    PVOID Notification;
    MK_COLOR Staged[MK_KEY_COUNT];
    MK_COLOR Committed[MK_KEY_COUNT];
    MK_PROFILE Profile;
    MK_DIAGNOSTICS Diagnostics;
    USHORT RequestedLamp;
    ULONG Generation;
    ULONG AppliedGeneration;
    ULONG RetryCount;
    ULONGLONG NextUpdateTime;
    NTSTATUS LastTransportStatus;
    BOOLEAN Autonomous;
    BOOLEAN Powered;
    BOOLEAN Queued;
    // Only the passive worker and the drained D0Exit path access Owned.
    BOOLEAN Owned;
} DEVICE_CONTEXT;
WDF_DECLARE_CONTEXT_TYPE_WITH_NAME(DEVICE_CONTEXT, DeviceContext)

DRIVER_INITIALIZE DriverEntry;
EVT_WDF_DRIVER_DEVICE_ADD MkDeviceAdd;
EVT_WDF_DEVICE_D0_ENTRY MkD0Entry;
EVT_WDF_DEVICE_D0_EXIT MkD0Exit;
EVT_WDF_OBJECT_CONTEXT_CLEANUP MkCleanup;
EVT_WDF_WORKITEM MkWork;
EVT_WDF_TIMER MkRetry;
EVT_VHF_ASYNC_OPERATION MkGetFeature;
EVT_VHF_ASYNC_OPERATION MkSetFeature;

// Caller holds Lock. Schedules at most one passive work item.
VOID MkQueueLocked(DEVICE_CONTEXT* Context);
NTSTATUS MkApplyColors(DEVICE_CONTEXT* Context, const MK_COLOR* Colors);
NTSTATUS MkReleaseKeyboard(DEVICE_CONTEXT* Context);
BOOLEAN MkIsPhysicalInterface(PCUNICODE_STRING Name);
NTSTATUS MkCreateProfileQueue(DEVICE_CONTEXT* Context);
VOID MkLoadProfile(DEVICE_CONTEXT* Context);
