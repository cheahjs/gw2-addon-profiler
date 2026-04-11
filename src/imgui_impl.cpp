#include "imgui_impl.h"

#include <imgui.h>
#include <imgui_impl_win32.h>
#include <imgui_impl_dx11.h>

namespace ImGuiImpl {

static ImGuiContext*          s_ctx    = nullptr;
static ID3D11RenderTargetView* s_rtv  = nullptr;
static IDXGISwapChain*        s_sc    = nullptr;
static ID3D11Device*          s_dev   = nullptr;
static ID3D11DeviceContext*   s_dc    = nullptr;

static void CreateRTV() {
    if (!s_sc || !s_dev) return;
    ID3D11Texture2D* back = nullptr;
    s_sc->GetBuffer(0, __uuidof(ID3D11Texture2D), (void**)&back);
    if (back) {
        s_dev->CreateRenderTargetView(back, nullptr, &s_rtv);
        back->Release();
    }
}

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
    CreateRTV();
}

void Shutdown() {
    if (s_rtv) { s_rtv->Release(); s_rtv = nullptr; }
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
    if (!s_ctx) return;
    ImGui::EndFrame();
    ImGui::Render();
    if (s_rtv && s_dc) {
        s_dc->OMSetRenderTargets(1, &s_rtv, nullptr);
        ImGui_ImplDX11_RenderDrawData(ImGui::GetDrawData());
    }
}

void InvalidateRenderTarget() {
    if (s_rtv) { s_rtv->Release(); s_rtv = nullptr; }
    // Will be recreated next frame
    CreateRTV();
}

} // namespace ImGuiImpl
