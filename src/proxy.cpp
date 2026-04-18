#include "proxy.h"
#include "hooks.h"
#include "log.h"
#include <d3d11.h>
#include <dxgi.h>
#include <string>
#include <algorithm>

namespace D3D11Proxy {

static HMODULE s_chainloaded = nullptr; // arcdps / Nexus / other proxy
static HMODULE s_real        = nullptr; // system d3d11.dll (fallback)
static HMODULE s_active      = nullptr; // whichever we're forwarding to

using CreateDevice_t = HRESULT(WINAPI*)(
    IDXGIAdapter*, D3D_DRIVER_TYPE, HMODULE, UINT,
    const D3D_FEATURE_LEVEL*, UINT, UINT,
    ID3D11Device**, D3D_FEATURE_LEVEL*, ID3D11DeviceContext**);

using CreateDeviceAndSwapChain_t = HRESULT(WINAPI*)(
    IDXGIAdapter*, D3D_DRIVER_TYPE, HMODULE, UINT,
    const D3D_FEATURE_LEVEL*, UINT, UINT,
    const DXGI_SWAP_CHAIN_DESC*, IDXGISwapChain**,
    ID3D11Device**, D3D_FEATURE_LEVEL*, ID3D11DeviceContext**);

static CreateDevice_t              s_realCreateDevice = nullptr;
static CreateDeviceAndSwapChain_t  s_realCreateDeviceAndSwapChain = nullptr;

// ── Helpers ──────────────────────────────────────────────────

static HMODULE LoadSystemDll(const wchar_t* name) {
    wchar_t sysDir[MAX_PATH];
    GetSystemDirectoryW(sysDir, MAX_PATH);
    std::wstring path = std::wstring(sysDir) + L"\\" + name;
    return LoadLibraryW(path.c_str());
}

// Get the directory our DLL lives in (= game directory).
static std::wstring GetOwnDirectory() {
    wchar_t path[MAX_PATH];
    HMODULE hm = nullptr;
    GetModuleHandleExW(
        GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS |
        GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
        reinterpret_cast<LPCWSTR>(&GetOwnDirectory), &hm);
    GetModuleFileNameW(hm, path, MAX_PATH);
    std::wstring dir(path);
    auto pos = dir.find_last_of(L'\\');
    return (pos != std::wstring::npos) ? dir.substr(0, pos + 1) : dir;
}

// Try to load a chainload target from the game directory.
// Returns the first DLL found, or nullptr.
static HMODULE TryChainload(const std::wstring& gameDir) {
    // Check for well-known chainload names, in priority order.
    static const wchar_t* candidates[] = {
        L"d3d11_chainload.dll",
        L"d3d11_arcdps.dll",
        L"d3d11_nexus.dll",
    };

    for (auto name : candidates) {
        std::wstring full = gameDir + name;
        HMODULE h = LoadLibraryW(full.c_str());
        if (h) return h;
    }
    return nullptr;
}

// ── Public API ───────────────────────────────────────────────

bool Init() {
    std::wstring gameDir = GetOwnDirectory();
    Log::Write("Game directory: %ls", gameDir.c_str());

    // 1. Try chainloading another d3d11 proxy (arcdps, Nexus, etc.)
    s_chainloaded = TryChainload(gameDir);
    Log::Write("Chainload DLL: %p", s_chainloaded);

    // 2. Always load the real system DLL as well (chainloaded DLL
    //    might need it, and we use it as a fallback).
    s_real = LoadSystemDll(L"d3d11.dll");
    Log::Write("System d3d11.dll: %p", s_real);

    s_active = s_chainloaded ? s_chainloaded : s_real;
    if (!s_active) {
        Log::Write("ERROR: No D3D11 DLL available");
        return false;
    }
    Log::Write("Active D3D11 DLL: %p (chainloaded=%s)", s_active,
               s_chainloaded ? "yes" : "no");

    s_realCreateDevice =
        (CreateDevice_t)GetProcAddress(s_active, "D3D11CreateDevice");
    s_realCreateDeviceAndSwapChain =
        (CreateDeviceAndSwapChain_t)GetProcAddress(s_active, "D3D11CreateDeviceAndSwapChain");
    Log::Write("D3D11CreateDevice: %p", s_realCreateDevice);
    Log::Write("D3D11CreateDeviceAndSwapChain: %p", s_realCreateDeviceAndSwapChain);
    return true;
}

void Shutdown() {
    if (s_chainloaded) { FreeLibrary(s_chainloaded); s_chainloaded = nullptr; }
    if (s_real)        { FreeLibrary(s_real);         s_real        = nullptr; }
    s_active = nullptr;
}

HMODULE GetRealD3D11() { return s_active; }

} // namespace D3D11Proxy

