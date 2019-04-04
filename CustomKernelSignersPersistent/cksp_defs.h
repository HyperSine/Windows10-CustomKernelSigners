#pragma once
#include <ntifs.h>

NTSTATUS NTAPI ZwQueryLicenseValue(
    _In_ PUNICODE_STRING ValueName,
    _Out_opt_ PULONG Type,
    _Out_writes_bytes_to_opt_(DataSize, *ResultDataSize) PVOID Data,
    _In_ ULONG DataSize,
    _Out_ PULONG ResultDataSize
);

NTSTATUS NTAPI ExUpdateLicenseData(
    _In_ ULONG cbBytes,
    _In_reads_bytes_(cbBytes) PVOID lpBytes
);

typedef enum _CKSP_WORKER_ACTION {
    CkspWorkerActionNone = 0,
    CkspWorkerActionStop = 1
} CKSP_WORKER_ACTION;

typedef struct _CKSP_WORKER_CONTEXT {
    UNICODE_STRING                  LaunchDriverServiceName;
    HANDLE                          ProductOptionsKeyChangeEventHandle;
    PKEVENT                         ProductOptionsKeyChangeEventObject;
    HANDLE                          ProductOptionsKey;
    PKEY_VALUE_PARTIAL_INFORMATION  ProductPolicyValueInfo;
    ULONG                           ProductPolicyValueInfoSize;
    CKSP_WORKER_ACTION              Action;
} CKSP_WORKER_CONTEXT, *PCKSP_WORKER_CONTEXT;

extern PCKSP_WORKER_CONTEXT g_CkspWorkerContext;
extern HANDLE g_CkspWorkerThreadHandle;
extern PVOID  g_CkspWorkerThreadObject;

NTSTATUS NTAPI DriverEntry(
    _In_ PDRIVER_OBJECT DriverObject, 
    _In_ PUNICODE_STRING RegistryPath
);

VOID NTAPI DriverUnload(
    _In_ PDRIVER_OBJECT DriverObject
);

NTSTATUS NTAPI IrpNullHandler(
    _In_ PDEVICE_OBJECT DeviceObject, 
    _In_ PIRP Irp
);

//
// CKSP routines
//

NTSTATUS CkspInitContext(
    _In_ PCKSP_WORKER_CONTEXT Context,
    _In_ PDRIVER_OBJECT DriverObject,
    _In_ PUNICODE_STRING RegistryPath
);

VOID CkspClearContext(
    _In_ PCKSP_WORKER_CONTEXT Context
);

VOID NTAPI CkspWorker(
    _In_ PVOID StartContext
);

VOID CkspDeferUnloadAsPossible(
    _In_ PCKSP_WORKER_CONTEXT Context
);


