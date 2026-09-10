#include "MicroRuntime.hpp"
#include "MicroD3D12Hook.hpp"

#include <windows.h>

#include <atomic>
#include <chrono>
#include <cstdio>
#include <cstdint>

#include <hde64.h>
#include <safetyhook.hpp>

#include <sdk/Application.hpp>
#include <sdk/GameIdentity.hpp>
#include <sdk/REContext.hpp>
#include <sdk/RETypeDB.hpp>

namespace mhs3::micro_runtime {

namespace {

using ApplicationEntryFn = void (*)(void*);

ApplicationEntryFn g_begin_rendering_original = nullptr;
std::atomic<sdk::Application::Function*> g_begin_rendering_entry{nullptr};
std::atomic<uint64_t> g_frame_count{0};
std::atomic<uintptr_t> g_last_begin_rendering_entry{0};
std::atomic<uintptr_t> g_last_vm{0};
std::atomic<uintptr_t> g_last_thread_context{0};
std::atomic<uintptr_t> g_last_tdb{0};
std::atomic<uintptr_t> g_last_application_type{0};
std::atomic<uintptr_t> g_last_application_method{0};
std::atomic<float> g_last_max_fps{0.0f};
std::atomic<uintptr_t> g_last_stage_manager_type{0};
std::atomic<uintptr_t> g_last_stage_manager_method{0};
std::atomic<uintptr_t> g_last_stage_manager_instance{0};
std::atomic<uintptr_t> g_last_field_elder_ctrl_field{0};
std::atomic<uintptr_t> g_last_field_elder_ctrl{0};
std::atomic<uintptr_t> g_last_field_elder_param_userdata_field{0};
std::atomic<uintptr_t> g_last_field_elder_param_userdata{0};
std::atomic<bool> g_elder_chain_logged{false};
std::atomic<bool> g_elder_values_logged{false};
std::atomic<bool> g_base_pop_rate_written{false};
std::atomic<bool> g_elder_write_allowed{false};
safetyhook::MidHook g_get_title_text_hook{};
std::atomic<uint32_t> g_slow_elder_probe_log_count{0};
MicroD3D12Hook g_micro_d3d12_hook;

void* get_actual_function(void* possible_fn) {
    if (possible_fn == nullptr) {
        return nullptr;
    }

    auto actual_fn = possible_fn;
    auto ip = reinterpret_cast<uintptr_t>(possible_fn);

    for (auto i = 0; i < 10; ++i) {
        hde64s hde{};
        const auto len = hde64_disasm(
            reinterpret_cast<void*>(ip),
            &hde
        );

        ip += len;

        if (
            hde.opcode == 0xCC ||
            hde.opcode == 0xC3 ||
            hde.opcode == 0xC2
        ) {
            break;
        }

        if (hde.opcode == 0xE9) {
            actual_fn = reinterpret_cast<void*>(
                ip + hde.imm.imm32
            );
            break;
        }
    }

    return actual_fn;
}

void get_title_text_hook(safetyhook::Context&) {
    g_elder_write_allowed.store(
        true,
        std::memory_order_release
    );
}


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

    if (vm != nullptr) {
        auto* thread_context = vm->get_thread_context();

        g_last_thread_context.store(
            reinterpret_cast<uintptr_t>(thread_context),
            std::memory_order_relaxed
        );
    }

    auto* tdb = sdk::RETypeDB::get();

    g_last_tdb.store(
        reinterpret_cast<uintptr_t>(tdb),
        std::memory_order_relaxed
    );

    if (tdb != nullptr) {
        auto* application_type = tdb->find_type("via.Application");

        g_last_application_type.store(
            reinterpret_cast<uintptr_t>(application_type),
            std::memory_order_relaxed
        );

        if (application_type != nullptr) {
            auto* application_method = application_type->get_method("get_MaxFps");

            g_last_application_method.store(
                reinterpret_cast<uintptr_t>(application_method),
                std::memory_order_relaxed
            );

            if (application_method != nullptr) {
                const auto max_fps = application_method->call<float>(sdk::get_thread_context());

                g_last_max_fps.store(
                    max_fps,
                    std::memory_order_relaxed
                );
            }
        }
    }

    const auto elder_probe_begin =
        std::chrono::steady_clock::now();

