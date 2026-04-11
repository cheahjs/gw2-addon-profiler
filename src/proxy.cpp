#include "proxy.h"
#include "hooks.h"
#include <d3d11.h>
#include <string>

namespace D3D11Proxy {

static HMODULE s_real = nullptr;

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

static HMODULE LoadRealSystemDll(const wchar_t* name) {
    wchar_t sysDir[MAX_PATH];
    GetSystemDirectoryW(sysDir, MAX_PATH);
    std::wstring path = std::wstring(sysDir) + L"\\" + name;
    return LoadLibraryW(path.c_str());
}

bool Init() {
    s_real = LoadRealSystemDll(L"d3d11.dll");
    if (!s_real) return false;

    s_realCreateDevice =
        (CreateDevice_t)GetProcAddress(s_real, "D3D11CreateDevice");
    s_realCreateDeviceAndSwapChain =
        (CreateDeviceAndSwapChain_t)GetProcAddress(s_real, "D3D11CreateDeviceAndSwapChain");
    return true;
}

void Shutdown() {
    if (s_real) { FreeLibrary(s_real); s_real = nullptr; }
}

HMODULE GetRealD3D11() { return s_real; }

} // namespace D3D11Proxy

// ── Exported proxy functions ─────────────────────────────────
extern "C" {

HRESULT WINAPI D3D11CreateDevice(
    IDXGIAdapter* pAdapter, D3D_DRIVER_TYPE DriverType, HMODULE Software,
    UINT Flags, const D3D_FEATURE_LEVEL* pFeatureLevels, UINT FeatureLevels,
    UINT SDKVersion, ID3D11Device** ppDevice, D3D_FEATURE_LEVEL* pFeatureLevel,
    ID3D11DeviceContext** ppImmediateContext)
{
    if (!D3D11Proxy::s_realCreateDevice) return E_FAIL;
    return D3D11Proxy::s_realCreateDevice(
        pAdapter, DriverType, Software, Flags,
        pFeatureLevels, FeatureLevels, SDKVersion,
        ppDevice, pFeatureLevel, ppImmediateContext);
}

HRESULT WINAPI D3D11CreateDeviceAndSwapChain(
    IDXGIAdapter* pAdapter, D3D_DRIVER_TYPE DriverType, HMODULE Software,
    UINT Flags, const D3D_FEATURE_LEVEL* pFeatureLevels, UINT FeatureLevels,
    UINT SDKVersion, const DXGI_SWAP_CHAIN_DESC* pSwapChainDesc,
    IDXGISwapChain** ppSwapChain, ID3D11Device** ppDevice,
    D3D_FEATURE_LEVEL* pFeatureLevel, ID3D11DeviceContext** ppImmediateContext)
{
    if (!D3D11Proxy::s_realCreateDeviceAndSwapChain) return E_FAIL;
    HRESULT hr = D3D11Proxy::s_realCreateDeviceAndSwapChain(
        pAdapter, DriverType, Software, Flags,
        pFeatureLevels, FeatureLevels, SDKVersion, pSwapChainDesc,
        ppSwapChain, ppDevice, pFeatureLevel, ppImmediateContext);

    if (SUCCEEDED(hr) && ppSwapChain && *ppSwapChain) {
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