// ── DXGI Factory hook ───────────────────────────────────────
//
// GW2 calls D3D11CreateDevice (not ...AndSwapChain) and creates
// the swap chain separately via IDXGIFactory::CreateSwapChain.
// We hook the factory vtable to catch that.

namespace {

using CreateSwapChainFn = HRESULT(STDMETHODCALLTYPE*)(
    IDXGIFactory*, IUnknown*, DXGI_SWAP_CHAIN_DESC*, IDXGISwapChain**);

void** g_factoryVtable         = nullptr;
void*  g_origCreateSwapChain   = nullptr;

static HRESULT STDMETHODCALLTYPE HookedCreateSwapChain(
    IDXGIFactory* factory, IUnknown* pDevice,
    DXGI_SWAP_CHAIN_DESC* pDesc, IDXGISwapChain** ppSwapChain)
{
    Log::Write("IDXGIFactory::CreateSwapChain called");
    HRESULT hr = ((CreateSwapChainFn)g_origCreateSwapChain)(
        factory, pDevice, pDesc, ppSwapChain);
    Log::Write("  -> HRESULT: 0x%08x", hr);

    if (SUCCEEDED(hr) && ppSwapChain && *ppSwapChain) {
        Log::Write("  -> SwapChain created via DXGI, installing Present hook");
        Hooks::OnSwapChainCreated(*ppSwapChain);
    }
    return hr;
}

static void PatchFactoryVTable(void** vtable, int index, void* hook, void** origOut) {
    *origOut = vtable[index];
    DWORD oldProt;
    VirtualProtect(&vtable[index], sizeof(void*), PAGE_READWRITE, &oldProt);
    vtable[index] = hook;
    VirtualProtect(&vtable[index], sizeof(void*), oldProt, &oldProt);
}

static void HookDXGIFactory(ID3D11Device* device) {
    if (g_factoryVtable) return; // already hooked

    IDXGIDevice* dxgiDevice = nullptr;
    HRESULT hr = device->QueryInterface(__uuidof(IDXGIDevice), (void**)&dxgiDevice);
    if (FAILED(hr) || !dxgiDevice) {
        Log::Write("Failed to get IDXGIDevice: 0x%08x", hr);
        return;
    }

    IDXGIAdapter* adapter = nullptr;
    hr = dxgiDevice->GetAdapter(&adapter);
    dxgiDevice->Release();
    if (FAILED(hr) || !adapter) {
        Log::Write("Failed to get IDXGIAdapter: 0x%08x", hr);
        return;
    }

    IDXGIFactory* factory = nullptr;
    hr = adapter->GetParent(__uuidof(IDXGIFactory), (void**)&factory);
    adapter->Release();
    if (FAILED(hr) || !factory) {
        Log::Write("Failed to get IDXGIFactory: 0x%08x", hr);
        return;
    }

    Log::Write("Hooking IDXGIFactory::CreateSwapChain (factory=%p)", factory);
    g_factoryVtable = *reinterpret_cast<void***>(factory);
    PatchFactoryVTable(g_factoryVtable, 10, (void*)&HookedCreateSwapChain,
                       &g_origCreateSwapChain);
    Log::Write("  CreateSwapChain: orig=%p", g_origCreateSwapChain);

    factory->Release();
}

} // anon namespace

