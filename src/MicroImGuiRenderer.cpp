#include <Windows.h>

#include "MicroImGuiRenderer.hpp"

#include "imgui.h"
#include <imgui_freetype.h>
#include "re2-imgui/imgui_impl_dx12.h"
#include "re2-imgui/imgui_impl_win32.h"

namespace mhs3::micro_runtime {

bool MicroImGuiRenderer::wait_for_gpu() {
    if (m_command_queue == nullptr ||
        m_fence == nullptr ||
        m_fence_event == nullptr) {
        return true;
    }

    const UINT64 signal_value = m_next_fence_value++;

    if (FAILED(m_command_queue->Signal(
            m_fence.Get(),
            signal_value))) {
        return false;
    }

    if (m_fence->GetCompletedValue() < signal_value) {
        if (FAILED(m_fence->SetEventOnCompletion(
                signal_value,
                m_fence_event))) {
            return false;
        }

        if (WaitForSingleObject(
                m_fence_event,
                INFINITE) != WAIT_OBJECT_0) {
            return false;
        }
    }

    for (auto& frame : m_frames) {
        frame.fence_value = 0;
    }

    return true;
}

bool MicroImGuiRenderer::create_render_targets() {
    DXGI_SWAP_CHAIN_DESC desc{};

    if (FAILED(m_swap_chain->GetDesc(&desc)) ||
        desc.BufferCount == 0) {
        return false;
    }

    m_rtv_format = desc.BufferDesc.Format;
    m_back_buffers.resize(desc.BufferCount);

    D3D12_DESCRIPTOR_HEAP_DESC rtv_desc{};
    rtv_desc.Type = D3D12_DESCRIPTOR_HEAP_TYPE_RTV;
    rtv_desc.NumDescriptors = desc.BufferCount;
    rtv_desc.Flags = D3D12_DESCRIPTOR_HEAP_FLAG_NONE;

    if (FAILED(m_device->CreateDescriptorHeap(
            &rtv_desc,
            IID_PPV_ARGS(&m_rtv_heap)))) {
        return false;
    }

    m_rtv_descriptor_size =
        m_device->GetDescriptorHandleIncrementSize(
            D3D12_DESCRIPTOR_HEAP_TYPE_RTV
        );

    auto rtv_handle =
        m_rtv_heap->GetCPUDescriptorHandleForHeapStart();

    for (UINT i = 0; i < desc.BufferCount; ++i) {
        if (FAILED(m_swap_chain->GetBuffer(
                i,
                IID_PPV_ARGS(&m_back_buffers[i])))) {
            destroy_render_targets();
            return false;
        }

        m_device->CreateRenderTargetView(
            m_back_buffers[i].Get(),
            nullptr,
            rtv_handle
        );

        rtv_handle.ptr += m_rtv_descriptor_size;
    }

    return true;
}

void MicroImGuiRenderer::destroy_render_targets() {
    m_back_buffers.clear();
    m_rtv_heap.Reset();

    m_rtv_descriptor_size = 0;
    m_rtv_format = DXGI_FORMAT_UNKNOWN;
}

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

    if (!create_render_targets()) {
        shutdown();
        return false;
    }

    D3D12_DESCRIPTOR_HEAP_DESC srv_desc{};
    srv_desc.Type = D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV;
    srv_desc.NumDescriptors = 1;
    srv_desc.Flags = D3D12_DESCRIPTOR_HEAP_FLAG_SHADER_VISIBLE;

    if (FAILED(m_device->CreateDescriptorHeap(
            &srv_desc,
            IID_PPV_ARGS(&m_srv_heap)))) {
        shutdown();
        return false;
    }

    IMGUI_CHECKVERSION();
    ImGui::CreateContext();

    auto& io = ImGui::GetIO();
    io.IniFilename = nullptr;
    io.LogFilename = nullptr;

    // Chungus deserves his rabbit. 🐇
    io.Fonts->FontLoader = ImGuiFreeType::GetFontLoader();
    io.Fonts->AddFontDefault();

    ImFontConfig emoji_config{};
    emoji_config.MergeMode = true;
    emoji_config.FontLoaderFlags |=
        ImGuiFreeTypeLoaderFlags_LoadColor |
        ImGuiFreeTypeLoaderFlags_Bitmap;

    static const ImWchar rabbit_ranges[] = {
        0x1F407, 0x1F407,
        0
    };

    io.Fonts->AddFontFromFileTTF(
        "reframework/fonts/NotoColorEmoji.ttf",
        16.0f,
        &emoji_config,
        rabbit_ranges
    );

    if (!ImGui_ImplWin32_Init(desc.OutputWindow)) {
        ImGui::DestroyContext();
        shutdown();
        return false;
    }

