#include "App.h"
#include <sddl.h>

static SERVICE_STATUS_HANDLE StatusHandle;
static HANDLE StopEvent;
static DWORD Checkpoint;

static void ReportStatus(DWORD State, DWORD Error = NO_ERROR)
{
    SERVICE_STATUS status = {};
    status.dwServiceType = SERVICE_WIN32_OWN_PROCESS;
    status.dwCurrentState = State;
    status.dwWin32ExitCode = Error;
    status.dwControlsAccepted = State == SERVICE_RUNNING ? SERVICE_ACCEPT_STOP | SERVICE_ACCEPT_SHUTDOWN : 0;
    if (State == SERVICE_START_PENDING || State == SERVICE_STOP_PENDING) {
        status.dwCheckPoint = ++Checkpoint;
        status.dwWaitHint = 10000;
    }
    SetServiceStatus(StatusHandle, &status);
}

static DWORD WINAPI ServiceControl(DWORD Control, DWORD, void*, void*)
{
    if (Control == SERVICE_CONTROL_STOP || Control == SERVICE_CONTROL_SHUTDOWN) {
        ReportStatus(SERVICE_STOP_PENDING);
        SetEvent(StopEvent);
    }
    return NO_ERROR;
}

static DWORD DriverIoctl(HANDLE Driver, DWORD Code, void* Input, DWORD InputSize,
                         void* Output, DWORD OutputSize)
{
    OVERLAPPED overlap = {};
    overlap.hEvent = CreateEventW(nullptr, TRUE, FALSE, nullptr);
    if (!overlap.hEvent) return GetLastError();
    DWORD bytes = 0;
    BOOL immediate = DeviceIoControl(Driver, Code, Input, InputSize, Output, OutputSize,
                                     nullptr, &overlap);
    DWORD error = FinishIo(Driver, overlap, immediate, immediate ? 0 : GetLastError(),
                           StopEvent, 5000, &bytes);
    if (!error && OutputSize && bytes != OutputSize) error = ERROR_INVALID_DATA;
    CloseHandle(overlap.hEvent);
    return error;
}

static void HandleRequest(const MK_PIPE_REQUEST& Request, MK_PIPE_RESPONSE& Response)
{
    Response.Magic = MK_PIPE_MAGIC;
    if (Request.Magic != MK_PIPE_MAGIC ||
        (Request.Operation != MK_OP_GET && Request.Operation != MK_OP_SET)) {
        Response.Error = ERROR_INVALID_PARAMETER;
        return;
    }
    if (Request.Operation == MK_OP_SET &&
        (Request.Profile.Version != MK_API_VERSION || Request.Profile.Count != MK_KEY_COUNT ||
         Request.Profile.Mode > MK_MODE_STATIC || Request.Profile.Reserved)) {
        Response.Error = ERROR_INVALID_PARAMETER;
        return;
    }
    HANDLE driver = CreateFileW(MK_DEVICE_PATH, GENERIC_READ | GENERIC_WRITE,
        FILE_SHARE_READ | FILE_SHARE_WRITE, nullptr, OPEN_EXISTING, FILE_FLAG_OVERLAPPED, nullptr);
    if (driver == INVALID_HANDLE_VALUE) { Response.Error = GetLastError(); return; }
    if (Request.Operation == MK_OP_SET) {
        MK_PROFILE profile = Request.Profile;
        Response.Error = DriverIoctl(driver, IOCTL_MK_SET_PROFILE,
            &profile, sizeof(profile), nullptr, 0);
    }
    if (!Response.Error) {
        Response.Error = DriverIoctl(driver, IOCTL_MK_GET_STATE,
            nullptr, 0, &Response.State, sizeof(Response.State));
    }
    CloseHandle(driver);
}

