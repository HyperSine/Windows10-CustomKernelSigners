#include "cksp_defs.h"

typedef struct _PPBinaryHeader {
    ULONG TotalSize;
    ULONG DataSize;
    ULONG EndMarkerSize;
    ULONG Reserved;
    ULONG Revision;
} PPBinaryHeader, *PPPBinaryHeader;

typedef struct _PPBinaryValue {
    USHORT TotalSize;
    USHORT NameSize;
    USHORT DataType;
    USHORT DataSize;
    ULONG Flags;
    ULONG Reserved;
} PPBinaryValue, *PPPBinaryValue;

UNICODE_STRING g_ProductOptionsKeyName =
    RTL_CONSTANT_STRING(L"\\Registry\\Machine\\SYSTEM\\CurrentControlSet\\Control\\ProductOptions");

UNICODE_STRING g_ProductPolicyValueName =
    RTL_CONSTANT_STRING(L"ProductPolicy");

UNICODE_STRING g_CiAcpName =
    RTL_CONSTANT_STRING(L"CodeIntegrity-AllowConfigurablePolicy");

UNICODE_STRING g_CiAcpCksName =
    RTL_CONSTANT_STRING(L"CodeIntegrity-AllowConfigurablePolicy-CustomKernelSigners");

static NTSTATUS CkspModifyPolicyBinary(_In_ PUCHAR lpBytes, _In_ ULONG cbBytes) {
    BOOLEAN AllowConfigurablePolicySet = FALSE;
    BOOLEAN AllowConfigurablePolicyCustomKernelSignerSet = FALSE;
    PPPBinaryHeader pHeader = (PPPBinaryHeader)lpBytes;
    PUCHAR EndPtr = lpBytes + cbBytes;
    PPPBinaryValue pVal;
    
    if (cbBytes < sizeof(PPBinaryHeader) ||
        cbBytes != pHeader->TotalSize ||
        cbBytes != sizeof(PPBinaryHeader) + sizeof(ULONG) + pHeader->DataSize) 
    {
        return STATUS_INFO_LENGTH_MISMATCH;
    }

    EndPtr -= sizeof(ULONG);
    if (*(PULONG)EndPtr != 0x45)    // Product policy end-mark
        return STATUS_INVALID_PARAMETER;

    for (pVal = (PPPBinaryValue)(pHeader + 1); (PUCHAR)pVal + sizeof(PPBinaryValue) < EndPtr; pVal = (PPPBinaryValue)((PUCHAR)pVal + pVal->TotalSize)) {
        PWSTR pValName;
        PVOID pValData;

        if (pVal->NameSize % 2 != 0)
            return STATUS_INVALID_PARAMETER;

        pValName = (PWSTR)(pVal + 1);
        pValData = (PUCHAR)pValName + pVal->NameSize;

        if ((PUCHAR)pValData + pVal->DataSize > EndPtr)
            return STATUS_INVALID_PARAMETER;
        
        if (AllowConfigurablePolicySet == FALSE && _wcsnicmp(pValName, L"CodeIntegrity-AllowConfigurablePolicy", pVal->NameSize / 2) == 0) {
            if (pVal->DataType == REG_DWORD && pVal->DataSize == 4) {
                *(PULONG)pValData = 1;
                AllowConfigurablePolicySet = TRUE;
            } else {
                return STATUS_INVALID_PARAMETER;
            }
        } else if (AllowConfigurablePolicyCustomKernelSignerSet == FALSE && _wcsnicmp(pValName, L"CodeIntegrity-AllowConfigurablePolicy-CustomKernelSigners", pVal->NameSize / 2) == 0) {
            if (pVal->DataType == REG_DWORD && pVal->DataSize == 4) {
                *(PULONG)pValData = 1;
                AllowConfigurablePolicyCustomKernelSignerSet = TRUE;
            } else {
                return STATUS_INVALID_PARAMETER;
            }
        }

        if (AllowConfigurablePolicySet && AllowConfigurablePolicyCustomKernelSignerSet)
            break;
    }

    return STATUS_SUCCESS;
}

