#include "hooks.h"
#include "profiler.h"
#include "imgui_impl.h"
#include "ui.h"

#include <imgui.h>
#include <imgui_impl_win32.h>

extern IMGUI_IMPL_API LRESULT ImGui_ImplWin32_WndProcHandler(
    HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam);

// ── Present vtable hook ──────────────────────────────────────

namespace {

using PresentFn       = HRESULT(STDMETHODCALLTYPE*)(IDXGISwapChain*, UINT, UINT);
using ResizeBuffersFn = HRESULT(STDMETHODCALLTYPE*)(IDXGISwapChain*, UINT, UINT, UINT, DXGI_FORMAT, UINT);

void** g_vtable              = nullptr;
void*  g_origPresent         = nullptr;
void*  g_origResizeBuffers   = nullptr;
bool   g_imguiInitialized    = false;

ID3D11Device*        g_device  = nullptr;
ID3D11DeviceContext* g_context = nullptr;

static HRESULT STDMETHODCALLTYPE HookedPresent(
    IDXGISwapChain* sc, UINT syncInterval, UINT flags)
{
    Hooks::OnPresent(sc);
    return ((PresentFn)g_origPresent)(sc, syncInterval, flags);
}

static HRESULT STDMETHODCALLTYPE HookedResizeBuffers(
    IDXGISwapChain* sc, UINT count, UINT w, UINT h,
    DXGI_FORMAT fmt, UINT fl)
{
    ImGuiImpl::InvalidateRenderTarget();
    return ((ResizeBuffersFn)g_origResizeBuffers)(sc, count, w, h, fmt, fl);
}

void PatchVTable(void** vtable, int index, void* hook, void** origOut) {
    *origOut = vtable[index];
    DWORD oldProt;
    VirtualProtect(&vtable[index], sizeof(void*), PAGE_READWRITE, &oldProt);
    vtable[index] = hook;
    VirtualProtect(&vtable[index], sizeof(void*), oldProt, &oldProt);
}

void RestoreVTable(void** vtable, int index, void* orig) {
    if (!vtable || !orig) return;
    DWORD oldProt;
    VirtualProtect(&vtable[index], sizeof(void*), PAGE_READWRITE, &oldProt);
    vtable[index] = orig;
    VirtualProtect(&vtable[index], sizeof(void*), oldProt, &oldProt);
}

// ── WndProc hook ─────────────────────────────────────────────

HWND    g_hwnd           = nullptr;
WNDPROC g_origWndProc    = nullptr;

LRESULT CALLBACK HookedWndProc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp) {
    if (ImGui_ImplWin32_WndProcHandler(hwnd, msg, wp, lp))
        return TRUE;
    return CallWindowProcW(g_origWndProc, hwnd, msg, wp, lp);
}

} // anon namespace

// ── Public API ───────────────────────────────────────────────

namespace Hooks {

void OnSwapChainCreated(IDXGISwapChain* swapchain) {
    if (g_vtable) return; // already hooked

    g_vtable = *reinterpret_cast<void***>(swapchain);
    PatchVTable(g_vtable, 8,  (void*)&HookedPresent,       &g_origPresent);
    PatchVTable(g_vtable, 13, (void*)&HookedResizeBuffers,  &g_origResizeBuffers);
}

void OnPresent(IDXGISwapChain* swapchain) {
    // One-time ImGui + WndProc init on first Present
    if (!g_imguiInitialized) {
        swapchain->GetDevice(__uuidof(ID3D11Device), (void**)&g_device);
        if (g_device) g_device->GetImmediateContext(&g_context);

        DXGI_SWAP_CHAIN_DESC desc{};
        swapchain->GetDesc(&desc);

        if (g_device && g_context && desc.OutputWindow) {
            ImGuiImpl::Init(swapchain, g_device, g_context, desc.OutputWindow);
            InstallWndProc(desc.OutputWindow);
            g_imguiInitialized = true;
        }
    }

    if (!g_imguiInitialized) return;

    Profiler::OnFrameTick();

    ImGuiImpl::NewFrame();
    UI::Render();
    ImGuiImpl::EndFrame();
}

void InstallWndProc(HWND hwnd) {
    if (g_hwnd) return;
    g_hwnd = hwnd;
    g_origWndProc = (WNDPROC)SetWindowLongPtrW(hwnd, GWLP_WNDPROC, (LONG_PTR)HookedWndProc);
}

void RemoveWndProc() {
    if (!g_hwnd) return;
    SetWindowLongPtrW(g_hwnd, GWLP_WNDPROC, (LONG_PTR)g_origWndProc);
    g_origWndProc = nullptr;
    g_hwnd = nullptr;
}

void RemovePresentHook() {
    if (!g_vtable) return;
    RestoreVTable(g_vtable, 8,  g_origPresent);
    RestoreVTable(g_vtable, 13, g_origResizeBuffers);
    g_vtable = nullptr;
    g_origPresent = nullptr;
    g_origResizeBuffers = nullptr;

    if (g_imguiInitialized) {
        ImGuiImpl::Shutdown();
        g_imguiInitialized = false;
    }
    if (g_context) { g_context->Release(); g_context = nullptr; }
    if (g_device)  { g_device->Release();  g_device  = nullptr; }
}

} // namespace Hooks
