//
// callout.c  —  Connect/Bind redirect callout functions
//
// How it works:
//   FWPM_LAYER_ALE_CONNECT_REDIRECT_V4  : modify remote IP/port before connect
//   FWPM_LAYER_ALE_BIND_REDIRECT_V4     : modify local IP to match virtual NIC
//
#include <initguid.h>
#include "wfpredirect.h"

// ---------------------------------------------------------------
// Global state definitions
// ---------------------------------------------------------------
volatile LONG  g_TargetPid = 0;           // 0 = disabled
volatile ULONG g_DestIp = 0x0A080002;  // default 10.8.0.2, host byte order
volatile USHORT g_DestPort = 0;           // 0 = keep original port

// ---------------------------------------------------------------
// Helper: convert host-byte-order ULONG to network byte order
// ---------------------------------------------------------------
static FORCEINLINE ULONG HostToNetLong(ULONG x)
{
	return RtlUlongByteSwap(x);
}

static FORCEINLINE USHORT HostToNetShort(USHORT x)
{
	return RtlUshortByteSwap(x);
}

// ---------------------------------------------------------------
// ConnectRedirectClassify
//
// Called at FWPM_LAYER_ALE_CONNECT_REDIRECT_V4 for every outbound
// TCP/UDP connect attempt.  If the initiating PID matches our target,
// we rewrite the remote address (and optionally port) to g_DestIp.
// ---------------------------------------------------------------
VOID
ConnectRedirectClassify(
	_In_        const FWPS_INCOMING_VALUES0* inFixedValues,
	_In_        const FWPS_INCOMING_METADATA_VALUES0* inMetaValues,
	_Inout_opt_ VOID* layerData,
	_In_opt_    const VOID* classifyContext,
	_In_        const FWPS_FILTER1* filter,
	_In_        UINT64                                 flowContext,
	_Inout_     FWPS_CLASSIFY_OUT0* classifyOut
)
{
	NTSTATUS                 status = STATUS_SUCCESS;
	FWPS_CONNECT_REQUEST0* connectRequest = NULL;
	SOCKADDR_IN* remoteAddr = NULL;
	ULONG                    targetPid = (ULONG)g_TargetPid;
	ULONG                    destIp = g_DestIp;
	USHORT                   destPort = g_DestPort;

	UINT64 classifyHandle = 0;

	UNREFERENCED_PARAMETER(inFixedValues);
	UNREFERENCED_PARAMETER(classifyContext);
	UNREFERENCED_PARAMETER(filter);
	UNREFERENCED_PARAMETER(flowContext);

	if ((classifyOut->rights & FWPS_RIGHT_ACTION_WRITE) == 0) {
		KdPrint(("WfpRedirect: No write rights, skipping.\n"));
		return;
	}

	if (classifyContext == NULL) {
		KdPrint(("WfpRedirect: No classifyContext, skipping.\n"));
		return;
	}

	// STEP 1: Acquire a classify handle from classifyContext
	status = FwpsAcquireClassifyHandle0(classifyContext, 0, &classifyHandle);
	if (!NT_SUCCESS(status)) {
		KdPrint(("WfpRedirect: FwpsAcquireClassifyHandle0 failed: 0x%08X\n", status));
		return;
	}


	// Default: permit and move on
	classifyOut->actionType = FWP_ACTION_PERMIT;

	// Redirection disabled?
	if (targetPid == 0 || destIp == 0)
		return;

	// Does this connection come from our target process?
	//if (!(inMetaValues->currentMetadataValues & FWPS_METADATA_FIELD_PROCESS_ID))
	//    return;

	//if (inMetaValues->processId != (UINT64)targetPid)
	//    return;

	// layerData points to the modifiable connect request
	if (layerData == NULL)
		return;

	connectRequest = (FWPS_CONNECT_REQUEST0*)layerData;

	if (classifyContext == NULL)
	{
		DbgPrintEx(DPFLTR_IHVNETWORK_ID, DPFLTR_ERROR_LEVEL,
			"[WfpRedir] classifyContext is NULL, filter action type wrong?\n");
		classifyOut->actionType = FWP_ACTION_PERMIT;
		return;
	}

	DbgPrintEx(DPFLTR_IHVNETWORK_ID, DPFLTR_ERROR_LEVEL,
		"[WfpRedir] classifyOut->rights = 0x%08X, actionType = 0x%08X\n",
		classifyOut->rights,
		classifyOut->actionType);
	// We must acquire write access to the connect request
	// by calling FwpsAcquireWritableLayerDataPointer0
	FWPS_CONNECT_REQUEST0* writableRequest = NULL;
	FWPS_CONNECT_REQUEST0* initialRequest = (FWPS_CONNECT_REQUEST0*)layerData;

	// If localRedirectHandle is set, this is a reauth of an already-redirected flow
	if (initialRequest->localRedirectHandle != NULL)
	{
		classifyOut->actionType = FWP_ACTION_PERMIT;
		return;
	}
	status = FwpsAcquireWritableLayerDataPointer0(
		classifyHandle,
		filter->filterId,
		0,
		(PVOID*)&writableRequest,
		classifyOut);

	if (!NT_SUCCESS(status) || writableRequest == NULL)
	{
		DbgPrintEx(DPFLTR_IHVNETWORK_ID, DPFLTR_ERROR_LEVEL,
			"[WfpRedir] FwpsAcquireWritableLayerDataPointer0 failed: 0x%08X\n", status);
		classifyOut->actionType = FWP_ACTION_PERMIT;
		return;
	}

	// Read original destination for logging
	remoteAddr = (SOCKADDR_IN*)&writableRequest->remoteAddressAndPort;
	ULONG origIp = RtlUlongByteSwap(remoteAddr->sin_addr.s_addr);
	USHORT origPort = RtlUshortByteSwap(remoteAddr->sin_port);

	if (remoteAddr->sin_addr.S_un.S_addr == origIp) {
		DbgPrint("WfpRedir: Original IP is the same as destination IP, skipping rewrite.\n");
		FwpsApplyModifiedLayerData0(classifyHandle, (PVOID)connectRequest, 0);
		FwpsReleaseClassifyHandle0(classifyHandle);
		return;
	}
	// --- Rewrite remote IP ---
	remoteAddr->sin_addr.s_addr = HostToNetLong(destIp);

	// --- Rewrite remote port (only if g_DestPort != 0) ---
	if (destPort != 0)
		remoteAddr->sin_port = HostToNetShort(destPort);

	DbgPrintEx(DPFLTR_IHVNETWORK_ID, DPFLTR_INFO_LEVEL,
		"[WfpRedir] PID %lu: %d.%d.%d.%d:%d  -->  %d.%d.%d.%d:%d\n",
		targetPid,
		(origIp >> 24) & 0xFF, (origIp >> 16) & 0xFF,
		(origIp >> 8) & 0xFF, origIp & 0xFF,
		origPort,
		(destIp >> 24) & 0xFF, (destIp >> 16) & 0xFF,
		(destIp >> 8) & 0xFF, destIp & 0xFF,
		(destPort != 0) ? destPort : origPort);

	// Commit the changes and release
	FwpsApplyModifiedLayerData0(
		classifyHandle,
		(PVOID)writableRequest,
		0);

	// PERMIT with the modified address
	classifyOut->actionType = FWP_ACTION_PERMIT;
	classifyOut->rights &= ~FWPS_RIGHT_ACTION_WRITE; // Clear the write right to prevent other filters from changing our decision

	// STEP 6: Release the classify handle
	FwpsReleaseClassifyHandle0(classifyHandle);
}

