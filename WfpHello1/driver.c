//
// driver.c  —  DriverEntry, DriverUnload, IRP dispatch (IOCTL)
//
// User-mode sends IOCTLs to set the target PID and redirect destination.
//
#include "wfpredirect.h"

static PDEVICE_OBJECT g_DeviceObject = NULL;
static UNICODE_STRING  g_DeviceName;
static UNICODE_STRING  g_SymlinkName;

// ---------------------------------------------------------------
// IRP_MJ_CREATE / IRP_MJ_CLOSE  — allow user-mode to open handle
// ---------------------------------------------------------------
static NTSTATUS
DispatchCreateClose(
    _In_ PDEVICE_OBJECT DeviceObject,
    _Inout_ PIRP Irp
)
{
    UNREFERENCED_PARAMETER(DeviceObject);
    Irp->IoStatus.Status = STATUS_SUCCESS;
    Irp->IoStatus.Information = 0;
    IoCompleteRequest(Irp, IO_NO_INCREMENT);
    return STATUS_SUCCESS;
}

// ---------------------------------------------------------------
// IRP_MJ_DEVICE_CONTROL  — handle IOCTLs from user-mode
// ---------------------------------------------------------------
static NTSTATUS
DispatchIoControl(
    _In_ PDEVICE_OBJECT DeviceObject,
    _Inout_ PIRP Irp
)
{
    UNREFERENCED_PARAMETER(DeviceObject);

    PIO_STACK_LOCATION stack = IoGetCurrentIrpStackLocation(Irp);
    ULONG              ioctl = stack->Parameters.DeviceIoControl.IoControlCode;
    PVOID              buffer = Irp->AssociatedIrp.SystemBuffer;
    ULONG              inputLen = stack->Parameters.DeviceIoControl.InputBufferLength;
    NTSTATUS           status = STATUS_SUCCESS;

    switch (ioctl)
    {
        // ---- Set target PID ----------------------------------------
    case IOCTL_WFPREDIR_SET_PID:
        if (inputLen < sizeof(ULONG) || buffer == NULL) {
            status = STATUS_INVALID_PARAMETER;
            break;
        }
        {
            ULONG pid = *(ULONG*)buffer;
            InterlockedExchange(&g_TargetPid, (LONG)pid);
            DbgPrintEx(DPFLTR_IHVNETWORK_ID, DPFLTR_INFO_LEVEL,
                "[WfpRedir] Target PID set to: %lu\n", pid);
        }
        break;

        // ---- Set destination IP ------------------------------------
    case IOCTL_WFPREDIR_SET_DEST_IP:
        if (inputLen < sizeof(ULONG) || buffer == NULL) {
            status = STATUS_INVALID_PARAMETER;
            break;
        }
        {
            ULONG ip = *(ULONG*)buffer;  // host byte order, e.g. 0x0A080002
            InterlockedExchange((volatile LONG*)&g_DestIp, (LONG)ip);
            DbgPrintEx(DPFLTR_IHVNETWORK_ID, DPFLTR_INFO_LEVEL,
                "[WfpRedir] Dest IP set to: %d.%d.%d.%d\n",
                (ip >> 24) & 0xFF, (ip >> 16) & 0xFF,
                (ip >> 8) & 0xFF, ip & 0xFF);
        }
        break;

        // ---- Clear / disable ---------------------------------------
    case IOCTL_WFPREDIR_CLEAR:
        InterlockedExchange(&g_TargetPid, 0);
        DbgPrintEx(DPFLTR_IHVNETWORK_ID, DPFLTR_INFO_LEVEL,
            "[WfpRedir] Redirection disabled.\n");
        break;

    default:
        status = STATUS_INVALID_DEVICE_REQUEST;
        break;
    }

    Irp->IoStatus.Status = status;
    Irp->IoStatus.Information = 0;
    IoCompleteRequest(Irp, IO_NO_INCREMENT);
    return status;
}

// ---------------------------------------------------------------
// DriverUnload
// ---------------------------------------------------------------
VOID
WfpRedirUnload(_In_ PDRIVER_OBJECT DriverObject)
{
    UNREFERENCED_PARAMETER(DriverObject);

    DbgPrintEx(DPFLTR_IHVNETWORK_ID, DPFLTR_INFO_LEVEL,
        "[WfpRedir] Unloading...\n");

    WfpRedirUnregister();

    IoDeleteSymbolicLink(&g_SymlinkName);
    if (g_DeviceObject)
        IoDeleteDevice(g_DeviceObject);

    DbgPrintEx(DPFLTR_IHVNETWORK_ID, DPFLTR_INFO_LEVEL,
        "[WfpRedir] Unloaded.\n");
}

NTSTATUS DriverEntry( _In_ PDRIVER_OBJECT  DriverObject, _In_ PUNICODE_STRING RegistryPath){
    NTSTATUS status;
    UNREFERENCED_PARAMETER(RegistryPath);

    DbgPrintEx(DPFLTR_IHVNETWORK_ID, DPFLTR_INFO_LEVEL,
        "[WfpRedir] DriverEntry.\n");

    RtlInitUnicodeString(&g_DeviceName, WFPREDIR_DEVICE_NAME);
    RtlInitUnicodeString(&g_SymlinkName, WFPREDIR_SYMLINK_NAME);

    // Create control device so user-mode can send IOCTLs
    status = IoCreateDevice(
        DriverObject,
        0,
        &g_DeviceName,
        FILE_DEVICE_UNKNOWN,
        FILE_DEVICE_SECURE_OPEN,
        FALSE,
        &g_DeviceObject);

    if (!NT_SUCCESS(status)) {
        DbgPrintEx(DPFLTR_IHVNETWORK_ID, DPFLTR_ERROR_LEVEL,
            "[WfpRedir] IoCreateDevice failed: 0x%08X\n", status);
        return status;
    }

    status = IoCreateSymbolicLink(&g_SymlinkName, &g_DeviceName);
    if (!NT_SUCCESS(status)) {
        IoDeleteDevice(g_DeviceObject);
        return status;
    }

    // Hook dispatch routines
    DriverObject->MajorFunction[IRP_MJ_CREATE] = DispatchCreateClose;
    DriverObject->MajorFunction[IRP_MJ_CLOSE] = DispatchCreateClose;
    DriverObject->MajorFunction[IRP_MJ_DEVICE_CONTROL] = DispatchIoControl;
    DriverObject->DriverUnload = WfpRedirUnload;

    // Register WFP callouts
    status = WfpRedirRegister(g_DeviceObject);
    if (!NT_SUCCESS(status)) {
        WfpRedirUnregister();
        IoDeleteSymbolicLink(&g_SymlinkName);
        IoDeleteDevice(g_DeviceObject);
        return status;
    }

    DbgPrintEx(DPFLTR_IHVNETWORK_ID, DPFLTR_INFO_LEVEL,
        "[WfpRedir] Ready. Send IOCTL_WFPREDIR_SET_PID to begin.\n");
    return STATUS_SUCCESS;
}