#include "imgui_impl.h"

#include <imgui.h>
#include <imgui_impl_win32.h>
#include <imgui_impl_dx11.h>

namespace ImGuiImpl {

static ImGuiContext*          s_ctx    = nullptr;
static IDXGISwapChain*        s_sc    = nullptr;
static ID3D11Device*          s_dev   = nullptr;
static ID3D11DeviceContext*   s_dc    = nullptr;

void Init(IDXGISwapChain* sc, ID3D11Device* dev,
          ID3D11DeviceContext* ctx, HWND hwnd)
{
    s_sc  = sc;
    s_dev = dev;
    s_dc  = ctx;

    s_ctx = ImGui::CreateContext();
    ImGui::SetCurrentContext(s_ctx);

    ImGuiIO& io = ImGui::GetIO();
    io.IniFilename = nullptr; // no ini persistence for overlay

    ImGui_ImplWin32_Init(hwnd);
    ImGui_ImplDX11_Init(dev, ctx);
}

void Shutdown() {
    ImGui_ImplDX11_Shutdown();
    ImGui_ImplWin32_Shutdown();
    if (s_ctx) { ImGui::DestroyContext(s_ctx); s_ctx = nullptr; }
}

void NewFrame() {
    if (!s_ctx) return;
    ImGui::SetCurrentContext(s_ctx);
    ImGui_ImplDX11_NewFrame();
    ImGui_ImplWin32_NewFrame();
    ImGui::NewFrame();
}

void EndFrame() {
    if (!s_ctx || !s_sc || !s_dev || !s_dc) return;
    ImGui::EndFrame();
    ImGui::Render();

    // Acquire the current back buffer every frame. The swap chain's
    // back buffer can change without ResizeBuffers being called through
    // our hooks (e.g. Nexus managing buffers internally), so a cached
    // RTV can go stale.
    ID3D11Texture2D* back = nullptr;
    s_sc->GetBuffer(0, __uuidof(ID3D11Texture2D), (void**)&back);
    if (back) {
        ID3D11RenderTargetView* rtv = nullptr;
        s_dev->CreateRenderTargetView(back, nullptr, &rtv);
        back->Release();
        if (rtv) {
            s_dc->OMSetRenderTargets(1, &rtv, nullptr);
            ImGui_ImplDX11_RenderDrawData(ImGui::GetDrawData());
            rtv->Release();
        }
    }
}

void InvalidateRenderTarget() {
    // No-op: RTV is now created fresh each frame in EndFrame.
}

} // namespace ImGuiImpl
