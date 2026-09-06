#include "MicroRuntime.hpp"

#include <windows.h>

#include <cstdio>
#include <cstdint>

#include <sdk/GameIdentity.hpp>

namespace mhs3::micro_runtime {

void initialize() {
    // Keep this deliberately boring.
    // No hooks, no renderer, no SDK initialization, no frame callbacks.

    FILE* log = nullptr;
    fopen_s(&log, "mhs3_micro_runtime.log", "a");

    if (log == nullptr) {
        return;
    }

    const auto game = GetModuleHandleW(nullptr);
    const auto& gi = sdk::GameIdentity::get();

    fprintf(log, "========================================\n");
    fprintf(log, "[MHS3 MicroRuntime] Big Chungus lives.\n");
    fprintf(log, "[MHS3 MicroRuntime] Executable base: 0x%llX\n",
        static_cast<unsigned long long>(reinterpret_cast<uintptr_t>(game)));
    fprintf(log, "[MHS3 MicroRuntime] TDB version: %u\n",
        static_cast<unsigned int>(gi.tdb_ver()));
    fprintf(log, "[MHS3 MicroRuntime] Initialization complete.\n");
    fprintf(log, "========================================\n");

    fclose(log);
}

}
