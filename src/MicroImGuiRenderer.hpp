#pragma once

#include <d3d12.h>
#include <dxgi1_4.h>
#include <wrl/client.h>

#include <cstdint>
#include <vector>

namespace mhs3::micro_runtime {

class MicroImGuiRenderer {
public:
    bool initialize(
        ID3D12Device* device,
        ID3D12CommandQueue* command_queue,
        IDXGISwapChain3* swap_chain
    );

    bool render_frame();
    void shutdown();

private:
    struct FrameContext {
        Microsoft::WRL::ComPtr<ID3D12CommandAllocator> allocator;
        UINT64 fence_value{};
    };

    Microsoft::WRL::ComPtr<ID3D12Device> m_device;
    Microsoft::WRL::ComPtr<ID3D12CommandQueue> m_command_queue;
    Microsoft::WRL::ComPtr<IDXGISwapChain3> m_swap_chain;

    Microsoft::WRL::ComPtr<ID3D12GraphicsCommandList> m_command_list;
    Microsoft::WRL::ComPtr<ID3D12Fence> m_fence;

    std::vector<FrameContext> m_frames;

    HANDLE m_fence_event{};
    UINT64 m_next_fence_value{1};
    bool m_initialized{};
};

}
