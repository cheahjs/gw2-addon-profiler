# gw2-addon-profiler

A D3D11 proxy DLL that profiles Guild Wars 2 addon performance using [Tracy](https://github.com/wolfpld/tracy). Instruments callbacks from both arcdps and Nexus addon frameworks.

## Features

- Per-addon callback timing (last, average, peak, count) for arcdps and Nexus addons
- In-game ImGui overlay with frame time graph and per-addon breakdowns
- Tracy profiler integration for detailed timeline analysis
- Chainloading support for coexistence with other D3D11 proxies (arcdps, Nexus)
- Load-order independent: works as a transparent D3D11 proxy

## Installation

Download `gw2_addon_profiler.dll` from the [Releases](https://github.com/cheahjs/gw2-addon-profiler/releases) page.

Place it in your Guild Wars 2 installation directory and rename it to `d3d11.dll`. If you already have a `d3d11.dll` from another addon (arcdps or Nexus), rename the existing one to `d3d11_chainload.dll` (or `d3d11_arcdps.dll` / `d3d11_nexus.dll`) and name the profiler `d3d11.dll`. The profiler will automatically chainload the other proxy.

## Usage

The overlay appears automatically in-game, showing frame time and per-addon performance stats.

To get detailed profiling data, connect the [Tracy profiler client](https://github.com/wolfpld/tracy/releases) to `localhost:8086` while the game is running.

## Building

Requires CMake 3.20+ and Ninja. Dependencies (Tracy, MinHook, ImGui) are fetched automatically.

**MSVC (Windows):**

```
cmake --preset msvc
cmake --build --preset msvc
```

**MinGW cross-compile (Linux):**

```
cmake --preset mingw64
cmake --build --preset mingw64
```

Output: `build/<preset>/gw2_addon_profiler.dll`
