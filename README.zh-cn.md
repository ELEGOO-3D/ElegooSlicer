
<p align="center">
    <a href="https://discord.com/invite/NkFzP96cMt">
        <img alt="Static Badge" src="https://img.shields.io/badge/Chat%20on%20Discord-%23FFF?style=flat&logo=discord&logoColor=white&color=%235563e9">
    </a>
    <a href="https://discord.com/channels/969282195552346202/1370832511042850987">
        <img alt="Static Badge" src="https://img.shields.io/badge/BETA%20channel%20for%20FDM%20slicer-%23FFF?style=flat&logo=discord&logoColor=white&color=%23FF6000">
    </a>
    <a href="https://github.com/ELEGOO-3D/ElegooSlicer/issues">
        <img alt="GitHub Issues or Pull Requests by label" src="https://img.shields.io/github/issues/ELEGOO-3D/ElegooSlicer/bug">
    </a>
</p>

# 关于 ElegooSlicer

ElegooSlicer 是一款兼容大多数 FDM 打印机的开源切片软件。目前 ElegooSlicer 正在快速迭代中，更多功能即将发布，敬请关注。欢迎[加入我们的 Discord](https://discord.com/invite/NkFzP96cMt)并关注公告，加入 [FDM 切片器 BETA 频道](https://discord.com/channels/969282195552346202/1370832511042850987)以获取 ELEGOO 产品和 FDM 切片器的最新消息。


# 安装方式
**Windows**：
1.  从 [发布页面](https://github.com/ELEGOO-3D/ElegooSlicer/releases) 下载对应版本的安装程序。
    - *为方便使用，也提供便携版。*
    - *如果运行遇到问题，可能需要安装以下运行时：*
      - [MicrosoftEdgeWebView2RuntimeInstallerX64](https://github.com/SoftFever/OrcaSlicer/releases/download/v1.0.10-sf2/MicrosoftEdgeWebView2RuntimeInstallerX64.exe)
          - [运行时详情](https://aka.ms/webview2)
          - [Microsoft 官方下载](https://go.microsoft.com/fwlink/p/?LinkId=2124703)
      - [vcredist2019_x64](https://github.com/SoftFever/OrcaSlicer/releases/download/v1.0.10-sf2/vcredist2019_x64.exe)
          -  [Microsoft 官方下载](https://aka.ms/vs/17/release/vc_redist.x64.exe)
          -  如果已安装 Visual Studio，此文件可能已存在。检查路径：`%VCINSTALLDIR%Redist\MSVC\v142`

**Mac**：
1. 下载对应架构的 DMG：`arm64` 适用于 Apple Silicon，`x86_64` 适用于 Intel CPU。
2. 将 ElegooSlicer.app 拖入 Applications 文件夹。
3. *如需运行 PR 构建版本，还需按以下步骤操作：*
    <details quarantine>
    <summary>详情</summary>

    - 方案 1（只需操作一次，之后可正常打开）：
      - 第一步：按住 _cmd_ 右键点击应用，在菜单中选择 **Open**。
      - 第二步：弹出警告窗口，点击 _Open_

    - 方案 2：
      终端执行：`xattr -dr com.apple.quarantine /Applications/ElegooSlicer.app`
      ```console
          softfever@mac:~$ xattr -dr com.apple.quarantine /Applications/ElegooSlicer.app
      ```
    - 方案 3：
        - 第一步：打开应用，弹出警告窗口
            ![image](./SoftFever_doc/mac_cant_open.png)
        - 第二步：在 `系统设置` -> `隐私与安全性` 中，点击 `仍要打开`：
            ![image](./SoftFever_doc/mac_security_setting.png)
    </details>

# 编译方式
- Windows 64 位
  - 所需工具：Visual Studio 2022、CMake、git、git-lfs、Strawberry Perl。
      - CMake 版本需 3.13 或以上（Windows 上限 4.0），可在[官网](https://cmake.org/download/)下载。
      - Strawberry Perl 可在 [GitHub](https://github.com/StrawberryPerl/Perl-Dist-Strawberry/releases/) 下载。
  - 在 `x64 Native Tools Command Prompt for VS 2022` 中运行 `build_release_windows.bat`
  - 注意：Windows 上克隆仓库后别忘了运行 `git lfs pull` 下载工具

- Mac 64 位
  - 所需工具：Xcode、CMake、git、gettext、libtool、automake、autoconf、texinfo
      - 可通过 `brew install cmake gettext libtool automake autoconf texinfo` 安装大部分
  - 运行 `build_release_macos.sh`
  - 在 Xcode 中构建和调试：
      - 运行 `Xcode.app`
      - 打开 ``build_`arch`/OrcaSlicer.Xcodeproj``
      - 菜单栏：Product => Scheme => OrcaSlicer
      - 菜单栏：Product => Scheme => Edit Scheme...
          - Run => Info 标签 => Build Configuration: `RelWithDebInfo`
          - Run => Options 标签 => Document Versions: 取消勾选 `Allow debugging when browsing versions`
      - 菜单栏：Product => Run


# 开发者工具

详见 [doc/DEVELOPER.zh-cn.md](doc/DEVELOPER.zh-cn.md)。


# 问题反馈

欢迎加入我们的 Discord BETA 频道进行实时反馈和讨论，也可在 GitHub Issues 上报和跟踪问题。

<a href="https://github.com/ELEGOO-3D/ElegooSlicer/issues">
    <img alt="GitHub Issues or Pull Requests by label" src="https://img.shields.io/github/issues/ELEGOO-3D/ElegooSlicer/bug">
</a>

# 许可证

ElegooSlicer 基于 GNU Affero General Public License, version 3 许可。ElegooSlicer 基于 SoftFever 的 Orca Slicer。

Orca Slicer 基于 GNU Affero General Public License, version 3 许可。Orca Slicer 基于 BambuLab 的 Bambu Studio。

Bambu Studio 基于 GNU Affero General Public License, version 3 许可。Bambu Studio 基于 PrusaResearch 的 PrusaSlicer。

PrusaSlicer 基于 GNU Affero General Public License, version 3 许可。PrusaSlicer 归 Prusa Research 所有。PrusaSlicer 最初基于 Alessandro Ranellucci 的 Slic3r。

Slic3r 基于 GNU Affero General Public License, version 3 许可。Slic3r 由 Alessandro Ranellucci 在众多贡献者的帮助下创建。

GNU Affero General Public License, version 3 确保如果你以任何方式使用本软件的任何部分（包括在 Web 服务器后端），你的软件也必须以相同许可证发布。

Orca Slicer 包含改编自 Andrew Ellis 生成器的压力提前校准测试，该生成器基于 GNU General Public License, version 3 许可。Ellis 的生成器本身改编自 Sineos 为 Marlin 开发的生成器，同样基于 GNU General Public License, version 3 许可。

Bambu 网络插件基于 BambuLab 的非自由库。它对 Orca Slicer 是可选的，为 BambuLab 打印机用户提供扩展功能。

基于 [AGPL-3.0](LICENSE.txt) 许可证。
