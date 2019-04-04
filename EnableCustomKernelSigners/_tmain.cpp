#include <tchar.h>
#include <stdio.h>
#include <windows.h>
#include <winternl.h>
#include "OwnedResource.hpp"
#include "ProductPolicy.hpp"
#include "ProductPolicyParser.hpp"

#pragma comment(lib, "ntdll.lib")

struct GenericHandleTraits {
    using HandleType = HANDLE;
    static inline const HandleType InvalidValue = NULL;
    static constexpr auto& Releasor = CloseHandle;
};

struct RegistryKeyTraits {
    using HandleType = HKEY;
    static inline const HandleType InvalidValue = NULL;
    static constexpr auto& Releasor = RegCloseKey;
};

DWORD EnableCksCheckPrivilege(_In_ HANDLE hToken, _In_ PCTSTR lpszPrivilegeName, _Out_ PBOOL lpEnable) {
    PRIVILEGE_SET ps = { 1, PRIVILEGE_SET_ALL_NECESSARY };

    if (!LookupPrivilegeValue(NULL, lpszPrivilegeName, &ps.Privilege[0].Luid))
        return GetLastError();

    if (!PrivilegeCheck(hToken, &ps, lpEnable))
        return GetLastError();

    return ERROR_SUCCESS;
}

DWORD EnableCksSetPrivilege(_In_ HANDLE hToken, _In_ PCTSTR lpszPrivilegeName, _In_ BOOL bEnable) {
    TOKEN_PRIVILEGES tp = { 1 };

    if (!LookupPrivilegeValue(NULL, lpszPrivilegeName, &tp.Privileges[0].Luid))
        return GetLastError();

    tp.Privileges[0].Attributes = bEnable ? SE_PRIVILEGE_ENABLED : 0;

    if (!AdjustTokenPrivileges(hToken, FALSE, &tp, NULL, 0, NULL))
        return GetLastError();

    return ERROR_SUCCESS;
}

int EnableCksNonSetupMode() {
    LSTATUS Status;
    PROCESS_BASIC_INFORMATION ProcessBasicInfo = {};
    OwnedResource<RegistryKeyTraits> hKey;
    DWORD SetupType;
    DWORD CmdLineSize;
    OwnedResource<CppDynamicArrayTraits<TCHAR>> lpszCmdLine;

    //
    // This call never fails
    //
    NtQueryInformationProcess(GetCurrentProcess(),
                              ProcessBasicInformation,
                              &ProcessBasicInfo,
                              sizeof(PROCESS_BASIC_INFORMATION),
                              NULL);

    CmdLineSize = _sctprintf(TEXT("\"%ls\" \"--setupmode\""), 
                             ProcessBasicInfo.PebBaseAddress->ProcessParameters->ImagePathName.Buffer) + 1;

    lpszCmdLine.TakeOver(new TCHAR[CmdLineSize]());

    _stprintf_s(lpszCmdLine, CmdLineSize, TEXT("\"%ls\" \"--setupmode\""),
                ProcessBasicInfo.PebBaseAddress->ProcessParameters->ImagePathName.Buffer);

    Status = RegOpenKeyEx(HKEY_LOCAL_MACHINE, TEXT("SYSTEM\\Setup"), NULL, KEY_WRITE, hKey.GetAddress());
    if (Status != ERROR_SUCCESS) {
        _tprintf_s(TEXT("[-] Failed to open \"HKLM\\SYSTEM\\Setup\". CODE: 0x%.8X\n"), Status);
        return -1;
    } else {
        _tprintf_s(TEXT("[+] Succeeded to open \"HKLM\\SYSTEM\\Setup\".\n"));
    }

    Status = RegSetValueEx(hKey, TEXT("CmdLine"), NULL, REG_SZ, reinterpret_cast<BYTE*>(lpszCmdLine.Get()), CmdLineSize * sizeof(TCHAR));
    if (Status != ERROR_SUCCESS) {
        _tprintf_s(TEXT("[-] Failed to set \"CmdLine\" value. CODE: 0x%.8X\n"), Status);
        return -1;
    } else {
        _tprintf_s(TEXT("[+] Succeeded to set \"CmdLine\" value.\n"));
    }

    SetupType = 1;
    Status = RegSetValueEx(hKey, TEXT("SetupType"), NULL, REG_DWORD, reinterpret_cast<BYTE*>(&SetupType), sizeof(DWORD));
    if (Status != ERROR_SUCCESS) {
        _tprintf_s(TEXT("[-] Failed to set \"SetupType\" value. CODE: 0x%.8X\n"), Status);
        return -1;
    } else {
        _tprintf_s(TEXT("[+] Succeeded to set \"SetupType\" value.\n"));
    }

    return 0;
}

