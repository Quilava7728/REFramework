#include "MicroD3D12Hook.hpp"

#include <windows.h>

namespace mhs3::micro_runtime {

MicroD3D12Hook* MicroD3D12Hook::s_instance = nullptr;

bool MicroD3D12Hook::initialize() {
    if (s_instance != nullptr) {
        return false;
    }

    const auto dxgi_module = LoadLibraryA("dxgi.dll");
    if (dxgi_module == nullptr) {
        return false;
    }

    const auto create_dxgi_factory =
        reinterpret_cast<decltype(CreateDXGIFactory)*>(
            GetProcAddress(dxgi_module, "CreateDXGIFactory")
        );

    if (create_dxgi_factory == nullptr) {
        return false;
    }

    Microsoft::WRL::ComPtr<IDXGIFactory2> factory;

    if (FAILED(create_dxgi_factory(IID_PPV_ARGS(&factory)))) {
        return false;
    }

    auto** factory_vtable =
        *reinterpret_cast<void***>(factory.Get());

    if (factory_vtable == nullptr) {
        return false;
    }

    s_instance = this;

    if (!m_create_swapchain_hook.install(
            &factory_vtable[15],
            reinterpret_cast<void*>(&MicroD3D12Hook::create_swapchain_for_hwnd))) {
        s_instance = nullptr;
        return false;
    }

    return true;
}

void MicroD3D12Hook::shutdown() {
    m_create_swapchain_hook.uninstall();

    m_command_queue.Reset();
    m_swap_chain.Reset();
    m_device.Reset();

    if (s_instance == this) {
        s_instance = nullptr;
    }
}

HRESULT WINAPI MicroD3D12Hook::create_swapchain_for_hwnd(
    IDXGIFactory2* factory,
    IUnknown* device,
    HWND hwnd,
    const DXGI_SWAP_CHAIN_DESC1* desc,
    const DXGI_SWAP_CHAIN_FULLSCREEN_DESC* fullscreen_desc,
    IDXGIOutput* restrict_to_output,
    IDXGISwapChain1** swap_chain) {

    using CreateSwapChainForHwndFn = HRESULT(WINAPI*)(
        IDXGIFactory2*,
        IUnknown*,
        HWND,
        const DXGI_SWAP_CHAIN_DESC1*,
        const DXGI_SWAP_CHAIN_FULLSCREEN_DESC*,
        IDXGIOutput*,
        IDXGISwapChain1**
    );

    auto* self = s_instance;

    if (self == nullptr) {
        return E_FAIL;
    }

    const auto original =
        self->m_create_swapchain_hook.original<CreateSwapChainForHwndFn>();

    if (original == nullptr) {
        return E_FAIL;
    }

    const auto result = original(
        factory,
        device,
        hwnd,
        desc,
        fullscreen_desc,
        restrict_to_output,
        swap_chain
    );

    if (FAILED(result) || swap_chain == nullptr || *swap_chain == nullptr) {
        return result;
    }

    Microsoft::WRL::ComPtr<ID3D12CommandQueue> command_queue;

    if (SUCCEEDED(device->QueryInterface(IID_PPV_ARGS(&command_queue)))) {
        self->m_command_queue = command_queue;
    }

    Microsoft::WRL::ComPtr<IDXGISwapChain3> swap_chain3;

    if (SUCCEEDED((*swap_chain)->QueryInterface(IID_PPV_ARGS(&swap_chain3)))) {
        self->m_swap_chain = swap_chain3;

        Microsoft::WRL::ComPtr<ID3D12Device> d3d12_device;

        if (SUCCEEDED(swap_chain3->GetDevice(IID_PPV_ARGS(&d3d12_device)))) {
            self->m_device = d3d12_device;
        }
    }

    return result;
}

}
