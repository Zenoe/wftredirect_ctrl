#pragma once

#include <ntddk.h>
#include <wdm.h>
#include <fwpsk.h>    // WFP kernel-mode callout APIs
#include <fwpmk.h>    // WFP management APIs (kernel)

// Unique GUIDs ¡ª generated once for this driver.
// {A1B2C3D4-E5F6-7890-ABCD-EF1234567890}
DEFINE_GUID(WFP_HELLO_CALLOUT_GUID,
    0xa1b2c3d4, 0xe5f6, 0x7890,
    0xab, 0xcd, 0xef, 0x12, 0x34, 0x56, 0x78, 0x90);

// {B2C3D4E5-F6A7-8901-BCDE-F12345678901}
DEFINE_GUID(WFP_HELLO_FILTER_GUID,
    0xb2c3d4e5, 0xf6a7, 0x8901,
    0xbc, 0xde, 0xf1, 0x23, 0x45, 0x67, 0x89, 0x01);

// {C3D4E5F6-A7B8-9012-CDEF-123456789012}
DEFINE_GUID(WFP_HELLO_SUBLAYER_GUID,
    0xc3d4e5f6, 0xa7b8, 0x9012,
    0xcd, 0xef, 0x12, 0x34, 0x56, 0x78, 0x90, 0x12);

// Prototypes
DRIVER_UNLOAD    WfpHelloUnload;
DRIVER_INITIALIZE DriverEntry;

NTSTATUS WfpHelloRegister(_In_ PDEVICE_OBJECT DeviceObject);
VOID     WfpHelloUnregister(VOID);