int EnableCksSetupMode() {
    LSTATUS Status;
    OwnedResource<RegistryKeyTraits> hKey;
    DWORD ValueType;
    DWORD ValueSize;
    ProductPolicy Policy;

    Status = RegOpenKeyEx(HKEY_LOCAL_MACHINE,
                          TEXT("SYSTEM\\CurrentControlSet\\Control\\ProductOptions"),
                          NULL,
                          KEY_READ | KEY_WRITE,
                          hKey.GetAddress());
    if (Status != ERROR_SUCCESS) {
        _tprintf_s(TEXT("[-] Failed to open \"HKLM\\SYSTEM\\CurrentControlSet\\Control\\ProductOptions\". CODE: 0x%.8X\n"), Status);
        return -1;
    } else {
        _tprintf_s(TEXT("[+] Succeeded to open \"HKLM\\SYSTEM\\CurrentControlSet\\Control\\ProductOptions\".\n"));
    }
    
    Status = RegQueryValueEx(hKey, TEXT("ProductPolicy"), NULL, &ValueType, NULL, &ValueSize);
    if (Status != ERROR_SUCCESS) {
        _tprintf_s(TEXT("[-] Failed to get size of \"ProductPolicy\" value. CODE: 0x%.8X\n"), Status);
        return -1;
    } else {
        _tprintf_s(TEXT("[+] Succeeded to get size of \"ProductPolicy\" value. SIZE: %d(0x%.8X)\n"), ValueSize, ValueSize);
    }

    if (ValueType != REG_BINARY) {
        _tprintf_s(TEXT("[-] The type of \"ProductPolicy\" value mismatches. Abort!\n"));
        return -1;
    }

    std::vector<uint8_t> Value(ValueSize);

    Status = RegQueryValueEx(hKey, TEXT("ProductPolicy"), NULL, &ValueType, Value.data(), &ValueSize);
    if (Status != ERROR_SUCCESS) {
        _tprintf_s(TEXT("[-] Failed to get \"ProductPolicy\" value data. CODE: 0x%.8x\n"), Status);
        return -1;
    } else {
        _tprintf_s(TEXT("[+] Succeeded to get \"ProductPolicy\" value data.\n"));
    }

    try {
        Policy = ProductPolicyParser::FromBinary(Value);
    } catch (std::exception& ex) {
        _tprintf_s(TEXT("[-] Failed to parse \"ProductPolicy\" value.\n"
                        "    REASON: %hs\n"),
                   ex.what());
        return -1;
    }

    Policy[L"CodeIntegrity-AllowConfigurablePolicy"].GetData<PolicyValue::TypeOfUInt32>() = 1;
    _tprintf_s(TEXT("[*] Enable CodeIntegrity-AllowConfigurablePolicy\n"));

    Policy[L"CodeIntegrity-AllowConfigurablePolicy-CustomKernelSigners"].GetData<PolicyValue::TypeOfUInt32>() = 1;
    _tprintf_s(TEXT("[*] Enable CodeIntegrity-AllowConfigurablePolicy-CustomKernelSigners\n"));

    try {
        Value = ProductPolicyParser::ToBinary(Policy);
    } catch (std::exception& ex) {
        _tprintf_s(TEXT("[-] Failed to parse Policy to binary.\n"
                        "    REASON: %hs\n"),
                   ex.what());
        return -1;
    }

    Status = RegSetValueEx(hKey, TEXT("ProductPolicy"), NULL, REG_BINARY, Value.data(), static_cast<DWORD>(Value.size()));
    if (Status != ERROR_SUCCESS) {
        _tprintf_s(TEXT("[-] Failed to set \"ProductPolicy\" value, CODE: 0x%.8x\n"), Status);
        return -1;
    } else {
        _tprintf_s(TEXT("[+] Succeeded to set \"ProductPolicy\" value.\n"));
    }

    _tprintf_s(TEXT("[*] Checking......\n"));
    SleepEx(1000, FALSE);

    Status = RegQueryValueEx(hKey, TEXT("ProductPolicy"), NULL, &ValueType, NULL, &ValueSize);
    if (Status != ERROR_SUCCESS) {
        _tprintf_s(TEXT("[-] Failed to get size of \"ProductPolicy\" value. CODE: 0x%.8X\n"), Status);
        return -1;
    }

    std::vector<uint8_t> Value2(ValueSize);

    Status = RegQueryValueEx(hKey, TEXT("ProductPolicy"), NULL, &ValueType, Value2.data(), &ValueSize);
    if (Status != ERROR_SUCCESS) {
        _tprintf_s(TEXT("[-] Failed to get \"ProductPolicy\" value data. CODE: 0x%.8x\n"), Status);
        return -1;
    }

    if (Value.size() == Value2.size() && memcmp(Value.data(), Value2.data(), Value2.size()) == 0) {
        _tprintf_s(TEXT("[+] Checking...... Pass!\n"));
    } else {
        _tprintf_s(TEXT("[-] Checking...... Fail! Are you sure you are in Setup Mode?\n"));
        return -1;
    }

    hKey.Release();

    _tprintf_s(TEXT("[*] Clearing \"CmdLine\" in \"HKLM\\SYSTEM\\Setup\".\n"));

    Status = RegOpenKeyEx(HKEY_LOCAL_MACHINE, TEXT("SYSTEM\\Setup"), NULL, KEY_WRITE, hKey.GetAddress());
    if (Status != ERROR_SUCCESS) {
        _tprintf_s(TEXT("[-] Failed to open \"HKLM\\SYSTEM\\Setup\". CODE: 0x%.8X\n"), Status);
        return -1;
    }

    Status = RegSetValueEx(hKey, TEXT("CmdLine"), NULL, REG_SZ, reinterpret_cast<const BYTE*>(TEXT("")), sizeof(TEXT("")));
    if (Status != ERROR_SUCCESS) {
        _tprintf_s(TEXT("[-] Failed to set \"CmdLine\" value. CODE: 0x%.8X\n"), Status);
        return -1;
    }

    return 0;
}

