#include "Lighting.h"
#include "KeyLayout.h"

// Explicit wire offsets avoid compiler packing/alignment dependencies.
static USHORT Read16(const UCHAR* p)
{
    return (USHORT)(p[0] | ((USHORT)p[1] << 8));
}

static VOID Write16(UCHAR* p, USHORT v)
{
    p[0] = (UCHAR)v;
    p[1] = (UCHAR)(v >> 8);
}

static VOID Write32(UCHAR* p, ULONG v)
{
    p[0] = (UCHAR)v;
    p[1] = (UCHAR)(v >> 8);
    p[2] = (UCHAR)(v >> 16);
    p[3] = (UCHAR)(v >> 24);
}

VOID MkGetFeature(PVOID Client, VHFOPERATIONHANDLE Operation,
                  PVOID OperationContext, PHID_XFER_PACKET Packet)
{
    NTSTATUS status = STATUS_NOT_SUPPORTED;
    UCHAR* p = Packet->reportBuffer;
    ULONG required = Packet->reportId == 1 ? 23 : 29;
    DEVICE_CONTEXT* ctx = (DEVICE_CONTEXT*)Client;
    USHORT lamp;
    UNREFERENCED_PARAMETER(OperationContext);

    if (Packet->reportId == 1 || Packet->reportId == 3) {
        if (p == NULL || Packet->reportBufferLen < required) {
            status = STATUS_BUFFER_TOO_SMALL;
        } else {
            RtlZeroMemory(p, Packet->reportBufferLen);
            p[0] = Packet->reportId;
            if (Packet->reportId == 1) {
                Write16(p + 1, MK_KEY_COUNT);
                // Nominal envelope, not measured physical key coordinates.
                Write32(p + 3, 460000);
                Write32(p + 7, 150000);
                Write32(p + 11, 30000);
                Write32(p + 15, 1);            // LampArrayKind: keyboard.
                Write32(p + 19, MK_INTERVAL_US);
            } else {
                WdfSpinLockAcquire(ctx->Lock);
                lamp = ctx->RequestedLamp;
                ctx->RequestedLamp = (USHORT)((lamp + 1) % MK_KEY_COUNT);
                WdfSpinLockRelease(ctx->Lock);
                Write16(p + 1, lamp);
                Write32(p + 3, MkKeys[lamp].X);
                Write32(p + 7, MkKeys[lamp].Y);
                Write32(p + 11, 15000);
                Write32(p + 15, MK_INTERVAL_US);
                Write32(p + 19, 1);            // LampPurpose: control.
                p[23] = p[24] = p[25] = p[26] = 255;
                p[27] = 1;                     // Programmable.
                p[28] = MkKeys[lamp].Usage;     // Fn has no standard usage.
            }
            status = STATUS_SUCCESS;
        }
    }
    (VOID)VhfAsyncOperationComplete(Operation, status);
}

VOID MkSetFeature(PVOID Client, VHFOPERATIONHANDLE Operation,
                  PVOID OperationContext, PHID_XFER_PACKET Packet)
{
    DEVICE_CONTEXT* ctx = (DEVICE_CONTEXT*)Client;
    NTSTATUS status = STATUS_SUCCESS;
    UCHAR* p = Packet->reportBuffer;
    ULONG required;
    BOOLEAN commit = FALSE;
    BOOLEAN changed = FALSE;
    ULONG i;
    UNREFERENCED_PARAMETER(OperationContext);

    switch (Packet->reportId) {
    case 2: required = 3; break;
    case 4: required = 9; break;
    case 5: required = 10; break;
    case 6: required = 2; break;
    default:
        (VOID)VhfAsyncOperationComplete(Operation, STATUS_NOT_SUPPORTED);
        return;
    }
    if (p == NULL || Packet->reportBufferLen < required) {
        (VOID)VhfAsyncOperationComplete(Operation, STATUS_BUFFER_TOO_SMALL);
        return;
    }
    if (p[0] != Packet->reportId ||
        (Packet->reportId == 4 && (p[1] > 1 || (p[2] & ~1u))) ||
        (Packet->reportId == 5 && ((p[1] & ~1u) || Read16(p + 2) > Read16(p + 4))) ||
        (Packet->reportId == 6 && p[1] > 1)) {
        (VOID)VhfAsyncOperationComplete(Operation, STATUS_INVALID_PARAMETER);
        return;
    }

    WdfSpinLockAcquire(ctx->Lock);
    switch (Packet->reportId) {
    case 2:
        ctx->RequestedLamp = Read16(p + 1);
        if (ctx->RequestedLamp >= MK_KEY_COUNT) ctx->RequestedLamp = 0;
        break;
    case 4:
        ++ctx->Diagnostics.MultiReports;
        RtlZeroMemory(ctx->Diagnostics.LastMulti, sizeof(ctx->Diagnostics.LastMulti));
        RtlCopyMemory(ctx->Diagnostics.LastMulti, p, required);
        if (!ctx->Autonomous) {
            for (i = 0; i < p[1]; ++i) {
                USHORT lamp = Read16(p + 3 + 2 * i);
                if (lamp < MK_KEY_COUNT) {
                    RtlCopyMemory(&ctx->Staged[lamp], p + 5, sizeof(MK_COLOR));
                }
            }
            commit = (BOOLEAN)(p[2] & 1);
        }
        break;
    case 5:
        ++ctx->Diagnostics.RangeReports;
        RtlCopyMemory(ctx->Diagnostics.LastRange, p, 10);
        if (!ctx->Autonomous) {
            for (i = Read16(p + 2); i <= Read16(p + 4) && i < MK_KEY_COUNT; ++i) {
                RtlCopyMemory(&ctx->Staged[i], p + 6, sizeof(MK_COLOR));
            }
            commit = (BOOLEAN)(p[1] & 1);
        }
        break;
    case 6:
        if (ctx->Autonomous != (BOOLEAN)p[1]) {
            ctx->Autonomous = (BOOLEAN)p[1];
            // Discard an unfinished update when ownership changes.
            RtlCopyMemory(ctx->Staged, ctx->Committed, sizeof(ctx->Staged));
            changed = TRUE;
        }
        break;
    }
    if (commit &&
        RtlCompareMemory(ctx->Staged, ctx->Committed, sizeof(ctx->Staged)) != sizeof(ctx->Staged)) {
        RtlCopyMemory(ctx->Committed, ctx->Staged, sizeof(ctx->Committed));
        changed = TRUE;
    }
    // Preserve Windows' latest state, but it cannot override the saved mode.
    if (changed && ctx->Profile.Mode == MK_MODE_WINDOWS) {
        ++ctx->Generation;
        ctx->RetryCount = 0;
        MkQueueLocked(ctx);
    }
    WdfSpinLockRelease(ctx->Lock);
    // Acknowledges acceptance into the latest-color queue. Physical delivery
    // runs at PASSIVE_LEVEL; failures are retained in LastTransportStatus.
    (VOID)VhfAsyncOperationComplete(Operation, status);
}
