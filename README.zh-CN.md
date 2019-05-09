# Windows10 - Custom Kernel Signers

## 1. 什么是Custom Kernel Signers?

我们知道Windows10对内核代码驱动有着严格的要求，其中一条就是驱动必须被微软认可的EV证书签名，并且自1607版本开始，新驱动还需要被提交到Windows Hardware Portal来获得微软的签名。对于自签名证书签署的驱动，在不开启TestSigning的情况下，即使自签名证书被安装到了Windows证书库（`certlm.msc`或`certmgr.msc`）的受信任区域，驱动仍然无法加载。这说明Windows10针对内核模式代码有独立的可信证书库。

__Custom Kernel Signers(CKS)__ 是Windows10（可能从1703开始）支持的一种产品策略。这个产品策略的全名是`CodeIntegrity-AllowConfigurablePolicy-CustomKernelSigners`，它允许用户自定义内核代码证书，从而使得用户可以摆脱“驱动必须由微软签名”的强制性要求。值得注意的是，这个策略可能依赖另外一个产品策略`CodeIntegrity-AllowConfigurablePolicy`。

在Windows10的所有版本中，__CKS__ 默认是关闭的，除了 __Windows10 中国政府特供版__。

如果一个Windows10 PC满足下列条件：

1. 产品策略`CodeIntegrity-AllowConfigurablePolicy-CustomKernelSigners`是开启的。
   （也许`CodeIntegrity-AllowConfigurablePolicy`也要开启。）

2. SecureBoot也是开启的。

那么任何拥有该PC的UEFI Platform Key的人都可以自定义内核代码证书。这意味着，__在不开启调试模式、不开启TestSigning、不关闭DSE的情况下__，他可以使系统允许自签名驱动的加载。

