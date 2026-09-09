#pragma once

#include "MicroPointerHook.hpp"
#include "MicroVtableHook.hpp"
#include "MicroImGuiRenderer.hpp"

#include <d3d12.h>
#include <dxgi1_4.h>
#include <wrl/client.h>

namespace mhs3::micro_runtime {

class MicroD3D12Hook {
public:
    bool initialize();
    void shutdown();

private:
    static HRESULT WINAPI present(
        IDXGISwapChain3* swap_chain,
        UINT sync_interval,
        UINT flags
    );

    static HRESULT WINAPI create_swapchain_for_hwnd(
        IDXGIFactory2* factory,
        IUnknown* device,
        HWND hwnd,
        const DXGI_SWAP_CHAIN_DESC1* desc,
        const DXGI_SWAP_CHAIN_FULLSCREEN_DESC* fullscreen_desc,
        IDXGIOutput* restrict_to_output,
        IDXGISwapChain1** swap_chain
    );

    static MicroD3D12Hook* s_instance;

    MicroPointerHook m_create_swapchain_hook;
    MicroVtableHook m_swapchain_hook;
    MicroImGuiRenderer m_renderer;

    Microsoft::WRL::ComPtr<ID3D12Device> m_device;
    Microsoft::WRL::ComPtr<IDXGISwapChain3> m_swap_chain;
    Microsoft::WRL::ComPtr<ID3D12CommandQueue> m_command_queue;
};

}
