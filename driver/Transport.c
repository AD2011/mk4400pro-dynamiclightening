#include "Lighting.h"
#include <hidpddi.h>
#include "ActiveSlots.h"
C_ASSERT(RTL_NUMBER_OF(ActiveSlots) == MK_KEY_COUNT);
C_ASSERT(sizeof(MK_PROFILE) == 432);
C_ASSERT(sizeof(MK_STATE) == 448);

#define MK_REPORT_BYTES 65
#define MK_FRAME_BYTES 436
#define MK_CHUNK_BYTES 56

BOOLEAN MkIsPhysicalInterface(PCUNICODE_STRING Name)
{
    static const WCHAR needle[] = L"vid_5566&pid_000a&mi_02";
    SIZE_T i, j;
    SIZE_T count;
    SIZE_T length = RTL_NUMBER_OF(needle) - 1;
    if (Name == NULL || Name->Buffer == NULL) return FALSE;
    count = Name->Length / sizeof(WCHAR);
    for (i = 0; i + length < count; ++i) {
        for (j = 0; j < length; ++j) {
            if (RtlUpcaseUnicodeChar(Name->Buffer[i + j]) != RtlUpcaseUnicodeChar(needle[j])) {
                break;
            }
        }
        if (j == length &&
            (Name->Buffer[i + j] == L'#' || Name->Buffer[i + j] == L'&')) return TRUE;
    }
    return FALSE;
}

static NTSTATUS QueryIoctl(WDFIOTARGET Target, ULONG Code, PVOID Buffer, ULONG Size)
{
    WDF_MEMORY_DESCRIPTOR output;
    WDF_REQUEST_SEND_OPTIONS options;
    ULONG_PTR received = 0;
    NTSTATUS status;
    WDF_MEMORY_DESCRIPTOR_INIT_BUFFER(&output, Buffer, Size);
    WDF_REQUEST_SEND_OPTIONS_INIT(&options, WDF_REQUEST_SEND_OPTION_TIMEOUT);
    WDF_REQUEST_SEND_OPTIONS_SET_TIMEOUT(&options, WDF_REL_TIMEOUT_IN_MS(1000));
    status = WdfIoTargetSendIoctlSynchronously(Target, NULL, Code, NULL,
                                              &output, &options, &received);
    if (NT_SUCCESS(status) && received < Size) status = STATUS_DEVICE_DATA_ERROR;
    return status;
}

static NTSTATUS ValidateTarget(WDFIOTARGET Target)
{
    HID_COLLECTION_INFORMATION info;
    PHIDP_PREPARSED_DATA data = NULL;
    HIDP_CAPS caps;
    NTSTATUS status;
    RtlZeroMemory(&info, sizeof(info));
    status = QueryIoctl(Target, IOCTL_HID_GET_COLLECTION_INFORMATION, &info, sizeof(info));
    if (!NT_SUCCESS(status)) return status;
    if (info.VendorID != 0x5566 || info.ProductID != 0x000a ||
        info.DescriptorSize == 0 || info.DescriptorSize > 65536) {
        return STATUS_DEVICE_CONFIGURATION_ERROR;
    }
    data = (PHIDP_PREPARSED_DATA)ExAllocatePool2(POOL_FLAG_NON_PAGED,
                                                info.DescriptorSize, MK_TAG);
    if (data == NULL) return STATUS_INSUFFICIENT_RESOURCES;
    status = QueryIoctl(Target, IOCTL_HID_GET_COLLECTION_DESCRIPTOR, data, info.DescriptorSize);
    if (NT_SUCCESS(status)) {
        status = HidP_GetCaps(data, &caps);
        if (status == HIDP_STATUS_SUCCESS) {
            status = (caps.UsagePage == 0xff00 && caps.Usage == 1 &&
                      caps.InputReportByteLength == MK_REPORT_BYTES &&
                      caps.OutputReportByteLength == MK_REPORT_BYTES)
                         ? STATUS_SUCCESS : STATUS_DEVICE_CONFIGURATION_ERROR;
        }
    }
    ExFreePoolWithTag(data, MK_TAG);
    return status;
}