static void WINAPI ServiceMain(DWORD, PWSTR*)
{
    StatusHandle = RegisterServiceCtrlHandlerExW(MK_SERVICE_NAME, ServiceControl, nullptr);
    if (!StatusHandle) return;
    ReportStatus(SERVICE_START_PENDING);
    StopEvent = CreateEventW(nullptr, TRUE, FALSE, nullptr);
    if (!StopEvent) { ReportStatus(SERVICE_STOPPED, GetLastError()); return; }

    PSECURITY_DESCRIPTOR descriptor = nullptr;
    // Interactive local users can exchange messages, but cannot create extra
    // server instances. In particular, do not grant FILE_CREATE_PIPE_INSTANCE.
    // Include FILE_READ_ATTRIBUTES: the Windows client open also requires it.
    // Omitting it made normal desktop clients fail with ERROR_ACCESS_DENIED.
    if (!ConvertStringSecurityDescriptorToSecurityDescriptorW(
        L"D:P(A;;GA;;;SY)(A;;GA;;;BA)(A;;GA;;;LS)(A;;0x00100083;;;IU)",
        SDDL_REVISION_1, &descriptor, nullptr)) {
        ReportStatus(SERVICE_STOPPED, GetLastError());
        CloseHandle(StopEvent);
        return;
    }
    SECURITY_ATTRIBUTES security = { sizeof(security), descriptor, FALSE };
    DWORD failure = ERROR_SUCCESS;
    ReportStatus(SERVICE_RUNNING);
    while (WaitForSingleObject(StopEvent, 0) == WAIT_TIMEOUT) {
        HANDLE pipe = CreateNamedPipeW(MK_PIPE_NAME,
            PIPE_ACCESS_DUPLEX | FILE_FLAG_OVERLAPPED | FILE_FLAG_FIRST_PIPE_INSTANCE,
            PIPE_TYPE_MESSAGE | PIPE_READMODE_MESSAGE | PIPE_WAIT | PIPE_REJECT_REMOTE_CLIENTS,
            1, sizeof(MK_PIPE_RESPONSE), sizeof(MK_PIPE_REQUEST), 5000, &security);
        if (pipe == INVALID_HANDLE_VALUE) { failure = GetLastError(); break; }
        OVERLAPPED connect = {};
        connect.hEvent = CreateEventW(nullptr, TRUE, FALSE, nullptr);
        if (!connect.hEvent) { failure = GetLastError(); CloseHandle(pipe); break; }
        BOOL immediate = ConnectNamedPipe(pipe, &connect);
        DWORD error = immediate ? ERROR_SUCCESS : GetLastError();
        DWORD ignored = 0;
        if (error != ERROR_PIPE_CONNECTED) {
            error = FinishIo(pipe, connect, immediate, error, StopEvent, 15000, &ignored);
        } else {
            error = ERROR_SUCCESS;
        }
        CloseHandle(connect.hEvent);
        if (error) {
            CloseHandle(pipe);
            // No editor requests for 15 seconds: leave no resident helper.
            if (error != ERROR_TIMEOUT && error != ERROR_OPERATION_ABORTED) failure = error;
            break;
        }
        MK_PIPE_REQUEST request = {};
        MK_PIPE_RESPONSE response = {};
        error = PipeTransfer(pipe, &request, sizeof(request), false, StopEvent);
        if (!error) {
            HandleRequest(request, response);
            (void)PipeTransfer(pipe, &response, sizeof(response), true, StopEvent);
            // Wait for a one-byte receipt so DisconnectNamedPipe cannot discard
            // the reply. A broken client cannot hold the service indefinitely.
            UCHAR receipt = 0;
            (void)PipeTransfer(pipe, &receipt, sizeof(receipt), false, StopEvent);
        }
        DisconnectNamedPipe(pipe);
        CloseHandle(pipe);
    }
    LocalFree(descriptor);
    ReportStatus(SERVICE_STOP_PENDING);
    CloseHandle(StopEvent);
    StopEvent = nullptr;
    ReportStatus(SERVICE_STOPPED, failure);
}

DWORD RunProfileService()
{
    SERVICE_TABLE_ENTRYW table[] = {
        { const_cast<PWSTR>(MK_SERVICE_NAME), ServiceMain }, { nullptr, nullptr }
    };
    return StartServiceCtrlDispatcherW(table) ? ERROR_SUCCESS : GetLastError();
}
