#include "App.h"

DWORD ProfileRequest(ULONG Operation, const MK_PROFILE* Profile, MK_STATE* State, PCWSTR* Stage)
{
    *Stage = L"Opening service manager";
    SC_HANDLE manager = OpenSCManagerW(nullptr, nullptr, SC_MANAGER_CONNECT);
    if (!manager) return GetLastError();
    *Stage = L"Opening profile helper service";
    SC_HANDLE service = OpenServiceW(manager, MK_SERVICE_NAME, SERVICE_START | SERVICE_QUERY_STATUS);
    DWORD error = service ? ERROR_SUCCESS : GetLastError();
    CloseServiceHandle(manager);
    if (!service) return error;
    SERVICE_STATUS_PROCESS status = {};
    DWORD needed = 0;
    ULONGLONG deadline = GetTickCount64() + 8000;
    HANDLE pipe = INVALID_HANDLE_VALUE;
    while (GetTickCount64() < deadline) {
        *Stage = L"Reading helper service status";
        if (!QueryServiceStatusEx(service, SC_STATUS_PROCESS_INFO,
            reinterpret_cast<BYTE*>(&status), sizeof(status), &needed)) {
            error = GetLastError(); break;
        }
        if (status.dwCurrentState == SERVICE_STOPPED) {
            *Stage = L"Starting profile helper";
            if (!StartServiceW(service, 0, nullptr) && GetLastError() != ERROR_SERVICE_ALREADY_RUNNING) {
                error = GetLastError(); break;
            }
        } else if (status.dwCurrentState == SERVICE_RUNNING) {
            *Stage = L"Connecting to profile helper pipe";
            pipe = CreateFileW(MK_PIPE_NAME, FILE_READ_DATA | FILE_WRITE_DATA | FILE_READ_ATTRIBUTES | SYNCHRONIZE,
                0, nullptr, OPEN_EXISTING, FILE_FLAG_OVERLAPPED |
                SECURITY_SQOS_PRESENT | SECURITY_IDENTIFICATION, nullptr);
            if (pipe != INVALID_HANDLE_VALUE) {
                *Stage = L"Checking helper process identity";
                ULONG serverPid = 0;
                BOOL identified = GetNamedPipeServerProcessId(pipe, &serverPid);
                if (!identified || serverPid != status.dwProcessId || !serverPid) {
                    error = identified ? ERROR_ACCESS_DENIED : GetLastError();
                    CloseHandle(pipe); pipe = INVALID_HANDLE_VALUE;
                }
                break;
            }
            error = GetLastError();
            if (error != ERROR_PIPE_BUSY && error != ERROR_FILE_NOT_FOUND) break;
        }
        Sleep(100);
    }
    CloseServiceHandle(service);
    if (pipe == INVALID_HANDLE_VALUE) return error ? error : ERROR_TIMEOUT;
    MK_PIPE_REQUEST request = {};
    request.Magic = MK_PIPE_MAGIC;
    request.Operation = Operation;
    if (Profile) request.Profile = *Profile;
    MK_PIPE_RESPONSE response = {};
    *Stage = L"Sending profile request";
    error = PipeTransfer(pipe, &request, sizeof(request), true);
    if (!error) {
        *Stage = L"Receiving helper response";
        error = PipeTransfer(pipe, &response, sizeof(response), false);
    }
    if (!error) {
        UCHAR receipt = 1;
        (void)PipeTransfer(pipe, &receipt, sizeof(receipt), true);
        *Stage = L"Reading or saving driver profile";
        if (response.Magic != MK_PIPE_MAGIC) error = ERROR_INVALID_DATA;
        else error = response.Error;
        if (!error && (response.State.Profile.Version != MK_API_VERSION ||
            response.State.Profile.Count != MK_KEY_COUNT ||
            response.State.Profile.Mode > MK_MODE_STATIC ||
            response.State.Profile.Reserved)) error = ERROR_INVALID_DATA;
        if (!error && State) *State = response.State;
    }
    CloseHandle(pipe);
    return error;
}