int EnableCksFinal(int Status, bool SetupMode) {
    if (Status != 0) {
        _tprintf_s(TEXT("\n"
                        "Press Enter to contiue......"));
        _gettchar();
    } else {
        if (SetupMode == false) {
            bool bReboot;

            _tprintf_s(TEXT("\n"));

            while (true) {
                _tprintf_s(TEXT("Reboot is required. Are you ready to reboot? [y/N] "));

                auto c = _gettchar();
                while (c != '\n' && _gettchar() != '\n') {}

                if (c == 'y' || c == 'Y') {
                    bReboot = true;
                    break;
                }
                
                if (c == '\n' || c == 'n' || c == 'N' || c == _TEOF) {
                    bReboot = false;
                    break;
                }

                _tprintf_s(TEXT("Invalid char.\n"));
            }

            if (bReboot) {
                _tprintf_s(TEXT("Rebooting......\n"));
                if (!ExitWindowsEx(EWX_REBOOT, 0)) {
                    _tprintf_s(TEXT("Failed to reboot. CODE: 0x%.8X\n"), GetLastError());
                    _tprintf_s(TEXT("Please reboot by yourself.\n"));
                    Status = -1;
                } else {
                    SleepEx(INFINITE, FALSE);
                }
            } else {
                _tprintf_s(TEXT("Reboot will not take place. Please reboot by yourself.\n"));
            }
        } else {    // when in setup mode
            _tprintf_s(TEXT("Rebooting......\n"));
            InitiateSystemShutdownEx(NULL, NULL, 0, TRUE, TRUE, 0);
            SleepEx(INFINITE, FALSE);
        }
    }

    return Status;
}

int _tmain(int argc, PTSTR argv[]) {
    LSTATUS Status;
    OwnedResource<GenericHandleTraits> hToken;
    BOOL bShutdownPrivilegeEnable;

    if (!OpenProcessToken(GetCurrentProcess(), TOKEN_ALL_ACCESS, hToken.GetAddress())) {
        _tprintf_s(TEXT("[-] Failed to open current process's token. CODE: 0x%.8X\n"), GetLastError());
        return -1;
    }

    Status = EnableCksCheckPrivilege(hToken, SE_SHUTDOWN_NAME, &bShutdownPrivilegeEnable);
    if (Status != ERROR_SUCCESS) {
        _tprintf_s(TEXT("[-] Failed to check " SE_SHUTDOWN_NAME ". CODE: 0x%.8X\n"), Status);
        return -1;
    }

    if (bShutdownPrivilegeEnable == FALSE && (Status = EnableCksSetPrivilege(hToken, SE_SHUTDOWN_NAME, TRUE)) != ERROR_SUCCESS) {
        _tprintf_s(TEXT("[-] Failed to enable " SE_SHUTDOWN_NAME " . CODE: 0x%.8X\n"), Status);
        return -1;
    }

    if (argc == 2 && _tcsicmp(argv[1], TEXT("--setupmode")) == 0) {
        return EnableCksFinal(EnableCksSetupMode(), true);
    } else {
        return EnableCksFinal(EnableCksNonSetupMode(), false);
    }
}

