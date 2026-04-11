#pragma once

#include <windows.h>
#include <dxgi.h>
#include <d3d11.h>

namespace Hooks {

// Called by the D3D11 proxy when the game creates a swap chain.
void OnSwapChainCreated(IDXGISwapChain* swapchain);

// Called from the hooked Present, before the real Present.
void OnPresent(IDXGISwapChain* swapchain);

// Install / remove the WndProc hook on the game window.
void InstallWndProc(HWND hwnd);
void RemoveWndProc();

// Tear down Present vtable hook.
void RemovePresentHook();

} // namespace Hooks
