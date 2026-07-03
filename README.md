
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

# About ElegooSlicer

ElegooSlicer is an open-source slicer compatible with most FDM printers. Currently, ElegooSlicer is undergoing rapid iteration, with more features to be released soon. Please stay tuned for updates. We invite you to [join our Discord](https://discord.com/invite/NkFzP96cMt) and follow the announcement to join the [BETA channel for FDM slicer](https://discord.com/channels/969282195552346202/1370832511042850987) to stay updated on the latest news about ELEGOO products and FDM slicer.


# How to install
**Windows**: 
1.  Download the installer for your preferred version from the [releases page](https://github.com/ELEGOO-3D/ElegooSlicer/releases).
    - *For convenience there is also a portable build available.*
    - *If you have troubles to run the build, you might need to install following runtimes:*
      - [MicrosoftEdgeWebView2RuntimeInstallerX64](https://github.com/SoftFever/OrcaSlicer/releases/download/v1.0.10-sf2/MicrosoftEdgeWebView2RuntimeInstallerX64.exe)
          - [Details of this runtime](https://aka.ms/webview2)
          - [Alternative Download Link Hosted by Microsoft](https://go.microsoft.com/fwlink/p/?LinkId=2124703)
      - [vcredist2019_x64](https://github.com/SoftFever/OrcaSlicer/releases/download/v1.0.10-sf2/vcredist2019_x64.exe)
          -  [Alternative Download Link Hosted by Microsoft](https://aka.ms/vs/17/release/vc_redist.x64.exe)
          -  This file may already be available on your computer if you've installed visual studio.  Check the following location: `%VCINSTALLDIR%Redist\MSVC\v142`

**Mac**:
1. Download the DMG for your computer: `arm64` version for Apple Silicon and `x86_64` for Intel CPU.  
2. Drag ElegooSlicer.app to Application folder. 
3. *If you want to run a build from a PR, you also need to follow the instructions below:*  
    <details quarantine>
    <summary>Details</summary>

    - Option 1 (You only need to do this once. After that the app can be opened normally.):
      - Step 1: Hold _cmd_ and right click the app, from the context menu choose **Open**.
      - Step 2: A warning window will pop up, click _Open_  
      
    - Option 2:  
      Execute this command in terminal: `xattr -dr com.apple.quarantine /Applications/ElegooSlicer.app`
      ```console
          softfever@mac:~$ xattr -dr com.apple.quarantine /Applications/ElegooSlicer.app
      ```
    - Option 3:  
        - Step 1: open the app, a warning window will pop up  
            ![image](./SoftFever_doc/mac_cant_open.png)  
        - Step 2: in `System Settings` -> `Privacy & Security`, click `Open Anyway`:  
            ![image](./SoftFever_doc/mac_security_setting.png)  
    </details>

# How to compile
- Windows 64-bit  
  - Tools needed: Visual Studio 2022, CMake, git, git-lfs, Strawberry Perl.
      - You will require CMake version 3.13 or later (and below 4.0 on Windows), which is available [on their website](https://cmake.org/download/).
      - Strawberry Perl is [available on their GitHub repository](https://github.com/StrawberryPerl/Perl-Dist-Strawberry/releases/).
  - Run `build_release_windows.bat` in `x64 Native Tools Command Prompt for VS 2022`
  - Note: Don't forget to run `git lfs pull` after cloning the repository to download tools on Windows

- Mac 64-bit  
  - Tools needed: Xcode, Cmake, git, gettext, libtool, automake, autoconf, texinfo
      - You can install most of them by running `brew install cmake gettext libtool automake autoconf texinfo`
  - run `build_release_macos.sh`
  - To build and debug in Xcode:
      - run `Xcode.app`
      - open ``build_`arch`/OrcaSlicer.Xcodeproj``
      - menu bar: Product => Scheme => OrcaSlicer
      - menu bar: Product => Scheme => Edit Scheme...
          - Run => Info tab => Build Configuration: `RelWithDebInfo`
          - Run => Options tab => Document Versions: uncheck `Allow debugging when browsing versions`
      - menu bar: Product => Run


# Developer tools

See [doc/DEVELOPER.md](doc/DEVELOPER.md) for developer tools and AI assistant rules.


We recommend joining our Discord BETA channel to provide real-time feedback and engage in discussions. You can also report issues and track progress on GitHub Issues.

<a href="https://github.com/ELEGOO-3D/ElegooSlicer/issues">
    <img alt="GitHub Issues or Pull Requests by label" src="https://img.shields.io/github/issues/ELEGOO-3D/ElegooSlicer/bug">
</a>

# License
ElegooSlicer is licensed under the GNU Affero General Public License, version 3. ElegooSlicer is based on Orca Slicer by SoftFever.

Orca Slicer is licensed under the GNU Affero General Public License, version 3. Orca Slicer is based on Bambu Studio by BambuLab.

Bambu Studio is licensed under the GNU Affero General Public License, version 3. Bambu Studio is based on PrusaSlicer by PrusaResearch.

PrusaSlicer is licensed under the GNU Affero General Public License, version 3. PrusaSlicer is owned by Prusa Research. PrusaSlicer is originally based on Slic3r by Alessandro Ranellucci.

Slic3r is licensed under the GNU Affero General Public License, version 3. Slic3r was created by Alessandro Ranellucci with the help of many other contributors.

The GNU Affero General Public License, version 3 ensures that if you use any part of this software in any way (even behind a web server), your software must be released under the same license.

Orca Slicer includes a pressure advance calibration pattern test adapted from Andrew Ellis' generator, which is licensed under GNU General Public License, version 3. Ellis' generator is itself adapted from a generator developed by Sineos for Marlin, which is licensed under GNU General Public License, version 3.

The Bambu networking plugin is based on non-free libraries from BambuLab. It is optional to the Orca Slicer and provides extended functionalities for Bambulab printer users.

Licensed under the [AGPL-3.0](LICENSE.txt) license.
