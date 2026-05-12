//
// register.c  —  Register/unregister WFP callouts, sublayer, filters
//
#include "wfpredirect.h"

static HANDLE  g_EngineHandle = NULL;
static UINT32  g_ConnectCalloutId = 0;
static BOOLEAN g_ConnectCalloutReg = FALSE;

// ---------------------------------------------------------------
// Helper: register one kernel callout
// ---------------------------------------------------------------
static NTSTATUS
RegisterKernelCallout(
    _In_ PDEVICE_OBJECT  DeviceObject,
    _In_ const GUID* calloutKey,
    _In_ FWPS_CALLOUT_CLASSIFY_FN1   classifyFn,
    _Out_ UINT32* calloutId
)
{
    FWPS_CALLOUT1 c = { 0 };
    c.calloutKey = *calloutKey;
    c.classifyFn = classifyFn;
    c.notifyFn = CommonNotify;
    c.flowDeleteFn = NULL;

    return FwpsCalloutRegister0(DeviceObject, &c, calloutId);
}

// ---------------------------------------------------------------
// Helper: add one management callout inside a transaction
// ---------------------------------------------------------------
static NTSTATUS
AddManagementCallout(
    _In_ HANDLE      engineHandle,
    _In_ const GUID* calloutKey,
    _In_ const GUID* layerKey,
    _In_ PCWSTR      name
)
{
    FWPM_CALLOUT0 mc = { 0 };
    mc.calloutKey = *calloutKey;
    mc.displayData.name = (PWSTR)name;
    mc.applicableLayer = *layerKey;

    NTSTATUS status = FwpmCalloutAdd0(engineHandle, &mc, NULL, NULL);
    if (status == STATUS_OBJECT_NAME_COLLISION)
        status = STATUS_SUCCESS;
    return status;
}

static NTSTATUS
AddFilter(
    _In_ HANDLE      engineHandle,
    _In_ const GUID* filterKey,
    _In_ const GUID* layerKey,
    _In_ const GUID* calloutKey,
    _In_ PCWSTR      name
)
{
    FWPM_FILTER0 f = { 0 };
    f.filterKey = *filterKey;
    f.displayData.name = (PWSTR)name;
    f.layerKey = *layerKey;
    f.subLayerKey = WFPREDIR_SUBLAYER_GUID;
    f.weight.type = FWP_EMPTY;
    f.numFilterConditions = 0;
    f.filterCondition = NULL;

    f.action.type = FWP_ACTION_CALLOUT_UNKNOWN;
    f.action.calloutKey = *calloutKey;

    UINT64 filterId = 0;
    return FwpmFilterAdd0(engineHandle, &f, NULL, &filterId);
}

// ---------------------------------------------------------------
// WfpRedirRegister
// ---------------------------------------------------------------
NTSTATUS
WfpRedirRegister(_In_ PDEVICE_OBJECT DeviceObject)
{
    NTSTATUS status;

    // 1. Open WFP engine
    FWPM_SESSION0 session = { 0 };
    /* session.flags = FWPM_SESSION_FLAG_DYNAMIC; */

    status = FwpmEngineOpen0(NULL, RPC_C_AUTHN_WINNT, NULL, &session, &g_EngineHandle);
    if (!NT_SUCCESS(status)) {
        DbgPrintEx(DPFLTR_IHVNETWORK_ID, DPFLTR_ERROR_LEVEL,
            "[WfpRedir] FwpmEngineOpen0 failed: 0x%08X\n", status);
        return status;
    }

    // 2. Register kernel callout
    status = RegisterKernelCallout(
        DeviceObject,
        &WFPREDIR_CONNECT_CALLOUT_GUID,
        ConnectRedirectClassify,
        &g_ConnectCalloutId);
    if (!NT_SUCCESS(status)) {
        DbgPrintEx(DPFLTR_IHVNETWORK_ID, DPFLTR_ERROR_LEVEL,
            "[WfpRedir] Connect callout register failed: 0x%08X\n", status);
        WfpRedirUnregister();
        return status;
    }
    g_ConnectCalloutReg = TRUE;

    // 3. Add management objects inside a transaction
    status = FwpmTransactionBegin0(g_EngineHandle, 0);
    if (!NT_SUCCESS(status)) { WfpRedirUnregister(); return status; }

    // 3a. Sublayer
    {
        FWPM_SUBLAYER0 sl = { 0 };
        sl.subLayerKey = WFPREDIR_SUBLAYER_GUID;
        sl.displayData.name = L"WfpRedirect Sublayer";
        sl.weight = 0x8000;

        status = FwpmSubLayerAdd0(g_EngineHandle, &sl, NULL);
        if (status == STATUS_OBJECT_NAME_COLLISION) status = STATUS_SUCCESS;
        if (!NT_SUCCESS(status)) { FwpmTransactionAbort0(g_EngineHandle); WfpRedirUnregister(); return status; }
    }

    // 3b. Connect-redirect management callout + filter
    status = AddManagementCallout(
        g_EngineHandle,
        &WFPREDIR_CONNECT_CALLOUT_GUID,
        &FWPM_LAYER_ALE_CONNECT_REDIRECT_V4,
        L"WfpRedirect Connect Callout");
    if (!NT_SUCCESS(status)) { FwpmTransactionAbort0(g_EngineHandle); WfpRedirUnregister(); return status; }

    status = AddFilter(
        g_EngineHandle,
        &WFPREDIR_CONNECT_FILTER_GUID,
        &FWPM_LAYER_ALE_CONNECT_REDIRECT_V4,
        &WFPREDIR_CONNECT_CALLOUT_GUID,
        L"WfpRedirect Connect Filter");
    if (!NT_SUCCESS(status)) { FwpmTransactionAbort0(g_EngineHandle); WfpRedirUnregister(); return status; }

    // 4. Commit
    status = FwpmTransactionCommit0(g_EngineHandle);
    if (!NT_SUCCESS(status)) {
        DbgPrintEx(DPFLTR_IHVNETWORK_ID, DPFLTR_ERROR_LEVEL,
            "[WfpRedir] Transaction commit failed: 0x%08X\n", status);
        WfpRedirUnregister();
        return status;
    }

    DbgPrintEx(DPFLTR_IHVNETWORK_ID, DPFLTR_INFO_LEVEL,
        "[WfpRedir] Registered. Waiting for PID via IOCTL...\n");
    return STATUS_SUCCESS;
}

// ---------------------------------------------------------------
// WfpRedirUnregister
// ---------------------------------------------------------------
VOID
WfpRedirUnregister(VOID)
{
        if (g_EngineHandle) {
        // Must manually delete filter, callout, sublayer
        FwpmFilterDeleteByKey0(g_EngineHandle, &WFPREDIR_CONNECT_FILTER_GUID);
        FwpmCalloutDeleteByKey0(g_EngineHandle, &WFPREDIR_CONNECT_CALLOUT_GUID);
        FwpmSubLayerDeleteByKey0(g_EngineHandle, &WFPREDIR_SUBLAYER_GUID);
        FwpmEngineClose0(g_EngineHandle);
        g_EngineHandle = NULL;
    }

    if (g_EngineHandle) {
        FwpmEngineClose0(g_EngineHandle);
        g_EngineHandle = NULL;
    }

    if (g_ConnectCalloutReg) {
        FwpsCalloutUnregisterById0(g_ConnectCalloutId);
        g_ConnectCalloutReg = FALSE;
    }

    DbgPrintEx(DPFLTR_IHVNETWORK_ID, DPFLTR_INFO_LEVEL,
        "[WfpRedir] Unregistered.\n");
}