// ---------------------------------------------------------------
// Called at FWPM_LAYER_ALE_BIND_REDIRECT_V4.
// Rewrites the local bind address to g_DestIp so the OS routes
// outbound packets through the virtual NIC that owns that IP.
// ---------------------------------------------------------------
//VOID
//BindRedirectClassify(
//    _In_        const FWPS_INCOMING_VALUES0* inFixedValues,
//    _In_        const FWPS_INCOMING_METADATA_VALUES0* inMetaValues,
//    _Inout_opt_ VOID* layerData,
//    _In_opt_    const VOID* classifyContext,
//    _In_        const FWPS_FILTER1* filter,
//    _In_        UINT64                                 flowContext,
//    _Inout_     FWPS_CLASSIFY_OUT0* classifyOut
//)
//{
//    NTSTATUS              status = STATUS_SUCCESS;
//    FWPS_BIND_REQUEST0* writableReq = NULL;
//    SOCKADDR_IN* localAddr = NULL;
//    ULONG                 targetPid = (ULONG)g_TargetPid;
//    ULONG                 destIp = g_DestIp;
//
//    UNREFERENCED_PARAMETER(inFixedValues);
//    UNREFERENCED_PARAMETER(layerData);
//    UNREFERENCED_PARAMETER(filter);
//    UNREFERENCED_PARAMETER(flowContext);
//
//    classifyOut->actionType = FWP_ACTION_PERMIT;
//
//    //if (targetPid == 0 || destIp == 0)
//    //    return;
//
//    if (destIp == 0)
//        return;
//
//    //if (!(inMetaValues->currentMetadataValues & FWPS_METADATA_FIELD_PROCESS_ID))
//    //    return;
//
//    //if (inMetaValues->processId != (UINT64)targetPid)
//    //    return;
//
//    // FwpsAcquireWritableLayerDataPointer0 要求 classifyContext 非 NULL
//    if (classifyContext == NULL)
//    {
//        DbgPrintEx(DPFLTR_IHVNETWORK_ID, DPFLTR_ERROR_LEVEL,
//            "[WfpRedir] classifyContext is NULL, filter action type wrong?\n");
//        classifyOut->actionType = FWP_ACTION_PERMIT;
//        return;
//    }
//
//    status = FwpsAcquireWritableLayerDataPointer0(
//        classifyContext,
//        filter->filterId,
//        0,
//        (PVOID*)&writableReq,
//        classifyOut);
//
//    if (!NT_SUCCESS(status) || writableReq == NULL)
//    {
//        DbgPrintEx(DPFLTR_IHVNETWORK_ID, DPFLTR_ERROR_LEVEL,
//            "[WfpRedir] Bind: FwpsAcquireWritableLayerDataPointer0 failed: 0x%08X\n", status);
//        classifyOut->actionType = FWP_ACTION_PERMIT;
//        return;
//    }
//
//    localAddr = (SOCKADDR_IN*)&writableReq->localAddressAndPort;
//
//    // Only redirect if currently bound to INADDR_ANY (0.0.0.0)
//    // Avoid overwriting explicit binds the app made itself.
//    if (localAddr->sin_addr.s_addr == 0)
//    {
//        localAddr->sin_addr.s_addr = HostToNetLong(destIp);
//
//        DbgPrintEx(DPFLTR_IHVNETWORK_ID, DPFLTR_INFO_LEVEL,
//            "[WfpRedir] PID %lu: bind local --> %d.%d.%d.%d\n",
//            targetPid,
//            (destIp >> 24) & 0xFF, (destIp >> 16) & 0xFF,
//            (destIp >> 8) & 0xFF, destIp & 0xFF);
//
//        FwpsApplyModifiedLayerData0(classifyContext, (PVOID)writableReq, 0);
//    }
//    else
//    {
//        // App bound explicitly — release without changes
//        FwpsApplyModifiedLayerData0(classifyContext, (PVOID)writableReq, 0);
//    }
//
//    classifyOut->actionType = FWP_ACTION_PERMIT;
//}

// ---------------------------------------------------------------
// CommonNotify  — shared by both callouts
// ---------------------------------------------------------------
NTSTATUS
CommonNotify(
	_In_ FWPS_CALLOUT_NOTIFY_TYPE  notifyType,
	_In_ const GUID* filterKey,
	_Inout_ FWPS_FILTER1* filter
)
{
	UNREFERENCED_PARAMETER(notifyType);
	UNREFERENCED_PARAMETER(filterKey);
	UNREFERENCED_PARAMETER(filter);
	return STATUS_SUCCESS;
}
