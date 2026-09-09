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
    bool prepare_for_resize();
    bool finish_resize();
    void shutdown();

private:
    bool wait_for_gpu();
    bool create_render_targets();
    void destroy_render_targets();

    struct FrameContext {
        Microsoft::WRL::ComPtr<ID3D12CommandAllocator> allocator;
        UINT64 fence_value{};
    };

    Microsoft::WRL::ComPtr<ID3D12Device> m_device;
    Microsoft::WRL::ComPtr<ID3D12CommandQueue> m_command_queue;
    Microsoft::WRL::ComPtr<IDXGISwapChain3> m_swap_chain;

    Microsoft::WRL::ComPtr<ID3D12GraphicsCommandList> m_command_list;
    Microsoft::WRL::ComPtr<ID3D12Fence> m_fence;

    Microsoft::WRL::ComPtr<ID3D12DescriptorHeap> m_rtv_heap;
    Microsoft::WRL::ComPtr<ID3D12DescriptorHeap> m_srv_heap;

    std::vector<FrameContext> m_frames;
    std::vector<Microsoft::WRL::ComPtr<ID3D12Resource>> m_back_buffers;

    UINT m_rtv_descriptor_size{};
    DXGI_FORMAT m_rtv_format{DXGI_FORMAT_UNKNOWN};

    HANDLE m_fence_event{};
    UINT64 m_next_fence_value{1};
    bool m_imgui_initialized{};
    bool m_initialized{};
};

}
