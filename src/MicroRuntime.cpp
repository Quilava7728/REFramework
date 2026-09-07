#include "MicroRuntime.hpp"

#include <windows.h>

#include <atomic>
#include <cstdio>
#include <cstdint>

#include <sdk/Application.hpp>
#include <sdk/GameIdentity.hpp>
#include <sdk/REContext.hpp>

namespace mhs3::micro_runtime {

namespace {

using ApplicationEntryFn = void (*)(void*);

ApplicationEntryFn g_begin_rendering_original = nullptr;
std::atomic<sdk::Application::Function*> g_begin_rendering_entry{nullptr};
std::atomic<uint64_t> g_frame_count{0};
std::atomic<uintptr_t> g_last_begin_rendering_entry{0};
std::atomic<uintptr_t> g_last_vm{0};

void on_frame() {
    // Operation Chungus Build #6:
    // Read one already-resolved piece of engine-owned state each frame.
    // No lookup, logging, allocation, mutation of engine state, or locking.
    g_frame_count.fetch_add(1, std::memory_order_relaxed);

    auto* begin_rendering = g_begin_rendering_entry.load(std::memory_order_relaxed);

    if (begin_rendering != nullptr) {
        g_last_begin_rendering_entry.store(
            reinterpret_cast<uintptr_t>(begin_rendering->entry),
            std::memory_order_relaxed
        );
    }

    auto* vm = sdk::VM::get();

    g_last_vm.store(
        reinterpret_cast<uintptr_t>(vm),
        std::memory_order_relaxed
    );
}

void begin_rendering_hook(void* entry) {
    on_frame();
    g_begin_rendering_original(entry);
}

DWORD WINAPI install_begin_rendering_hook(LPVOID) {
    // Give RE Engine time to initialize its type database/application singleton.
    Sleep(5000);

    FILE* log = nullptr;
    fopen_s(&log, "mhs3_micro_runtime.log", "a");

    auto application = sdk::Application::get();

    if (application == nullptr) {
        if (log != nullptr) {
            fprintf(log, "[MHS3 MicroRuntime] BeginRendering hook: via.Application unavailable.\n");
            fclose(log);
        }

        return 0;
    }

    auto begin_rendering = application->get_function("BeginRendering");

    if (begin_rendering == nullptr || begin_rendering->func == nullptr) {
        if (log != nullptr) {
            fprintf(log, "[MHS3 MicroRuntime] BeginRendering hook: entry unavailable.\n");
            fclose(log);
        }

        return 0;
    }

    g_begin_rendering_original = begin_rendering->func;
    g_begin_rendering_entry.store(begin_rendering, std::memory_order_relaxed);
    begin_rendering->func = &begin_rendering_hook;

    if (log != nullptr) {
        fprintf(log, "[MHS3 MicroRuntime] BeginRendering hook installed.\n");
        fclose(log);
    }

    return 0;
}

}

void initialize() {
    FILE* log = nullptr;
    fopen_s(&log, "mhs3_micro_runtime.log", "a");

    if (log != nullptr) {
        const auto game = GetModuleHandleW(nullptr);
        const auto& gi = sdk::GameIdentity::get();

        fprintf(log, "========================================\n");
        fprintf(log, "[MHS3 MicroRuntime] Big Chungus lives.\n");
        fprintf(log, "[MHS3 MicroRuntime] Executable base: 0x%llX\n",
            static_cast<unsigned long long>(reinterpret_cast<uintptr_t>(game)));
        fprintf(log, "[MHS3 MicroRuntime] TDB version: %u\n",
            static_cast<unsigned int>(gi.tdb_ver()));
        fprintf(log, "[MHS3 MicroRuntime] Build #6: scheduling minimal BeginRendering hook.\n");
        fprintf(log, "========================================\n");

        fclose(log);
    }

    const auto thread = CreateThread(
        nullptr,
        0,
        install_begin_rendering_hook,
        nullptr,
        0,
        nullptr
    );

    if (thread != nullptr) {
        CloseHandle(thread);
    }
}

}