如果你对Windows的产品策略感兴趣，你可以看[这里](https://www.geoffchappell.com/notes/windows/license/install.htm)。

## 2. 如何开启这个feature?

开启这个feature确实非常麻烦。当然，如果你确实非常想要这个feature那就继续往下看吧。

### 2.1 前提

1. 你必须要有管理员权限。

2. 你需要一个Windows10企业版或教育版的临时系统，这个系统用于执行`ConvertFrom-CIPolicy`命令。

   为什么？因为这个命令只能在Windows10企业版或教育版下运行。

3. 你可以设置UEFI固件的Platform Key（PK）。

### 2.2 创建证书与设置Platform Key(PK)

请跟随[这里](asset/build-your-own-pki.zh-CN.md)来创建证书，之后你应该能得到如下文件：

```
// 自签名根CA证书
localhost-root-ca.der
localhost-root-ca.pfx

// 自签名根CA颁发的内核代码证书
localhost-km.der
localhost-km.pfx

// 自签名根CA颁发的UEFI Platform Key证书
localhost-pk.der
localhost-pk.pfx
```

关于如何设置UEFI固件的PK为自建的PK，不同的主板有不同的方法，请读者自行解决。这里只介绍VMware虚拟机设置UEFI PK的方法。

#### 2.2.1 VMware设置UEFI PK

假设你的虚拟机名为`TestVM`，并且你的虚拟机开启了SecureBoot，那么在你的虚拟机目录下应该有`TestVM.nvram`和`TestVM.vmx`这两个文件。你可以通过如下办法设置`TestVM`的PK：

1. 关闭虚拟机。

2. 删除`TestVM.nvram`。此举会使得虚拟机在下次启动时重置`TestVM`的UEFI设置。

3. 用文本编辑器打开`TestVM.vmx`，在文件后面加上

   ```
   uefi.allowAuthBypass = "TRUE"
   uefi.secureBoot.PKDefault.file0 = "localhost-pk.der"
   ```

   第一句允许你在虚拟机UEFI设置中管理SecureBoot keys。

   第二句指定虚拟机目录下`localhost-pk.der`文件为UEFI的默认PK。如果该文件不在虚拟机目录下，请将文件名替换为全路径。

完成后，启动`TestVM`即可导入PK。

### 2.3 构建内核代码证书规则

在Windows10企业版或教育版的临时系统中以管理员身份运行Powershell。

1. 使用`New-CIPolicy`建立新的CI(Code Integerity) Policy。注意确保临时系统是干净的，不然新的Policy里可能会掺入恶意软件的自签名CA。

   ```powershell
   New-CIPolicy -FilePath SiPolicy.xml -Level RootCertificate -ScanPath C:\windows\System32\
   ```

   此举会扫描整个System32目录，可能会耗费一段时间。如果你不想扫描，你可以用我准备好的[SiPolicy.xml](asset/SiPolicy.xml)文件。

2. 针对新生成的`SiPolicy.xml`，使用`Add-SignerRule`添加我们自己的内核代码证书`localhost-km.der`。

   ```powershell
   Add-SignerRule -FilePath .\SiPolicy.xml -CertificatePath .\localhost-km.der -Kernel
   ```

3. 使用`ConvertFrom-CIPolicy`将`SiPolicy.xml`序列化，得到二进制的`SiPolicy.bin`

   ```powershell
   ConvertFrom-CIPolicy -XmlFilePath .\SiPolicy.xml -BinaryFilePath .\SiPolicy.bin
   ```

至此构建内核代码CA规则就算完成。规则文件在被Platform Key签名后可以被用于任何版本的Windows10系统。后面的话不必使用Windows10企业版或教育版的系统。

### 2.4 签名规则并导入规则。

1. 对于规则文件`SiPolicy.bin`，我们得用UEFI的Platform Key签名。如果你有Windows SDK，你可以用`signtool`签名。

   ```
   signtool sign /fd sha256 /p7co 1.3.6.1.4.1.311.79.1 /p7 . /f .\localhost-pk.pfx /p <localhost-pk.pfx的密码> SiPolicy.bin
   ```

   __注意在`/p`后填写`localhost-pk.pfx`的密码。__
   
   之后你会在当前目录得到`SiPolicy.bin.p7`文件。

2. 将`SiPolicy.bin.p7`文件重名为`SiPolicy.p7b`，并放到`\EFI\Microsoft\Boot\`文件夹下。

   ```powershell
   # 管理员权限开启Powershell
   mv .\SiPolicy.bin.p7 .\SiPolicy.p7b
   mountvol x: /s
   cp .\SiPolicy.p7b X:\EFI\Microsoft\Boot\
   ```

### 2.5 开启CustomKernelSigners

__CKS__ 的开关保存在`HKLM\SYSTEM\CurrentControlSet\Control\ProductOptions`键的`ProductPolicy`值里。

尽管管理员可以修改这个值，但是这个值在被修改后会立即恢复原状。这是因为在内核初始化完后，这个值只是内核里一个变量的映射，只有通过`ExUpdateLicenseData`这个内核API才能修改。而这个API只能在内核里被调用，或者通过`NtQuerySystemInformation`的`SystemPolicyInformation`功能号间接调用。很遗憾的是后者只有Protected Process才能 __成功__ 调用。

所以我们只能在内核还尚未初始化完的时候修改 __CKS__ 开关。有这个机会吗？有，Windows的Setup Mode可以给我们提供这个机会。

我已经写了一个程序来帮助我们打开 __CKS__，代码在`EnableCustomKernelSigners`文件夹下。二进制程序可以在[release](https://github.com/DoubleLabyrinth/Windows10-CustomKernelSigners/releases)中得到，或者你也可以自己编译。

二进制程序是`EnableCKS.exe`，直接双击打开即可。打开后你应该能看到

```
[+] Succeeded to open "HKLM\SYSTEM\Setup".
[+] Succeeded to set "CmdLine" value.
[+] Succeeded to set "SetupType" value.

Reboot is required. Are you ready to reboot? [y/N]
```

输入`y`直接重启，然后系统会进入Setup Mode，`EnableCKS.exe`会自动启动并开启

```
CodeIntegrity-AllowConfigurablePolicy
CodeIntegrity-AllowConfigurablePolicy-CustomKernelSigners
```

这两个策略。最后会自动重启并重新进入到正常模式下。

### 2.6 CustomKernelSigners持久化

重新进入正常模式后，你应该就可以加载由`localhost-km.pfx`签署的驱动了。但是别高兴得太早，大约在10分钟之内，__CKS__ 会被`sppsvc`服务重置为关闭，除非你的Windows10是中国政府特供版。但不用担心，关闭还得等重启后才会实际生效。

所以我们得趁这个机会，加载自己编写的驱动，通过不断调用`ExUpdateLicenseData`来持久化 __CKS__。

这个驱动也可以在[release](https://github.com/DoubleLabyrinth/Windows10-CustomKernelSigners/releases)中得到，二进制文件为`ckspdrv.sys`，相应代码在`CustomKernelSignersPersistent`文件夹下。

这个驱动是没有签名的，你必须完成签名后才能加载。

```
signtool sign /fd sha256 /ac .\localhost-root-ca.der /f .\localhost-km.pfx /p <localhost-km.pfx的密码> /tr http://sha256timestamp.ws.symantec.com/sha256/timestamp ckspdrv.sys
```

__注意在`/p`后填写`localhost-km.pfx`的密码。__

然后将`ckspdrv.sys`放入`c:\windows\system32\drivers`下，管理员启动`cmd`：

```
sc create ckspdrv binpath=%windir%\system32\drivers\ckspdrv.sys type=kernel start=auto error=normal
sc start ckspdrv
```

不出意外的话`ckspdrv.sys`会成功加载，同时也证明我们构建的内核代码证书规则实际生效了。

之后你可以加载其他由`localhost-km.pfx`签署的驱动。