static NTSTATUS OpenKeyboard(DEVICE_CONTEXT* Context, WDFIOTARGET* Result)
{
    PWSTR interfaces = NULL;
    PWSTR path;
    WDFIOTARGET target = NULL;
    WDF_IO_TARGET_OPEN_PARAMS open;
    WDF_OBJECT_ATTRIBUTES attributes;
    UNICODE_STRING name;
    NTSTATUS status;
    NTSTATUS last = STATUS_DEVICE_NOT_CONNECTED;
    *Result = NULL;

    status = IoGetDeviceInterfaces(&GUID_DEVINTERFACE_HID, NULL, 0, &interfaces);
    if (!NT_SUCCESS(status)) return status;
    for (path = interfaces; *path; path += wcslen(path) + 1) {
        RtlInitUnicodeString(&name, path);
        if (!MkIsPhysicalInterface(&name)) continue;
        WDF_OBJECT_ATTRIBUTES_INIT(&attributes);
        attributes.ParentObject = Context->Device;
        status = WdfIoTargetCreate(Context->Device, &attributes, &target);
        if (!NT_SUCCESS(status)) { last = status; break; }
        WDF_IO_TARGET_OPEN_PARAMS_INIT_OPEN_BY_NAME(&open, &name, GENERIC_READ | GENERIC_WRITE);
        // One complete transaction batch owns the collection. Do not race the
        // vendor controller or another RGB program on the same command stream.
        open.ShareAccess = 0;
        status = WdfIoTargetOpen(target, &open);
        if (NT_SUCCESS(status)) status = ValidateTarget(target);
        if (NT_SUCCESS(status)) {
            *Result = target;
            last = STATUS_SUCCESS;
            break;
        }
        last = status;
        WdfObjectDelete(target);
        target = NULL;
    }
    ExFreePool(interfaces);
    return last;
}

static NTSTATUS Transaction(WDFIOTARGET Target, UCHAR Report[MK_REPORT_BYTES])
{
    WDF_MEMORY_DESCRIPTOR buffer;
    WDF_REQUEST_SEND_OPTIONS options;
    UCHAR response[MK_REPORT_BYTES];
    ULONG_PTR transferred = 0;
    ULONG i;
    NTSTATUS status;
    LARGE_INTEGER delay;

    Report[0] = 0;
    Report[1] = 0x55;
    Report[4] = 0;
    for (i = 5; i < MK_REPORT_BYTES; ++i) Report[4] += Report[i];
    // Preserve the vendor's 20 ms command spacing; do not busy-spin.
    delay.QuadPart = -20LL * 10000;
    (VOID)KeDelayExecutionThread(KernelMode, FALSE, &delay);
    WDF_REQUEST_SEND_OPTIONS_INIT(&options, WDF_REQUEST_SEND_OPTION_TIMEOUT);
    WDF_REQUEST_SEND_OPTIONS_SET_TIMEOUT(&options, WDF_REL_TIMEOUT_IN_MS(1000));
    WDF_MEMORY_DESCRIPTOR_INIT_BUFFER(&buffer, Report, MK_REPORT_BYTES);
    status = WdfIoTargetSendWriteSynchronously(Target, NULL, &buffer, NULL, &options, &transferred);
    if (!NT_SUCCESS(status)) return status;
    if (transferred != MK_REPORT_BYTES) return STATUS_DEVICE_DATA_ERROR;

    RtlZeroMemory(response, sizeof(response));
    WDF_MEMORY_DESCRIPTOR_INIT_BUFFER(&buffer, response, sizeof(response));
    status = WdfIoTargetSendReadSynchronously(Target, NULL, &buffer, NULL, &options, &transferred);
    if (!NT_SUCCESS(status)) return status;
    // The vendor strips report ID 0 before checking its AA response marker.
    if (transferred < 2 || response[0] != 0 || response[1] != 0xaa)
        return STATUS_DEVICE_PROTOCOL_ERROR;
    return STATUS_SUCCESS;
}