static NTSTATUS CkspEnableCustomKernelSigners(_In_ PCKSP_WORKER_CONTEXT Context) {
    NTSTATUS Status;
    ULONG ResultLength;

    while (1) {
        Status = ZwQueryValueKey(Context->ProductOptionsKey,
                                 &g_ProductPolicyValueName,
                                 KeyValuePartialInformation,
                                 Context->ProductPolicyValueInfo,
                                 Context->ProductPolicyValueInfoSize,
                                 &ResultLength);

        if (NT_SUCCESS(Status)) {
            break;
        } else if (Status == STATUS_BUFFER_OVERFLOW || Status == STATUS_BUFFER_TOO_SMALL) {
            ExFreePoolWithTag(Context->ProductPolicyValueInfo, 'cksp');
            Context->ProductPolicyValueInfo = 
                (PKEY_VALUE_PARTIAL_INFORMATION)ExAllocatePoolWithTag(PagedPool, ResultLength, 'cksp');
            if (Context->ProductPolicyValueInfo) {
                Context->ProductPolicyValueInfoSize = ResultLength;
            } else {
                Context->ProductPolicyValueInfoSize = 0;
                Status = STATUS_NO_MEMORY;
                goto ON_CkspEnableCustomKernelSigners_Error;
            }
        } else {
            goto ON_CkspEnableCustomKernelSigners_Error;
        }
    }

    Status = CkspModifyPolicyBinary(Context->ProductPolicyValueInfo->Data, 
                                    Context->ProductPolicyValueInfo->DataLength);
    if (!NT_SUCCESS(Status))
        goto ON_CkspEnableCustomKernelSigners_Error;

    Status = ExUpdateLicenseData(Context->ProductPolicyValueInfo->DataLength, 
                                 Context->ProductPolicyValueInfo->Data);

ON_CkspEnableCustomKernelSigners_Error:
    return Status;
}

NTSTATUS CkspInitContext(_In_ PCKSP_WORKER_CONTEXT Context, _In_ PDRIVER_OBJECT DriverObject, _In_ PUNICODE_STRING RegistryPath) {
    NTSTATUS Status;

    UNREFERENCED_PARAMETER(DriverObject);

    RtlZeroMemory(Context, sizeof(CKSP_WORKER_CONTEXT));

    {
        Status = RtlDuplicateUnicodeString(RTL_DUPLICATE_UNICODE_STRING_NULL_TERMINATE,
                                           RegistryPath,
                                           &Context->LaunchDriverServiceName);
        if (!NT_SUCCESS(Status))
            goto ON_CkspInitContext_ERROR;
    }

    {
        Status = ZwCreateEvent(&Context->ProductOptionsKeyChangeEventHandle, 
                               EVENT_ALL_ACCESS, 
                               NULL, 
                               SynchronizationEvent, 
                               FALSE);
        if (!NT_SUCCESS(Status)) {
            goto ON_CkspInitContext_ERROR;
        } else {
            ObReferenceObjectByHandle(Context->ProductOptionsKeyChangeEventHandle,
                                      EVENT_ALL_ACCESS,
                                      *ExEventObjectType,
                                      KernelMode,
                                      (PVOID*)&Context->ProductOptionsKeyChangeEventObject,
                                      NULL);
        }
    }

    {
        OBJECT_ATTRIBUTES KeyAttribute;
        InitializeObjectAttributes(&KeyAttribute, 
                                   &g_ProductOptionsKeyName, 
                                   OBJ_CASE_INSENSITIVE, 
                                   NULL, 
                                   NULL);
        Status = ZwOpenKey(&Context->ProductOptionsKey, KEY_READ, &KeyAttribute);
        if (!NT_SUCCESS(Status))
            goto ON_CkspInitContext_ERROR;
    }

    
    {
        ULONG ResultLength = 0;
        KEY_VALUE_PARTIAL_INFORMATION KeyInfo;
        Status = ZwQueryValueKey(Context->ProductOptionsKey,
                                 &g_ProductPolicyValueName,
                                 KeyValuePartialInformation,
                                 &KeyInfo,
                                 sizeof(KeyInfo),
                                 &ResultLength);
        if (Status != STATUS_BUFFER_OVERFLOW && Status != STATUS_BUFFER_TOO_SMALL && Status != STATUS_SUCCESS)
            goto ON_CkspInitContext_ERROR;

        Context->ProductPolicyValueInfo = (PKEY_VALUE_PARTIAL_INFORMATION)ExAllocatePoolWithTag(PagedPool, ResultLength, 'cksp');
        if (Context->ProductPolicyValueInfo == NULL) {
            Status = STATUS_NO_MEMORY;
            goto ON_CkspInitContext_ERROR;
        } else {
            Context->ProductPolicyValueInfoSize = ResultLength;
            Status = STATUS_SUCCESS;
        }
    }

ON_CkspInitContext_ERROR:
    if (!NT_SUCCESS(Status)) 
        CkspClearContext(Context);
    return Status;
}

