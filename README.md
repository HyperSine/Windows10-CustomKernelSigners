# Windows10 - Custom Kernel Signers

[中文版README](README.zh-CN.md)

## 1. What is Custom Kernel Signers?

We know that Windows10 has strict requirements for kernel mode driver. One of the requirements is that drivers must be signed by a EV certificate that Microsoft trusts. What's more start from 1607, new drivers must be submitted to Windows Hardware Portal to get signed by Microsoft. For a driver signed by a self-signed certificate, without enabling TestSigning mode, Windows10 still refuses to load it even the self-signed certificate was installed into Windows Certificate Store(`certlm.msc` or `certmgr.msc`). That means Windows10 has a independent certificate store for kernel mode driver.

__Custom Kernel Signers(CKS)__ is a product policy supported by Windows10(may be from 1703). The full product policy name is `CodeIntegrity-AllowConfigurablePolicy-CustomKernelSigners`. It allows users to decide what certificates is trusted or denied in kernel. By the way, this policy may require another policy, `CodeIntegrity-AllowConfigurablePolicy`, enable.

Generally, __CKS__ is disabled by default on any edtions of Windows10 except __Windows10 China Government Edition__. 

If a Windows10 PC meets the following conditions:

1. The product policy `CodeIntegrity-AllowConfigurablePolicy-CustomKernelSigners` is enabled. 
  (May be `CodeIntegrity-AllowConfigurablePolicy` is also required.)

2. SecureBoot is enabled.

one can add a certificate to kernel certificate store if he owns the PC's UEFI Platform Key so that he can lanuch any drivers signed by the certificate on that PC.

