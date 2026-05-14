#pragma once

//
// WFP Redirect Driver
// Redirects all TCP/UDP connections from a target PID to a specified IP.
//
// Include order is critical for WDK 10.0.26100+
//

#define NDIS_SUPPORT_NDIS6  1
#define NDIS60              1

#include <ntddk.h>
#include <ndis.h>
#include <fwpsk.h>
#include <fwpmk.h>
#include <wsk.h>

// ---------------------------------------------------------------
// IOCTLs  (user-mode app uses DeviceIoControl to set PID + dest IP)
// ---------------------------------------------------------------
#define WFPREDIR_DEVICE_NAME    L"\\Device\\WfpRedirect"
#define WFPREDIR_SYMLINK_NAME   L"\\DosDevices\\WfpRedirect"

#define WFPREDIR_IOCTL_BASE     0x8000

// Set target PID:  input = ULONG pid
#define IOCTL_WFPREDIR_SET_PID \
    CTL_CODE(FILE_DEVICE_UNKNOWN, WFPREDIR_IOCTL_BASE + 1, METHOD_BUFFERED, FILE_ANY_ACCESS)

// Set redirect dest IP (IPv4 dotted-decimal as ULONG, host byte order)
// input = ULONG ip  (e.g. 0x0A080002 for 10.8.0.2)
#define IOCTL_WFPREDIR_SET_DEST_IP \
    CTL_CODE(FILE_DEVICE_UNKNOWN, WFPREDIR_IOCTL_BASE + 2, METHOD_BUFFERED, FILE_ANY_ACCESS)

// Clear / disable redirection
#define IOCTL_WFPREDIR_CLEAR \
    CTL_CODE(FILE_DEVICE_UNKNOWN, WFPREDIR_IOCTL_BASE + 3, METHOD_BUFFERED, FILE_ANY_ACCESS)

// ---------------------------------------------------------------
// Global state (set by IOCTL, read by callout)
// ---------------------------------------------------------------
extern volatile LONG   g_TargetPid;       // 0 = disabled
extern volatile ULONG  g_DestIp;          // host byte order, e.g. 0x0A080002
extern volatile USHORT g_DestPort;        // 0 = keep original port, host byte order

// ---------------------------------------------------------------
// GUIDs
// ---------------------------------------------------------------

// Connect-redirect callout  {D1E2F3A4-B5C6-7890-DEFA-123456789ABC}
DEFINE_GUID(WFPREDIR_CONNECT_CALLOUT_GUID,
    0xd1e2f3a4, 0xb5c6, 0x7890,
    0xde, 0xfa, 0x12, 0x34, 0x56, 0x78, 0x9a, 0xbc);

// Bind-redirect callout  {E2F3A4B5-C6D7-8901-EFAB-23456789ABCD}
DEFINE_GUID(WFPREDIR_BIND_CALLOUT_GUID,
    0xe2f3a4b5, 0xc6d7, 0x8901,
    0xef, 0xab, 0x23, 0x45, 0x67, 0x89, 0xab, 0xcd);

// Sublayer  {F3A4B5C6-D7E8-9012-FABC-3456789ABCDE}
DEFINE_GUID(WFPREDIR_SUBLAYER_GUID,
    0xf3a4b5c6, 0xd7e8, 0x9012,
    0xfa, 0xbc, 0x34, 0x56, 0x78, 0x9a, 0xbc, 0xde);

// Filter GUIDs
DEFINE_GUID(WFPREDIR_CONNECT_FILTER_GUID,
    0xa4b5c6d7, 0xe8f9, 0x0123,
    0xab, 0xcd, 0x45, 0x67, 0x89, 0xab, 0xcd, 0xef);

DEFINE_GUID(WFPREDIR_BIND_FILTER_GUID,
    0xb5c6d7e8, 0xf9a0, 0x1234,
    0xbc, 0xde, 0x56, 0x78, 0x9a, 0xbc, 0xde, 0xf0);

// ---------------------------------------------------------------
// Prototypes
// ---------------------------------------------------------------
DRIVER_INITIALIZE  DriverEntry;
DRIVER_UNLOAD      WfpRedirUnload;

NTSTATUS WfpRedirRegister(_In_ PDEVICE_OBJECT DeviceObject);
VOID     WfpRedirUnregister(VOID);

// Callout classify functions
VOID ConnectRedirectClassify(
    _In_        const FWPS_INCOMING_VALUES0* inFixedValues,
    _In_        const FWPS_INCOMING_METADATA_VALUES0* inMetaValues,
    _Inout_opt_ VOID* layerData,
    _In_opt_    const VOID* classifyContext,
    _In_        const FWPS_FILTER1* filter,
    _In_        UINT64                                 flowContext,
    _Inout_     FWPS_CLASSIFY_OUT0* classifyOut);

VOID BindRedirectClassify(
    _In_        const FWPS_INCOMING_VALUES0* inFixedValues,
    _In_        const FWPS_INCOMING_METADATA_VALUES0* inMetaValues,
    _Inout_opt_ VOID* layerData,
    _In_opt_    const VOID* classifyContext,
    _In_        const FWPS_FILTER1* filter,
    _In_        UINT64                                 flowContext,
    _Inout_     FWPS_CLASSIFY_OUT0* classifyOut);

NTSTATUS CommonNotify(
    _In_ FWPS_CALLOUT_NOTIFY_TYPE  notifyType,
    _In_ const GUID* filterKey,
    _Inout_ FWPS_FILTER1* filter);