// ── Exported proxy functions ─────────────────────────────────
extern "C" {

HRESULT WINAPI D3D11CreateDevice(
    IDXGIAdapter* pAdapter, D3D_DRIVER_TYPE DriverType, HMODULE Software,
    UINT Flags, const D3D_FEATURE_LEVEL* pFeatureLevels, UINT FeatureLevels,
    UINT SDKVersion, ID3D11Device** ppDevice, D3D_FEATURE_LEVEL* pFeatureLevel,
    ID3D11DeviceContext** ppImmediateContext)
{
    Log::Write("D3D11CreateDevice called (DriverType=%d, Flags=0x%x)", DriverType, Flags);
    if (!D3D11Proxy::s_realCreateDevice) {
        Log::Write("  -> FAIL: no real CreateDevice function");
        return E_FAIL;
    }
    HRESULT hr = D3D11Proxy::s_realCreateDevice(
        pAdapter, DriverType, Software, Flags,
        pFeatureLevels, FeatureLevels, SDKVersion,
        ppDevice, pFeatureLevel, ppImmediateContext);
    Log::Write("  -> HRESULT: 0x%08x", hr);

    if (SUCCEEDED(hr) && ppDevice && *ppDevice) {
        HookDXGIFactory(*ppDevice);
    }
    return hr;
}

HRESULT WINAPI D3D11CreateDeviceAndSwapChain(
    IDXGIAdapter* pAdapter, D3D_DRIVER_TYPE DriverType, HMODULE Software,
    UINT Flags, const D3D_FEATURE_LEVEL* pFeatureLevels, UINT FeatureLevels,
    UINT SDKVersion, const DXGI_SWAP_CHAIN_DESC* pSwapChainDesc,
    IDXGISwapChain** ppSwapChain, ID3D11Device** ppDevice,
    D3D_FEATURE_LEVEL* pFeatureLevel, ID3D11DeviceContext** ppImmediateContext)
{
    Log::Write("D3D11CreateDeviceAndSwapChain called (DriverType=%d, Flags=0x%x)", DriverType, Flags);
    if (!D3D11Proxy::s_realCreateDeviceAndSwapChain) {
        Log::Write("  -> FAIL: no real CreateDeviceAndSwapChain function");
        return E_FAIL;
    }
    HRESULT hr = D3D11Proxy::s_realCreateDeviceAndSwapChain(
        pAdapter, DriverType, Software, Flags,
        pFeatureLevels, FeatureLevels, SDKVersion, pSwapChainDesc,
        ppSwapChain, ppDevice, pFeatureLevel, ppImmediateContext);
    Log::Write("  -> HRESULT: 0x%08x", hr);

    if (SUCCEEDED(hr) && ppSwapChain && *ppSwapChain) {
        Log::Write("  -> SwapChain created, installing Present hook");
        Hooks::OnSwapChainCreated(*ppSwapChain);
    }
    return hr;
}

HRESULT WINAPI D3D11On12CreateDevice(
    IUnknown* pDevice, UINT Flags,
    const D3D_FEATURE_LEVEL* pFeatureLevels, UINT FeatureLevels,
    IUnknown* const* ppCommandQueues, UINT NumQueues, UINT NodeMask,
    ID3D11Device** ppDevice, ID3D11DeviceContext** ppImmediateContext,
    D3D_FEATURE_LEVEL* pChosenFeatureLevel)
{
    using Fn = HRESULT(WINAPI*)(IUnknown*, UINT, const D3D_FEATURE_LEVEL*,
        UINT, IUnknown* const*, UINT, UINT, ID3D11Device**,
        ID3D11DeviceContext**, D3D_FEATURE_LEVEL*);
    auto fn = (Fn)GetProcAddress(D3D11Proxy::GetRealD3D11(),
                                  "D3D11On12CreateDevice");
    if (!fn) return E_FAIL;
    return fn(pDevice, Flags, pFeatureLevels, FeatureLevels,
              ppCommandQueues, NumQueues, NodeMask,
              ppDevice, ppImmediateContext, pChosenFeatureLevel);
}

} // extern "C"
