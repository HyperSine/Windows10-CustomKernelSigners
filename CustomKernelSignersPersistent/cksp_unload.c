#include "cksp_defs.h"

VOID NTAPI DriverUnload(_In_ PDRIVER_OBJECT DriverObject) {
    UNREFERENCED_PARAMETER(DriverObject);

    g_CkspWorkerContext->Action = CkspWorkerActionStop;
    KeSetEvent(g_CkspWorkerContext->ProductOptionsKeyChangeEventObject, IO_NO_INCREMENT, TRUE);
    KeWaitForSingleObject(g_CkspWorkerThreadObject, Executive, KernelMode, FALSE, NULL);

    ObDereferenceObject(g_CkspWorkerThreadObject);
    g_CkspWorkerThreadObject = NULL;

    ZwClose(g_CkspWorkerThreadHandle);
    g_CkspWorkerThreadHandle = NULL;

    ExFreePoolWithTag(g_CkspWorkerContext, 'cksp');
    g_CkspWorkerContext = NULL;
}

VOID NTAPI CkspDeferUnloadWorker(PVOID StartContext) {
    NTSTATUS Status;
    PUNICODE_STRING DriverServiceName;
    
    DriverServiceName = (PUNICODE_STRING)StartContext;
    Status = ZwUnloadDriver(DriverServiceName);

    RtlFreeUnicodeString(DriverServiceName);
    ExFreePoolWithTag(StartContext, 'cksp');
    PsTerminateSystemThread(Status);
}

VOID CkspDeferUnloadAsPossible(_In_ PCKSP_WORKER_CONTEXT Context) {
    PUNICODE_STRING DriverServiceName;
    
    DriverServiceName = (PUNICODE_STRING)ExAllocatePoolWithTag(PagedPool, sizeof(UNICODE_STRING), 'cksp');
    if (DriverServiceName) {
        NTSTATUS Status;
        HANDLE ThreadHandle;

        RtlCopyMemory(DriverServiceName, &Context->LaunchDriverServiceName, sizeof(UNICODE_STRING));
        RtlZeroMemory(&Context->LaunchDriverServiceName, sizeof(UNICODE_STRING));

        Status = PsCreateSystemThread(&ThreadHandle,
                                      THREAD_ALL_ACCESS,
                                      NULL,
                                      NULL,
                                      NULL,
                                      CkspDeferUnloadWorker,
                                      DriverServiceName);
        if (!NT_SUCCESS(Status)) {
            RtlFreeUnicodeString(DriverServiceName);
            ExFreePoolWithTag(DriverServiceName, 'cksp');
            DriverServiceName = NULL;
        } else {
            ZwClose(ThreadHandle);
        }
    }
}
