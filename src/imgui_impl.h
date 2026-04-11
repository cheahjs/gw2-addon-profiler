#pragma once

#include <windows.h>
#include <dxgi.h>
#include <d3d11.h>

namespace ImGuiImpl {

void Init(IDXGISwapChain* sc, ID3D11Device* dev,
          ID3D11DeviceContext* ctx, HWND hwnd);
void Shutdown();
void NewFrame();
void EndFrame();          // Render + present to back-buffer
void InvalidateRenderTarget();

} // namespace ImGuiImpl