VOID CkspClearContext(_In_ PCKSP_WORKER_CONTEXT Context) {
    if (Context->ProductPolicyValueInfo) {
        ExFreePoolWithTag(Context->ProductPolicyValueInfo, 'cksp');
        Context->ProductPolicyValueInfo = NULL;
        Context->ProductPolicyValueInfoSize = 0;
    }

    if (Context->ProductOptionsKey) {
        ZwClose(Context->ProductOptionsKey);
        Context->ProductOptionsKey = NULL;
    }

    if (Context->ProductOptionsKeyChangeEventHandle) {
        ObDereferenceObject(Context->ProductOptionsKeyChangeEventObject);
        Context->ProductOptionsKeyChangeEventObject = NULL;

        ZwClose(Context->ProductOptionsKeyChangeEventHandle);
        Context->ProductOptionsKeyChangeEventHandle = NULL;
    }

    if (Context->LaunchDriverServiceName.Buffer) {
        RtlFreeUnicodeString(&Context->LaunchDriverServiceName);
        RtlZeroMemory(&Context->LaunchDriverServiceName, sizeof(UNICODE_STRING));
    }
}

static NTSTATUS CkspMain(_In_ PCKSP_WORKER_CONTEXT Context) {
    NTSTATUS Status;
    ULONG PolicyValueType;
    ULONG ReturnLength;
    ULONG CiAcp;
    ULONG CiAcpCks;
    IO_STATUS_BLOCK IoStatusBlock;

    while (1) {
        //
        // Get status of CodeIntegrity-AllowConfigurablePolicy
        //
        Status = ZwQueryLicenseValue(&g_CiAcpName, &PolicyValueType, &CiAcp, sizeof(CiAcp), &ReturnLength);
        if (!NT_SUCCESS(Status)) {
            goto ON_CpskMain_ERROR;
        }
        if (PolicyValueType != REG_DWORD || ReturnLength != sizeof(ULONG)) {
            Status = STATUS_OBJECT_TYPE_MISMATCH;
            goto ON_CpskMain_ERROR;
        }

        //
        // Get status of CodeIntegrity-AllowConfigurablePolicy-CustomKernelSigners
        //
        Status = ZwQueryLicenseValue(&g_CiAcpCksName, &PolicyValueType, &CiAcpCks, sizeof(CiAcpCks), &ReturnLength);
        if (!NT_SUCCESS(Status)) {
            goto ON_CpskMain_ERROR;
        }
        if (PolicyValueType != REG_DWORD || ReturnLength != sizeof(ULONG)) {
            Status = STATUS_OBJECT_TYPE_MISMATCH;
            goto ON_CpskMain_ERROR;
        }

        //
        // When either CodeIntegrity-AllowConfigurablePolicy or CodeIntegrity-AllowConfigurablePolicy-CustomKernelSigners 
        // is not enable, we need update now;
        //
        if (CiAcp == 0 || CiAcpCks == 0) {
            Status = CkspEnableCustomKernelSigners(Context);
            if (!NT_SUCCESS(Status))
                goto ON_CpskMain_ERROR;
        }
        
        Status = ZwNotifyChangeKey(Context->ProductOptionsKey,
                                   Context->ProductOptionsKeyChangeEventHandle,
                                   NULL,
                                   NULL,
                                   &IoStatusBlock,
                                   REG_NOTIFY_CHANGE_LAST_SET,
                                   FALSE,
                                   NULL,
                                   0,
                                   TRUE);
        if (!NT_SUCCESS(Status))
            goto ON_CpskMain_ERROR;

        KeWaitForSingleObject(Context->ProductOptionsKeyChangeEventObject, Executive, KernelMode, FALSE, NULL);

        if (Context->Action == CkspWorkerActionStop)
            break;
    }

    CkspClearContext(Context);
    return Status;

ON_CpskMain_ERROR:
    CkspDeferUnloadAsPossible(Context);
    CkspClearContext(Context);
    return Status;
}

VOID NTAPI CkspWorker(_In_ PVOID StartContext) {
    PsTerminateSystemThread(CkspMain((PCKSP_WORKER_CONTEXT)StartContext));
}