    ImGui_ImplDX12_InitInfo init_info{};
    init_info.Device = m_device.Get();
    init_info.CommandQueue = m_command_queue.Get();
    init_info.NumFramesInFlight = static_cast<int>(desc.BufferCount);
    init_info.RTVFormat = m_rtv_format;
    init_info.DSVFormat = DXGI_FORMAT_UNKNOWN;
    init_info.SrvDescriptorHeap = m_srv_heap.Get();
    init_info.LegacySingleSrvCpuDescriptor =
        m_srv_heap->GetCPUDescriptorHandleForHeapStart();
    init_info.LegacySingleSrvGpuDescriptor =
        m_srv_heap->GetGPUDescriptorHandleForHeapStart();

    if (!ImGui_ImplDX12_Init(&init_info)) {
        ImGui_ImplWin32_Shutdown();
        ImGui::DestroyContext();
        shutdown();
        return false;
    }

    m_imgui_initialized = true;
    m_initialized = true;
    return true;
}

bool MicroImGuiRenderer::render_frame() {
    if (!m_initialized || !m_imgui_initialized) {
        return false;
    }

    const UINT index =
        m_swap_chain->GetCurrentBackBufferIndex();

    if (index >= m_frames.size() ||
        index >= m_back_buffers.size()) {
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

    ImGui_ImplDX12_NewFrame();
    ImGui_ImplWin32_NewFrame();
    ImGui::NewFrame();

    static bool menu_open = true;
    static bool test_checkbox = false;
    static int test_slider = 50;

    if ((GetAsyncKeyState(VK_INSERT) & 1) != 0) {
        menu_open = !menu_open;
    }

    if (menu_open) {
        ImGui::SetNextWindowSize(
            ImVec2(300.0f, 150.0f),
            ImGuiCond_FirstUseEver
        );

        ImGui::Begin("Operation Chungus 🐇");
        ImGui::TextUnformatted("Build #13D-2");
        ImGui::TextUnformatted("UI alive");
        ImGui::Checkbox("Test checkbox", &test_checkbox);
        ImGui::SliderInt(
            "Test slider",
            &test_slider,
            0,
            100
        );
        ImGui::End();
    }

    ImGui::Render();

    auto* back_buffer =
        m_back_buffers[index].Get();

    D3D12_RESOURCE_BARRIER barrier{};
    barrier.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
    barrier.Transition.pResource = back_buffer;
    barrier.Transition.Subresource =
        D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
    barrier.Transition.StateBefore =
        D3D12_RESOURCE_STATE_PRESENT;
    barrier.Transition.StateAfter =
        D3D12_RESOURCE_STATE_RENDER_TARGET;

    m_command_list->ResourceBarrier(1, &barrier);

    auto rtv =
        m_rtv_heap->GetCPUDescriptorHandleForHeapStart();

    rtv.ptr +=
        static_cast<SIZE_T>(index) *
        m_rtv_descriptor_size;

    m_command_list->OMSetRenderTargets(
        1,
        &rtv,
        FALSE,
        nullptr
    );

    ID3D12DescriptorHeap* heaps[] = {
        m_srv_heap.Get()
    };

    m_command_list->SetDescriptorHeaps(
        1,
        heaps
    );

    ImGui_ImplDX12_RenderDrawData(
        ImGui::GetDrawData(),
        m_command_list.Get()
    );

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

bool MicroImGuiRenderer::prepare_for_resize() {
    if (!m_initialized) {
        return true;
    }

    if (!wait_for_gpu()) {
        return false;
    }

    destroy_render_targets();
    return true;
}

bool MicroImGuiRenderer::finish_resize() {
    if (!m_initialized) {
        return true;
    }

    DXGI_SWAP_CHAIN_DESC desc{};

    if (FAILED(m_swap_chain->GetDesc(&desc)) || desc.BufferCount == 0) {
        return false;
    }

    m_frames.clear();
    m_frames.resize(desc.BufferCount);

    for (UINT i = 0; i < desc.BufferCount; ++i) {
        if (FAILED(m_device->CreateCommandAllocator(
                D3D12_COMMAND_LIST_TYPE_DIRECT,
                IID_PPV_ARGS(&m_frames[i].allocator)))) {
            m_frames.clear();
            return false;
        }
    }

    return create_render_targets();
}

void MicroImGuiRenderer::shutdown() {
    wait_for_gpu();

    if (m_imgui_initialized) {
        ImGui_ImplDX12_Shutdown();
        ImGui_ImplWin32_Shutdown();
        ImGui::DestroyContext();
        m_imgui_initialized = false;
    }

    destroy_render_targets();
    m_srv_heap.Reset();

    m_frames.clear();

    m_fence.Reset();
    m_command_list.Reset();

    if (m_fence_event != nullptr) {
        CloseHandle(m_fence_event);
        m_fence_event = nullptr;
    }

    m_swap_chain.Reset();
    m_command_queue.Reset();
    m_device.Reset();

    m_next_fence_value = 1;
    m_initialized = false;
}

}
