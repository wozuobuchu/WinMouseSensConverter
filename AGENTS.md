# WinMouseSensConverter Agent Guide

## Project overview

WinMouseSensConverter is a native Windows desktop application written in C++20. It reads low-latency keyboard and mouse data through Raw Input, accumulates horizontal mouse movement, and converts raw counts to a selected distance unit using a reference mouse DPI.

Key behavior:

- Pressing F1 starts or stops recording.
- Starting a recording clears the previous measurement.
- The displayed value is horizontal movement only: right is positive and left is negative.
- Supported output units are raw counts, inches, millimeters, centimeters, decimeters, and meters.
- Keyboard and mouse Raw Input are collected by dedicated message threads. The main UI thread consumes their queues and renders with Direct2D and DirectWrite.
- About and Instruction are modeless dialogs. UI work must not block the main UI loop or either Raw Input thread.
- The executable manifest requires administrator privileges at startup.

## Build policy

Only build and test the `x64` platform. Do not build or troubleshoot the `x86`/`Win32` configurations unless a user explicitly requests it.

Run builds from the repository root:

```powershell
.\build_windows.ps1 -Configuration Debug -Platform x64 -NoRestore
.\build_windows.ps1 -Configuration Release -Platform x64 -NoRestore
```

The normal release artifact is:

```text
x64\Release\WinMouseSensConverter.exe
```

Use `-Clean` only when a clean rebuild is needed. Keep `-NoRestore` for normal local validation after dependencies are installed.

## Build dependencies

- Windows 10 or Windows 11.
- Visual Studio with MSBuild, the MSVC `v145` C++ toolset, and the Windows 10 SDK.
- The Visual Studio "Desktop development with C++" workload supplies the required compiler, resource compiler, linker, Windows headers, Direct2D, DirectWrite, and WRL support.
- PowerShell and `vswhere.exe`; `build_windows.ps1` uses them to locate and initialize the latest suitable Visual Studio installation.
- Boost.Lockfree headers for `boost/lockfree/spsc_queue.hpp`, installed for the `x64-windows` vcpkg triplet.

This repository currently uses classic vcpkg integration and does not contain a `vcpkg.json` manifest. Install and integrate Boost before building if it is missing:

```powershell
vcpkg install boost-lockfree:x64-windows
vcpkg integrate install
```

`d2d1.lib` and `dwrite.lib` are linked from the Windows SDK. The application has no additional third-party runtime library requirement.

## Implementation constraints

- Preserve the dedicated Raw Input message threads and their single-producer/single-consumer queue design.
- Never perform blocking dialogs, waits, file/network operations, or thread joins in menu handlers or paint paths.
- Keep help windows modeless and route their messages through `ui::preprocess_modeless_dialog_message`.
- Keep resource scripts UTF-8 and retain `#pragma code_page(65001)`.
- Validate relevant changes with both Debug and Release x64 builds; do not require x86 validation.
