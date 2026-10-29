#pragma once
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#include <winioctl.h>
#include "../shared/Protocol.h"

DWORD RunProfileService();
DWORD ProfileRequest(ULONG Operation, const MK_PROFILE* Profile, MK_STATE* State, PCWSTR* Stage);
int RunEditor(HINSTANCE Instance, int Show);

// All local pipe operations are bounded and cancellation is drained before
// the stack-owned OVERLAPPED and payload can be destroyed.
inline DWORD FinishIo(HANDLE Handle, OVERLAPPED& Overlap, BOOL Immediate,
                       DWORD InitialError, HANDLE Stop, DWORD Timeout, DWORD* Bytes)
{
    if (!Immediate && InitialError != ERROR_IO_PENDING) return InitialError;
    if (!Immediate) {
        HANDLE events[2] = { Overlap.hEvent, Stop };
        DWORD wait = WaitForMultipleObjects(Stop ? 2 : 1, events, FALSE, Timeout);
        if (wait != WAIT_OBJECT_0) {
            CancelIoEx(Handle, &Overlap);
            DWORD ignored;
            GetOverlappedResult(Handle, &Overlap, &ignored, TRUE);
            return wait == WAIT_TIMEOUT ? ERROR_TIMEOUT : ERROR_OPERATION_ABORTED;
        }
    }
    return GetOverlappedResult(Handle, &Overlap, Bytes, FALSE) ? ERROR_SUCCESS : GetLastError();
}

inline DWORD PipeTransfer(HANDLE Pipe, void* Data, DWORD Size, bool Write, HANDLE Stop = nullptr)
{
    OVERLAPPED overlap = {};
    overlap.hEvent = CreateEventW(nullptr, TRUE, FALSE, nullptr);
    if (!overlap.hEvent) return GetLastError();
    DWORD bytes = 0;
    BOOL immediate = Write ? WriteFile(Pipe, Data, Size, nullptr, &overlap)
                           : ReadFile(Pipe, Data, Size, nullptr, &overlap);
    DWORD error = FinishIo(Pipe, overlap, immediate, immediate ? 0 : GetLastError(),
                           Stop, 5000, &bytes);
    CloseHandle(overlap.hEvent);
    if (!error && bytes != Size) error = ERROR_INVALID_DATA;
    return error;
}
