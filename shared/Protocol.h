#pragma once
// Include Windows/WDK types before this header. Fixed-size, pointer-free ABI.
#define MK_KEY_COUNT 104
#define MK_API_VERSION 1
#define MK_MODE_WINDOWS 0
#define MK_MODE_STATIC 1
#define MK_DEVICE_PATH L"\\\\.\\Mk4400LightingControl"
#define MK_SERVICE_NAME L"Mk4400Profile"
#define MK_PIPE_NAME L"\\\\.\\pipe\\Mk4400Profile.v1"
#define MK_PIPE_MAGIC 0x4d4b3434UL
#define MK_OP_GET 1
#define MK_OP_SET 2
#define IOCTL_MK_GET_STATE CTL_CODE(FILE_DEVICE_UNKNOWN, 0x800, METHOD_BUFFERED, FILE_READ_ACCESS)
#define IOCTL_MK_SET_PROFILE CTL_CODE(FILE_DEVICE_UNKNOWN, 0x801, METHOD_BUFFERED, FILE_WRITE_ACCESS)
#define IOCTL_MK_GET_DIAGNOSTICS CTL_CODE(FILE_DEVICE_UNKNOWN, 0x802, METHOD_BUFFERED, FILE_READ_ACCESS)

typedef struct _MK_COLOR {
    UCHAR R, G, B, Intensity;
} MK_COLOR;

typedef struct _MK_PROFILE {
    ULONG Version;
    ULONG Count;
    ULONG Mode;
    ULONG Reserved;
    MK_COLOR Colors[MK_KEY_COUNT];
} MK_PROFILE;

typedef struct _MK_STATE {
    MK_PROFILE Profile;
    LONG LastTransportStatus;
    ULONG Generation;
    ULONG AppliedGeneration;
    ULONG Powered;
} MK_STATE;

typedef struct _MK_DIAGNOSTICS {
    ULONG Version, Mode, Autonomous, MultiReports, RangeReports;
    UCHAR LastMulti[51];
    UCHAR LastRange[10];
    UCHAR Padding[3];
    MK_COLOR WindowsColors[MK_KEY_COUNT];
} MK_DIAGNOSTICS;

typedef struct _MK_PIPE_REQUEST {
    ULONG Magic;
    ULONG Operation;
    MK_PROFILE Profile;
} MK_PIPE_REQUEST;

typedef struct _MK_PIPE_RESPONSE {
    ULONG Magic;
    ULONG Error;
    MK_STATE State;
} MK_PIPE_RESPONSE;
