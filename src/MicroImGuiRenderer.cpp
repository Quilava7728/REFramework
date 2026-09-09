#include "MicroImGuiRenderer.hpp"

namespace mhs3::micro_runtime {

bool MicroImGuiRenderer::initialize(
    ID3D12Device* device,
    ID3D12CommandQueue* command_queue,
    IDXGISwapChain3* swap_chain) {

    if (m_initialized ||
        device == nullptr ||
        command_queue == nullptr ||
        swap_chain == nullptr) {
        return false;
    }

    DXGI_SWAP_CHAIN_DESC desc{};

    if (FAILED(swap_chain->GetDesc(&desc)) || desc.BufferCount == 0) {
        return false;
    }

    m_device = device;
    m_command_queue = command_queue;
    m_swap_chain = swap_chain;

    m_frames.resize(desc.BufferCount);

    for (UINT i = 0; i < desc.BufferCount; ++i) {
        if (FAILED(m_device->CreateCommandAllocator(
                D3D12_COMMAND_LIST_TYPE_DIRECT,
                IID_PPV_ARGS(&m_frames[i].allocator)))) {
            shutdown();
            return false;
        }
    }

    if (FAILED(m_device->CreateCommandList(
            0,
            D3D12_COMMAND_LIST_TYPE_DIRECT,
            m_frames[0].allocator.Get(),
            nullptr,
            IID_PPV_ARGS(&m_command_list)))) {
        shutdown();
        return false;
    }

    if (FAILED(m_command_list->Close())) {
        shutdown();
        return false;
    }

    if (FAILED(m_device->CreateFence(
            0,
            D3D12_FENCE_FLAG_NONE,
            IID_PPV_ARGS(&m_fence)))) {
        shutdown();
        return false;
    }

    m_fence_event = CreateEventA(
        nullptr,
        FALSE,
        FALSE,
        nullptr
    );

    if (m_fence_event == nullptr) {
        shutdown();
        return false;
    }

    m_initialized = true;
    return true;
}

bool MicroImGuiRenderer::render_frame() {
    if (!m_initialized) {
        return false;
    }

    const UINT index =
        m_swap_chain->GetCurrentBackBufferIndex();

    if (index >= m_frames.size()) {
        return false;
    }

    auto& frame = m_frames[index];

    if (frame.fence_value != 0 &&
        m_fence->GetCompletedValue() < frame.fence_value) {

        if (FAILED(m_fence->SetEventOnCompletion(
                frame.fence_value,
                m_fence_event))) {
            return false;
        }

        if (WaitForSingleObject(
                m_fence_event,
                INFINITE) != WAIT_OBJECT_0) {
            return false;
        }
    }

    if (FAILED(frame.allocator->Reset()) ||
        FAILED(m_command_list->Reset(
            frame.allocator.Get(),
            nullptr))) {
        return false;
    }

    Microsoft::WRL::ComPtr<ID3D12Resource> back_buffer;

    if (FAILED(m_swap_chain->GetBuffer(
            index,
            IID_PPV_ARGS(&back_buffer)))) {
        return false;
    }

    D3D12_RESOURCE_BARRIER barrier{};
    barrier.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
    barrier.Transition.pResource = back_buffer.Get();
    barrier.Transition.Subresource =
        D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
    barrier.Transition.StateBefore =
        D3D12_RESOURCE_STATE_PRESENT;
    barrier.Transition.StateAfter =
        D3D12_RESOURCE_STATE_RENDER_TARGET;

    m_command_list->ResourceBarrier(1, &barrier);

    barrier.Transition.StateBefore =
        D3D12_RESOURCE_STATE_RENDER_TARGET;
    barrier.Transition.StateAfter =
        D3D12_RESOURCE_STATE_PRESENT;

    m_command_list->ResourceBarrier(1, &barrier);

    if (FAILED(m_command_list->Close())) {
        return false;
    }

    ID3D12CommandList* lists[] = {
        m_command_list.Get()
    };

    m_command_queue->ExecuteCommandLists(1, lists);

    const UINT64 signal_value =
        m_next_fence_value++;

    if (FAILED(m_command_queue->Signal(
            m_fence.Get(),
            signal_value))) {
        return false;
    }

    frame.fence_value = signal_value;

    return true;
}

void MicroImGuiRenderer::shutdown() {
    if (m_fence_event != nullptr) {
        CloseHandle(m_fence_event);
        m_fence_event = nullptr;
    }

    m_frames.clear();

    m_fence.Reset();
    m_command_list.Reset();

    m_swap_chain.Reset();
    m_command_queue.Reset();
    m_device.Reset();

    m_next_fence_value = 1;
    m_initialized = false;
}

}
