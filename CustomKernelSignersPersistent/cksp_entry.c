#include "cksp_defs.h"

PCKSP_WORKER_CONTEXT g_CkspWorkerContext;
HANDLE g_CkspWorkerThreadHandle;
PVOID  g_CkspWorkerThreadObject;

NTSTATUS NTAPI DriverEntry(_In_ PDRIVER_OBJECT DriverObject, _In_ PUNICODE_STRING RegistryPath) {
    NTSTATUS Status;
    OBJECT_ATTRIBUTES ThreadAttribute;

    //
    // We don't handle any IRQs
    //
    for (int i = 0; i < IRP_MJ_MAXIMUM_FUNCTION; ++i)
        DriverObject->MajorFunction[i] = IrpNullHandler;

    DriverObject->DriverUnload = DriverUnload;

    //
    // Allocate CKSP context
    //
    g_CkspWorkerContext = (PCKSP_WORKER_CONTEXT)ExAllocatePoolWithTag(NonPagedPool, sizeof(CKSP_WORKER_CONTEXT), 'cksp');
    if (g_CkspWorkerContext == NULL) {
        Status = STATUS_NO_MEMORY;
        goto ON_DriverEntry_ERROR;
    }

    //
    // Initialize CKSP context
    //
    Status = CkspInitContext(g_CkspWorkerContext, DriverObject, RegistryPath);
    if (!NT_SUCCESS(Status))
        goto ON_DriverEntry_ERROR;
    
    //
    // Launch CkspWorker thread
    //
    InitializeObjectAttributes(&ThreadAttribute, NULL, OBJ_KERNEL_HANDLE, NULL, NULL);
    Status = PsCreateSystemThread(&g_CkspWorkerThreadHandle,
                                  THREAD_ALL_ACCESS,
                                  &ThreadAttribute,
                                  NULL,
                                  NULL,
                                  CkspWorker,
                                  g_CkspWorkerContext);
    if (!NT_SUCCESS(Status)) {
        goto ON_DriverEntry_ERROR;
    } else {
        //
        // never fail here
        //
        ObReferenceObjectByHandle(g_CkspWorkerThreadHandle,
                                  THREAD_ALL_ACCESS,
                                  *PsThreadType,
                                  KernelMode,
                                  &g_CkspWorkerThreadObject,
                                  NULL);
    }

    return Status;

ON_DriverEntry_ERROR:
    if (g_CkspWorkerContext) {
        CkspClearContext(g_CkspWorkerContext);
        ExFreePoolWithTag(g_CkspWorkerContext, 'cksp');
        g_CkspWorkerContext = NULL;
    }
    return Status;
}