    if (tdb != nullptr) {
        auto* stage_manager_type =
            tdb->find_type("app.StageManager");

        g_last_stage_manager_type.store(
            reinterpret_cast<uintptr_t>(stage_manager_type),
            std::memory_order_relaxed
        );

        if (stage_manager_type != nullptr) {
            auto* stage_manager_method =
                stage_manager_type->get_method("get_Instance");

            g_last_stage_manager_method.store(
                reinterpret_cast<uintptr_t>(stage_manager_method),
                std::memory_order_relaxed
            );

            if (stage_manager_method != nullptr) {
                auto* stage_manager =
                    stage_manager_method->call<::REManagedObject*>(
                        sdk::get_thread_context()
                    );

                g_last_stage_manager_instance.store(
                    reinterpret_cast<uintptr_t>(stage_manager),
                    std::memory_order_relaxed
                );

                if (stage_manager != nullptr) {
                    auto* field_elder_ctrl_field =
                        stage_manager_type->get_field("_FieldElderCtrl");

                    g_last_field_elder_ctrl_field.store(
                        reinterpret_cast<uintptr_t>(field_elder_ctrl_field),
                        std::memory_order_relaxed
                    );

                    if (field_elder_ctrl_field != nullptr) {
                        auto* field_elder_ctrl =
                            field_elder_ctrl_field->get_data<::REManagedObject*>(
                                stage_manager
                            );

                        g_last_field_elder_ctrl.store(
                            reinterpret_cast<uintptr_t>(field_elder_ctrl),
                            std::memory_order_relaxed
                        );

                        if (field_elder_ctrl != nullptr) {
                            auto* field_elder_ctrl_type =
                                field_elder_ctrl->get_type_definition();

                            if (field_elder_ctrl_type != nullptr) {
                                auto* field_elder_param_userdata_field =
                                    field_elder_ctrl_type->get_field(
                                        "_FieldElderParamUserData"
                                    );

                                g_last_field_elder_param_userdata_field.store(
                                    reinterpret_cast<uintptr_t>(
                                        field_elder_param_userdata_field
                                    ),
                                    std::memory_order_relaxed
                                );

                                if (field_elder_param_userdata_field != nullptr) {
                                    auto* field_elder_param_userdata =
                                        field_elder_param_userdata_field
                                            ->get_data<::REManagedObject*>(
                                                field_elder_ctrl
                                            );

                                    g_last_field_elder_param_userdata.store(
                                        reinterpret_cast<uintptr_t>(
                                            field_elder_param_userdata
                                        ),
                                        std::memory_order_relaxed
                                    );

                                    if (field_elder_param_userdata != nullptr) {
                                        auto* field_elder_param_userdata_type =
                                            field_elder_param_userdata->get_type_definition();

                                        if (field_elder_param_userdata_type != nullptr) {
                                            auto* base_pop_rate_field =
                                                field_elder_param_userdata_type->get_field(
                                                    "BasePopRate"
                                                );

                                            auto* elder_end_battle_count_field =
                                                field_elder_param_userdata_type->get_field(
                                                    "ElderEndBattleCount"
                                                );

                                            if (
                                                base_pop_rate_field != nullptr &&
                                                elder_end_battle_count_field != nullptr
                                            ) {
                                                const auto base_pop_rate =
                                                    base_pop_rate_field->get_data<int32_t>(
                                                        field_elder_param_userdata
                                                    );

                                                const auto elder_end_battle_count =
                                                    elder_end_battle_count_field->get_data<int32_t>(
                                                        field_elder_param_userdata
                                                    );

                                                bool write_expected = false;

                                                if (
                                                  g_elder_write_allowed.load(
                                                      std::memory_order_acquire
                                                  ) &&
                                                  g_base_pop_rate_written.compare_exchange_strong(
                                                      write_expected,
                                                      true,
                                                      std::memory_order_relaxed
                                                  )
                                              ) {
                                                    auto& base_pop_rate_ref =
                                                        base_pop_rate_field->get_data<int32_t>(
                                                            field_elder_param_userdata
                                                        );

                                                    const auto before = base_pop_rate_ref;
                                                    constexpr int32_t requested = 100;

                                                    base_pop_rate_ref = requested;

                                                    const auto after =
                                                        base_pop_rate_field->get_data<int32_t>(
                                                            field_elder_param_userdata
                                                        );

                                                    FILE* write_log = nullptr;
                                                    fopen_s(
                                                        &write_log,
                                                        "mhs3_micro_runtime.log",
                                                        "a"
                                                    );

                                                    if (write_log != nullptr) {
                                                        std::fprintf(
                                                            write_log,
                                                            "[MHS3 Micro] BasePopRate write: "
                                                            "before=%d requested=%d after=%d\n",
                                                            before,
                                                            requested,
                                                            after
                                                        );
                                                        std::fclose(write_log);
                                                    }
                                                }

                                                bool values_expected = false;

                                                if (
                                                    g_elder_values_logged.compare_exchange_strong(
                                                        values_expected,
                                                        true,
                                                        std::memory_order_relaxed
                                                    )
                                                ) {
                                                    FILE* log = nullptr;
                                                    fopen_s(
                                                        &log,
                                                        "mhs3_micro_runtime.log",
                                                        "a"
                                                    );

                                                    if (log != nullptr) {
                                                        std::fprintf(
                                                            log,
                                                            "[MHS3 Micro] Elder values: "
                                                            "BasePopRate=%d "
                                                            "ElderEndBattleCount=%d\n",
                                                            base_pop_rate,
                                                            elder_end_battle_count
                                                        );
                                                        std::fclose(log);
                                                    }
                                                }
                                            }
                                        }

                                        bool expected = false;

                                        if (g_elder_chain_logged.compare_exchange_strong(
                                                expected,
                                                true,
                                                std::memory_order_relaxed
                                            )) {
                                            FILE* log = nullptr;
                                            fopen_s(
                                                &log,
                                                "mhs3_micro_runtime.log",
                                                "a"
                                            );

                                            if (log != nullptr) {
                                                std::fprintf(
                                                    log,
                                                    "[MHS3 Micro] Elder chain resolved: "
                                                    "StageManager=0x%llx "
                                                    "FieldElderCtrl=0x%llx "
                                                    "FieldElderParamUserData=0x%llx\n",
                                                    static_cast<unsigned long long>(
                                                        g_last_stage_manager_instance.load(
                                                            std::memory_order_relaxed
                                                        )
                                                    ),
                                                    static_cast<unsigned long long>(
                                                        g_last_field_elder_ctrl.load(
                                                            std::memory_order_relaxed
                                                        )
                                                    ),
                                                    static_cast<unsigned long long>(
                                                        reinterpret_cast<uintptr_t>(
                                                            field_elder_param_userdata
                                                        )
                                                    )
                                                );
                                                std::fclose(log);
                                            }
                                        }
                                    }
                                }
                            }
                        }
                    }
                }
            }
        }
    }