If you are interested in looking for other product policies, you can see [this](https://www.geoffchappell.com/notes/windows/license/install.htm).

## 2. How to enable this feature?

### 2.1 Prerequisites

1. You must have administrator privilege.

2. You need a temporary environment whose OS is Windows10 Enterprise or Education.

   Why? Because you need it to execute `ConvertFrom-CIPolicy` in Powershell which cannot be done in other editions of Windows10.

3. You are able to set UEFI Platform Key.

### 2.2 Create certificates and set Platform Key(PK)

Please follow [this](asset/build-your-own-pki.md) to create certificates. After that you will get following files:

```
// self-signed root CA certificate
localhost-root-ca.der
localhost-root-ca.pfx

// kernel mode certificate issued by self-signed root CA
localhost-km.der
localhost-km.pfx

// UEFI Platform Key certificate issued by self-signed root CA
localhost-pk.der
localhost-pk.pfx
```

As for how to set PK in UEFI firmware, please do it yourself because different UEFI firmware has different methods. Here, I only tell you how to do it in VMware.

#### 2.2.1 Set PK in VMware

If your VMware virtual machine's name is `TestVM` and your vm has SecureBoot, there would be two files under your vm's folder: `TestVM.nvram` and `TestVM.vmx`. You can set PK by the following:

1. Close your vm.

2. Delete `TestVM.nvram`. This would reset your vm's UEFI settings next time your vm starts.

3. Open `TestVM.vmx` by a text editor and append the following two lines:

   ```
   uefi.allowAuthBypass = "TRUE"
   uefi.secureBoot.PKDefault.file0 = "localhost-pk.der"
   ```

   The first line allows you manage SecureBoot keys in UEFI firmware.

   The second line will make `localhost-pk.der` in vm's folder as default UEFI PK. If `localhost-pk.der` is not in vm's folder, please specify a full path.

Then start `TestVM` and your PK has been set.

### 2.3 Build kernel code-sign certificate rules

Run Powershell as administrator in Windows10 Enterprise/Education edition.

1. Use `New-CIPolicy` to create new CI (Code Integrity) policy. Please make sure that the OS is not affected with any malware.

   ```powershell
   New-CIPolicy -FilePath SiPolicy.xml -Level RootCertificate -ScanPath C:\windows\System32\
   ```

   It will scan the entire `System32` folder and take some time. If you do not want to scan, you can use [SiPolicy.xml](asset/SiPolicy.xml) I prepared.

2. Use `Add-SignerRule` to add our own kernel code-sign certificate to `SiPolicy.xml`.

   ```powershell
   Add-SignerRule -FilePath .\SiPolicy.xml -CertificatePath .\localhost-km.der -Kernel
   ```

3. Use `ConvertFrom-CIPolicy` to serialize `SiPolicy.xml` and get binary file `SiPolicy.bin`

   ```powershell
   ConvertFrom-CIPolicy -XmlFilePath .\SiPolicy.xml -BinaryFilePath .\SiPolicy.bin
   ```

Now our policy rules has been built. The newly-generated file can be applied to any editions of Windows10 once it is signed by PK certificate. From now on, we don't need Windows10 Enterprise/Education edition.

### 2.4 Sign policy rules and apply policy rules

1. For `SiPolicy.bin`, we should use PK certificate to sign it. If you have Windows SDK, you can sign it by `signtool`.

   ```
   signtool sign /fd sha256 /p7co 1.3.6.1.4.1.311.79.1 /p7 . /f .\localhost-pk.pfx /p <password of localhost-pk.pfx> SiPolicy.bin
   ```

   __Please fill `<password of localhost-pk.pfx>` with password of your `localhost-pk.pfx`.__

   Then you will get `SiPolicy.bin.p7` at current directory.

2. Rename `SiPolicy.bin.p7` to `SiPolicy.p7b` and copy `SiPolicy.p7b` to `EFI\Microsoft\Boot\`

   ```powershell
   # run powershell as administrator
   mv .\SiPolicy.bin.p7 .\SiPolicy.p7b
   mountvol x: /s
   cp .\SiPolicy.p7b X:\EFI\Microsoft\Boot\
   ```

### 2.5 Enable CustomKernelSigners

The variable that controls __CKS__ enable or not is stored in `ProductPolicy` value whose key path is `HKLM\SYSTEM\CurrentControlSet\Control\ProductOptions`.

Although administrators can modify this value, the value will be reset immediately once modified. This is because this value is just a mapping of a varialbe in kernel once kernel is initialized. The only way to modify the variable is to call `ExUpdateLicenseData`. However, this API could only be called in kernel mode or indirectly called by calling `NtQuerySystemInformation` with `SystemPolicyInformation`. Unfortunately, the latter way succeeds only when caller is a protected process.

So we could only modify it when kernel has not finished initialization. Do we have a chance? Yes, Windows Setup Mode can give us a chance.

I've built a program to help us enable __CKS__. The code in under `EnableCustomKernelSigners` folder and the binary executable file `EnableCKS.exe` can be downloaded on [release](https://github.com/HyperSine/Windows10-CustomKernelSigners/releases) page. Of course, you can build it with your own.

Double click `EnableCKS.exe` and you can see

```
[+] Succeeded to open "HKLM\SYSTEM\Setup".
[+] Succeeded to set "CmdLine" value.
[+] Succeeded to set "SetupType" value.

Reboot is required. Are you ready to reboot? [y/N]
```

Type `y` to reboot. Then system will enter Setup Mode. `EnableCKS.exe` will run automaticly and enable the following two policy

```
CodeIntegrity-AllowConfigurablePolicy
CodeIntegrity-AllowConfigurablePolicy-CustomKernelSigners
```

Finally, system will reboot again and go back to normal mode.

### 2.6 Persist CustomKernelSigners

Now you should be able to load drivers signed by `localhost-km.pfx`. But wait for a minute. Within 10 minutes, __CKS__ will be reset to disable by `sppsvc` except when you have Windows10 China Government Edition. Don't worry, it takes effect only next time system starts up.

So we have to load a driver to call `ExUpdateLicenseData` continuously to persist __CKS__. I've built a driver named `ckspdrv.sys` which can be downloaded on [release](https://github.com/HyperSine/Windows10-CustomKernelSigners/releases) page. The code is in `CustomKernelSignersPersistent` folder.

`ckspdrv.sys` is not signed. You must sign it with `localhost-km.pfx` so that it can be loaded into kernel.

```
signtool sign /fd sha256 /ac .\localhost-root-ca.der /f .\localhost-km.pfx /p <password of localhost-km.pfx> /tr http://sha256timestamp.ws.symantec.com/sha256/timestamp ckspdrv.sys
```

__Please fill `<password of localhost-km.pfx>` with password of your `localhost-km.pfx`.__

Then move `ckspdrv.sys` to `c:\windows\system32\drivers` and run `cmd` as administrator:

```
sc create ckspdrv binpath=%windir%\system32\drivers\ckspdrv.sys type=kernel start=auto error=normal
sc start ckspdrv
```

If nothing wrong, `ckspdrv.sys` will be loaded successfully, which also confirms that our policy rules have take effect.

Now you can load any driver signed by `localhost-km.pfx`. Have fun and enjoy~