static NTSTATUS Command(WDFIOTARGET Target, UCHAR Selector, UCHAR Argument)
{
    UCHAR report[MK_REPORT_BYTES] = {0};
    report[2] = Selector;
    report[3] = Argument;
    return Transaction(Target, report);
}

static BOOLEAN IsPowered(DEVICE_CONTEXT* Context)
{
    BOOLEAN powered;
    WdfSpinLockAcquire(Context->Lock);
    powered = Context->Powered;
    WdfSpinLockRelease(Context->Lock);
    return powered;
}

NTSTATUS MkApplyColors(DEVICE_CONTEXT* Context, const MK_COLOR* Colors)
{
    WDFIOTARGET target = NULL;
    UCHAR frame[500] = {0};
    UCHAR report[MK_REPORT_BYTES];
    ULONG i, offset;
    NTSTATUS status = OpenKeyboard(Context, &target);
    if (!NT_SUCCESS(status)) return status;
    if (!IsPowered(Context)) {
        WdfIoTargetClose(target);
        WdfObjectDelete(target);
        return STATUS_CANCELLED;
    }

    for (i = 0; i < RTL_NUMBER_OF(ActiveSlots); ++i) {
        MK_COLOR color = Colors[i];
        offset = ActiveSlots[i] * 4;
        frame[offset]     = (UCHAR)((color.R * (ULONG)color.Intensity + 127) / 255);
        frame[offset + 1] = (UCHAR)((color.G * (ULONG)color.Intensity + 127) / 255);
        frame[offset + 2] = (UCHAR)((color.B * (ULONG)color.Intensity + 127) / 255);
        frame[offset + 3] = 0xff;
    }
    // Re-enter on each batch, including after reconnect or a failed batch.
    // Mark ownership before sending: a failed ACK may still mean DC was applied.
    Context->Owned = TRUE;
    status = Command(target, 0xdc, 1);
    if (NT_SUCCESS(status)) status = Command(target, 0x01, 0);
    for (offset = 0; NT_SUCCESS(status) && offset < MK_FRAME_BYTES; offset += MK_CHUNK_BYTES) {
        if (!IsPowered(Context)) { status = STATUS_CANCELLED; break; }
        RtlZeroMemory(report, sizeof(report));
        report[2] = 0xdd;
        report[5] = (UCHAR)min(MK_CHUNK_BYTES, MK_FRAME_BYTES - offset);
        report[6] = (UCHAR)offset;
        report[7] = (UCHAR)(offset >> 8);
        // Last chunk declares 44 but copies/checksums 56, matching the app.
        RtlCopyMemory(report + 9, frame + offset, MK_CHUNK_BYTES);
        status = Transaction(target, report);
    }
    if (NT_SUCCESS(status)) status = Command(target, 0x02, 0);
    if (!NT_SUCCESS(status)) {
        // Do not intentionally leave an incomplete frame under host control.
        if (NT_SUCCESS(Command(target, 0xdc, 0))) Context->Owned = FALSE;
    }
    WdfIoTargetClose(target);
    WdfObjectDelete(target);
    return status;
}

NTSTATUS MkReleaseKeyboard(DEVICE_CONTEXT* Context)
{
    WDFIOTARGET target = NULL;
    NTSTATUS status;
    if (!Context->Owned) return STATUS_SUCCESS;
    status = OpenKeyboard(Context, &target);
    if (!NT_SUCCESS(status)) return status;
    status = Command(target, 0xdc, 0);
    if (NT_SUCCESS(status)) Context->Owned = FALSE;
    WdfIoTargetClose(target);
    WdfObjectDelete(target);
    return status;
}