    const auto elder_probe_end =
        std::chrono::steady_clock::now();

    const auto elder_probe_us =
        std::chrono::duration_cast<std::chrono::microseconds>(
            elder_probe_end - elder_probe_begin
        ).count();

    if (elder_probe_us >= 50000) {
        const auto log_index =
            g_slow_elder_probe_log_count.fetch_add(
                1,
                std::memory_order_relaxed
            );

        if (log_index < 20) {
            FILE* log = nullptr;
            fopen_s(
                &log,
                "mhs3_micro_runtime.log",
                "a"
            );

            if (log != nullptr) {
                std::fprintf(
                    log,
                    "[MHS3 Micro] Slow elder probe: %lld us "
                    "tick=%llu ms\n",
                    static_cast<long long>(elder_probe_us),
                    static_cast<unsigned long long>(
                        GetTickCount64()
                    )
                );
                std::fclose(log);
            }
        }
    }
}

void begin_rendering_hook(void* entry) {
    on_frame();
    g_begin_rendering_original(entry);
}

DWORD WINAPI install_begin_rendering_hook(LPVOID) {
    // Give RE Engine time to initialize its type database/application singleton.
    Sleep(5000);

    if (auto* tdb = sdk::RETypeDB::get(); tdb != nullptr) {
        if (auto* save_data_manager =
                tdb->find_type("app.SaveDataManager");
            save_data_manager != nullptr) {

            if (auto* get_title_text =
                    save_data_manager->get_method("getTitleText()");
                get_title_text != nullptr) {

                auto* target = get_actual_function(
                    get_title_text->get_function()
                );

                if (target != nullptr) {
                    g_get_title_text_hook = safetyhook::create_mid(
                        target,
                        &get_title_text_hook
                    );

                    FILE* hook_log = nullptr;
                    fopen_s(
                        &hook_log,
                        "mhs3_micro_runtime.log",
                        "a"
                    );

                    if (hook_log != nullptr) {
                        std::fprintf(
                            hook_log,
                            "[MHS3 Micro] getTitleText hook: %s target=0x%llx\n",
                            g_get_title_text_hook ? "installed" : "FAILED",
                            static_cast<unsigned long long>(
                                reinterpret_cast<uintptr_t>(target)
                            )
                        );
                        std::fclose(hook_log);
                    }
                }
            }
        }
    }

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
    g_micro_d3d12_hook.initialize();
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
