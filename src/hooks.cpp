#include "hooks.h"
#include "profiler.h"
#include "imgui_impl.h"
#include "ui.h"
#include "log.h"

#include <MinHook.h>
#include <imgui.h>
#include <imgui_impl_win32.h>

extern IMGUI_IMPL_API LRESULT ImGui_ImplWin32_WndProcHandler(
    HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam);

// ── Present hook (via MinHook) ──────────────────────────────

namespace {

using PresentFn       = HRESULT(STDMETHODCALLTYPE*)(IDXGISwapChain*, UINT, UINT);
using ResizeBuffersFn = HRESULT(STDMETHODCALLTYPE*)(IDXGISwapChain*, UINT, UINT, UINT, DXGI_FORMAT, UINT);

PresentFn       g_origPresent       = nullptr;
ResizeBuffersFn g_origResizeBuffers = nullptr;
bool            g_hooked            = false;
bool            g_imguiInitialized  = false;

ID3D11Device*        g_device  = nullptr;
ID3D11DeviceContext* g_context = nullptr;

static HRESULT STDMETHODCALLTYPE HookedPresent(
    IDXGISwapChain* sc, UINT syncInterval, UINT flags)
{
    Hooks::OnPresent(sc);
    return g_origPresent(sc, syncInterval, flags);
}

static HRESULT STDMETHODCALLTYPE HookedResizeBuffers(
    IDXGISwapChain* sc, UINT count, UINT w, UINT h,
    DXGI_FORMAT fmt, UINT fl)
{
    ImGuiImpl::InvalidateRenderTarget();
    return g_origResizeBuffers(sc, count, w, h, fmt, fl);
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
    if (g_hooked) return; // already hooked

    // Read the function pointers from the vtable, then use MinHook
    // to patch the function prologue. This survives vtable overwrites
    // by other proxies (Nexus, arcdps).
    void** vtable = *reinterpret_cast<void***>(swapchain);
    void* pPresent       = vtable[8];
    void* pResizeBuffers = vtable[13];

    Log::Write("Hooking Present/ResizeBuffers via MinHook (SwapChain=%p)", swapchain);
    Log::Write("  Present=%p, ResizeBuffers=%p", pPresent, pResizeBuffers);

    MH_STATUS st;
    st = MH_CreateHook(pPresent, (void*)&HookedPresent,
                       reinterpret_cast<void**>(&g_origPresent));
    Log::Write("  Present hook create: %d", st);
    st = MH_EnableHook(pPresent);
    Log::Write("  Present hook enable: %d", st);

    st = MH_CreateHook(pResizeBuffers, (void*)&HookedResizeBuffers,
                       reinterpret_cast<void**>(&g_origResizeBuffers));
    Log::Write("  ResizeBuffers hook create: %d", st);
    st = MH_EnableHook(pResizeBuffers);
    Log::Write("  ResizeBuffers hook enable: %d", st);

    g_hooked = true;
}

void OnPresent(IDXGISwapChain* swapchain) {
    // One-time ImGui + WndProc init on first Present
    if (!g_imguiInitialized) {
        Log::Write("First Present, initializing ImGui...");
        swapchain->GetDevice(__uuidof(ID3D11Device), (void**)&g_device);
        Log::Write("  Device: %p", g_device);
        if (g_device) g_device->GetImmediateContext(&g_context);
        Log::Write("  Context: %p", g_context);

        DXGI_SWAP_CHAIN_DESC desc{};
        swapchain->GetDesc(&desc);
        Log::Write("  OutputWindow: %p, BufferCount: %u, %ux%u",
                   desc.OutputWindow, desc.BufferCount,
                   desc.BufferDesc.Width, desc.BufferDesc.Height);

        if (g_device && g_context && desc.OutputWindow) {
            ImGuiImpl::Init(swapchain, g_device, g_context, desc.OutputWindow);
            InstallWndProc(desc.OutputWindow);
            g_imguiInitialized = true;
            Log::Write("  ImGui initialized OK");
        } else {
            Log::Write("  ImGui init SKIPPED (missing device/context/window)");
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
    // MinHook hooks are removed globally by MH_DisableHook(MH_ALL_HOOKS) in dllmain
    g_hooked = false;

    if (g_imguiInitialized) {
        ImGuiImpl::Shutdown();
        g_imguiInitialized = false;
    }
    if (g_context) { g_context->Release(); g_context = nullptr; }
    if (g_device)  { g_device->Release();  g_device  = nullptr; }
}

} // namespace Hooks
