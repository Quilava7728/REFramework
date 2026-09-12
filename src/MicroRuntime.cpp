#include "MicroRuntime.hpp"
#include "MicroD3D12Hook.hpp"

#include <windows.h>

#include <atomic>
#include <chrono>
#include <cstdio>
#include <cstdint>

#include <sdk/Application.hpp>
#include <sdk/GameIdentity.hpp>
#include <sdk/REContext.hpp>
#include "sdk/REMath.hpp"
#include <sdk/RETypeDB.hpp>
#include <sdk/helpers/NativeObject.hpp>

namespace mhs3::micro_runtime {

namespace {

using ApplicationEntryFn = void (*)(void*);
bool is_gamepad_a_held() {
    static sdk::helpers::NativeObject gamepad{"via.hid.GamePad"};

    if (!gamepad.update()) {
        return false;
    }

    auto* pad = sdk::call_native_func_easy<REManagedObject*>(
        gamepad.object,
        gamepad.t,
        "get_LastInputDevice"
    );

    if (pad == nullptr) {
        return false;
    }

    static const auto gamepad_device_t =
        sdk::find_type_definition("via.hid.GamePadDevice");

    static const auto is_down =
        gamepad_device_t != nullptr
            ? gamepad_device_t->get_method(
                "isDown(via.hid.GamePadButton)"
            )
            : nullptr;

    if (is_down == nullptr) {
        return false;
    }

    return is_down->call_safe<bool>(
        sdk::get_thread_context(),
        pad,
        via::hid::GamePadButton::RDown
    );
}

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
std::atomic<bool> g_elder_end_battle_count_written{false};
std::atomic<int> g_requested_base_pop_rate{10};
std::atomic<bool> g_base_pop_rate_request_pending{false};
std::atomic<int> g_requested_elder_end_battle_count{5};
std::atomic<bool> g_elder_end_battle_count_request_pending{false};
std::atomic<uint32_t> g_slow_elder_probe_log_count{0};
std::atomic<bool> g_otomon_manager_logged{false};
std::atomic<bool> g_otomon_entries_logged{false};
std::atomic<bool> g_otomon_levitate_requested{false};
MicroD3D12Hook g_micro_d3d12_hook;

void on_frame() {
    // Operation Chungus Build #6:
    // Read one already-resolved piece of engine-owned state each frame.
    // No lookup, logging, allocation, mutation of engine state, or locking.
    g_frame_count.fetch_add(1, std::memory_order_relaxed);

    if ((GetAsyncKeyState(VK_F10) & 1) != 0) {
        g_otomon_levitate_requested.store(
            true,
            std::memory_order_relaxed
        );
    }

    const bool otomon_climb_held =
        (GetAsyncKeyState('E') & 0x8000) != 0 ||
        is_gamepad_a_held();

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

                                                bool base_pop_rate_request =
                                                    g_base_pop_rate_request_pending.exchange(
                                                        false,
                                                        std::memory_order_acq_rel
                                                    );

                                                int32_t requested =
                                                    g_requested_base_pop_rate.load(
                                                        std::memory_order_relaxed
                                                    );

                                                bool write_expected = false;

                                                if (
                                                    (GetAsyncKeyState(VK_F8) & 1) != 0 &&
                                                    g_base_pop_rate_written.compare_exchange_strong(
                                                        write_expected,
                                                        true,
                                                        std::memory_order_relaxed
                                                    )
                                                ) {
                                                    requested = 100;
                                                    base_pop_rate_request = true;
                                                }

                                                if (base_pop_rate_request) {
                                                    auto& base_pop_rate_ref =
                                                        base_pop_rate_field->get_data<int32_t>(
                                                            field_elder_param_userdata
                                                        );

                                                    const auto before = base_pop_rate_ref;

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

                                                bool elder_end_battle_count_request =
                                                    g_elder_end_battle_count_request_pending.exchange(
                                                        false,
                                                        std::memory_order_acq_rel
                                                    );

                                                int32_t requested_elder_end_battle_count =
                                                    g_requested_elder_end_battle_count.load(
                                                        std::memory_order_relaxed
                                                    );

                                                bool elder_count_write_expected = false;

                                                if (
                                                    (GetAsyncKeyState(VK_F9) & 1) != 0 &&
                                                    g_elder_end_battle_count_written.compare_exchange_strong(
                                                        elder_count_write_expected,
                                                        true,
                                                        std::memory_order_relaxed
                                                    )
                                                ) {
                                                    requested_elder_end_battle_count = 2;
                                                    elder_end_battle_count_request = true;
                                                }

                                                if (elder_end_battle_count_request) {
                                                    auto& elder_end_battle_count_ref =
                                                        elder_end_battle_count_field->get_data<int32_t>(
                                                            field_elder_param_userdata
                                                        );

                                                    const auto before = elder_end_battle_count_ref;

                                                    elder_end_battle_count_ref = requested_elder_end_battle_count;

                                                    const auto after =
                                                        elder_end_battle_count_field->get_data<int32_t>(
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
                                                            "[MHS3 Micro] ElderEndBattleCount write: "
                                                            "before=%d requested=%d after=%d\n",
                                                            before,
                                                            requested_elder_end_battle_count,
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

    if (tdb != nullptr) {
        auto* otomon_manager_type =
            tdb->find_type("app.OtomonManager");

        if (otomon_manager_type != nullptr) {
            auto* get_instance =
                otomon_manager_type->get_method("get_Instance");

            if (get_instance != nullptr) {
                auto* otomon_manager =
                    get_instance->call<::REManagedObject*>(
                        sdk::get_thread_context()
                    );

                if (otomon_manager != nullptr) {
                    auto* list_field =
                        otomon_manager_type->get_field(
                            "_WorldOtomonManageInfoList"
                        );

                    if (list_field != nullptr) {
                        auto* list =
                            list_field->get_data<::REManagedObject*>(
                                otomon_manager
                            );

                        if (list != nullptr) {
                            auto* list_type =
                                list->get_type_definition();

                            auto* get_count =
                                list_type != nullptr
                                    ? list_type->get_method("get_Count")
                                    : nullptr;

                            if (get_count != nullptr) {
                                const auto count =
                                    get_count->call<int32_t>(
                                        sdk::get_thread_context(),
                                        list
                                    );

                                bool expected = false;

                                if (
                                    g_otomon_manager_logged.compare_exchange_strong(
                                        expected,
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
                                            "[MHS3 Micro] OtomonManager resolved: "
                                            "instance=0x%llx list=0x%llx count=%d\n",
                                            static_cast<unsigned long long>(
                                                reinterpret_cast<uintptr_t>(
                                                    otomon_manager
                                                )
                                            ),
                                            static_cast<unsigned long long>(
                                                reinterpret_cast<uintptr_t>(
                                                    list
                                                )
                                            ),
                                            count
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

    const bool otomon_probe_due =
        !g_otomon_entries_logged.load(
            std::memory_order_relaxed
        ) &&
        (g_frame_count.load(
            std::memory_order_relaxed
        ) % 60) == 0;

    const bool otomon_levitate_due =
        g_otomon_levitate_requested.load(
            std::memory_order_relaxed
        );

    if (
        tdb != nullptr &&
        (
            otomon_probe_due ||
            otomon_levitate_due ||
            otomon_climb_held
        )
    ) {
        const bool levitate_requested =
            g_otomon_levitate_requested.exchange(
                false,
                std::memory_order_acq_rel
            );

        bool levitate_done = false;
        bool climb_done = false;

        auto* otomon_manager_type =
            tdb->find_type("app.OtomonManager");

        if (otomon_manager_type != nullptr) {
            auto* get_instance =
                otomon_manager_type->get_method("get_Instance");

            if (get_instance != nullptr) {
                auto* otomon_manager =
                    get_instance->call<::REManagedObject*>(
                        sdk::get_thread_context()
                    );

                if (otomon_manager != nullptr) {
                    auto* list_field =
                        otomon_manager_type->get_field(
                            "_WorldOtomonManageInfoList"
                        );

                    if (list_field != nullptr) {
                        auto* list =
                            list_field->get_data<::REManagedObject*>(
                                otomon_manager
                            );

                        if (list != nullptr) {
                            auto* list_type =
                                list->get_type_definition();

                            auto* get_count =
                                list_type != nullptr
                                    ? list_type->get_method("get_Count")
                                    : nullptr;

                            auto* get_item =
                                list_type != nullptr
                                    ? list_type->get_method("get_Item")
                                    : nullptr;

                            if (
                                get_count != nullptr &&
                                get_item != nullptr
                            ) {
                                const auto count =
                                    get_count->call<int32_t>(
                                        sdk::get_thread_context(),
                                        list
                                    );

                                FILE* log = nullptr;

                                if (!otomon_climb_held) {
                                    fopen_s(
                                        &log,
                                        "mhs3_micro_runtime.log",
                                        "a"
                                    );
                                }

                                {
                                    auto otomon_log =
                                        [&](const char* format, auto... args) {
                                            if (log != nullptr) {
                                                std::fprintf(
                                                    log,
                                                    format,
                                                    args...
                                                );
                                            }
                                        };

                                    otomon_log(
                                        "[MHS3 Micro] Otomon roster: count=%d\n",
                                        count
                                    );

                                    const auto safe_count =
                                        count > 64 ? 64 : count;

                                    bool found_valid_entry = false;

                                    for (
                                        int32_t i = 0;
                                        i < safe_count;
                                        ++i
                                    ) {
                                        auto* item =
                                            get_item->call<::REManagedObject*>(
                                                sdk::get_thread_context(),
                                                list,
                                                i
                                            );

                                        if (item == nullptr) {
                                            continue;
                                        }

                                        auto* item_type =
                                            item->get_type_definition();

                                        if (item_type == nullptr) {
                                            continue;
                                        }

                                        auto* valid_field =
                                            item_type->get_field("_Valid");

                                        auto* character_field =
                                            item_type->get_field(
                                                "_WOtCharacter"
                                            );

                                        auto* game_object_field =
                                            item_type->get_field(
                                                "_MainGameObject"
                                            );

                                        otomon_log(
                                            "[MHS3 Micro] Otomon slot %d: "
                                            "item=0x%llx fields="
                                            "valid:%d character:%d gameObject:%d\n",
                                            i,
                                            static_cast<unsigned long long>(
                                                reinterpret_cast<uintptr_t>(
                                                    item
                                                )
                                            ),
                                            valid_field != nullptr ? 1 : 0,
                                            character_field != nullptr ? 1 : 0,
                                            game_object_field != nullptr ? 1 : 0
                                        );

                                        if (
                                            valid_field == nullptr ||
                                            character_field == nullptr ||
                                            game_object_field == nullptr
                                        ) {
                                            continue;
                                        }

                                        const auto valid =
                                            valid_field->get_data<bool>(
                                                item
                                            );

                                        auto* character =
                                            character_field
                                                ->get_data<::REManagedObject*>(
                                                    item
                                                );

                                        auto* game_object =
                                            game_object_field
                                                ->get_data<::REManagedObject*>(
                                                    item
                                                );

                                        otomon_log(
                                            "[MHS3 Micro] Otomon slot %d data: "
                                            "valid=%d character=0x%llx "
                                            "gameObject=0x%llx\n",
                                            i,
                                            valid ? 1 : 0,
                                            static_cast<unsigned long long>(
                                                reinterpret_cast<uintptr_t>(
                                                    character
                                                )
                                            ),
                                            static_cast<unsigned long long>(
                                                reinterpret_cast<uintptr_t>(
                                                    game_object
                                                )
                                            )
                                        );

                                        if (valid) {
                                            found_valid_entry = true;

                                            auto* character_type =
                                                character != nullptr
                                                    ? character->get_type_definition()
                                                    : nullptr;

                                            auto* fly_distcn_field =
                                                character_type != nullptr
                                                    ? character_type->get_field(
                                                        "_FlyMoveDistcn"
                                                    )
                                                    : nullptr;

                                            auto* disable_update_field =
                                                character_type != nullptr
                                                    ? character_type->get_field(
                                                        "_DisableUpdate"
                                                    )
                                                    : nullptr;

                                            float fly_distcn = 0.0f;
                                            bool disable_update = false;

                                            if (
                                                character != nullptr &&
                                                fly_distcn_field != nullptr
                                            ) {
                                                fly_distcn =
                                                    fly_distcn_field
                                                        ->get_data<float>(
                                                            character
                                                        );
                                            }

                                            if (
                                                character != nullptr &&
                                                disable_update_field != nullptr
                                            ) {
                                                disable_update =
                                                    disable_update_field
                                                        ->get_data<bool>(
                                                            character
                                                        );
                                            }

                                            auto* game_object_type =
                                                game_object != nullptr
                                                    ? game_object->get_type_definition()
                                                    : nullptr;

                                            auto* get_transform =
                                                game_object_type != nullptr
                                                    ? game_object_type->get_method(
                                                        "get_Transform"
                                                    )
                                                    : nullptr;

                                            auto* transform =
                                                get_transform != nullptr
                                                    ? get_transform
                                                        ->call<::REManagedObject*>(
                                                            sdk::get_thread_context(),
                                                            game_object
                                                        )
                                                    : nullptr;

                                            if (
                                                otomon_climb_held &&
                                                !climb_done &&
                                                fly_distcn > 1.0f &&
                                                transform != nullptr
                                            ) {
                                                Vector4f position{};

                                                sdk::call_object_func<Vector4f*>(
                                                    transform,
                                                    "get_Position",
                                                    &position,
                                                    sdk::get_thread_context(),
                                                    transform
                                                );

                                                Vector3f new_position{
                                                    position.x,
                                                    position.y + 1.0f,
                                                    position.z
                                                };

                                                sdk::call_object_func<void*>(
                                                    transform,
                                                    "set_Position",
                                                    sdk::get_thread_context(),
                                                    transform,
                                                    &new_position
                                                );

                                                climb_done = true;
                                            }

                                            if (
                                                levitate_requested &&
                                                !levitate_done &&
                                                fly_distcn > 1.0f &&
                                                transform != nullptr
                                            ) {
                                                Vector4f position{};

                                                sdk::call_object_func<Vector4f*>(
                                                    transform,
                                                    "get_Position",
                                                    &position,
                                                    sdk::get_thread_context(),
                                                    transform
                                                );

                                                const float old_y = position.y;

                                                Vector3f new_position{
                                                    position.x,
                                                    old_y + 0.5f,
                                                    position.z
                                                };

                                                sdk::call_object_func<void*>(
                                                    transform,
                                                    "set_Position",
                                                    sdk::get_thread_context(),
                                                    transform,
                                                    &new_position
                                                );

                                                otomon_log(
                                                    "[MHS3 Micro] Otomon levitate: "
                                                    "slot=%d oldY=%.3f newY=%.3f "
                                                    "flyDistcn=%.3f transform=0x%llx\n",
                                                    i,
                                                    old_y,
                                                    new_position.y,
                                                    fly_distcn,
                                                    static_cast<unsigned long long>(
                                                        reinterpret_cast<uintptr_t>(
                                                            transform
                                                        )
                                                    )
                                                );

                                                levitate_done = true;
                                            }

                                            otomon_log(
                                                "[MHS3 Micro] Otomon flight %d: "
                                                "flyField=%d disableField=%d "
                                                "flyDistcn=%.3f disableUpdate=%d "
                                                "transform=0x%llx\n",
                                                i,
                                                fly_distcn_field != nullptr ? 1 : 0,
                                                disable_update_field != nullptr ? 1 : 0,
                                                fly_distcn,
                                                disable_update ? 1 : 0,
                                                static_cast<unsigned long long>(
                                                    reinterpret_cast<uintptr_t>(
                                                        transform
                                                    )
                                                )
                                            );

                                            otomon_log(
                                                "[MHS3 Micro] Otomon entry %d: "
                                                "valid=1 character=0x%llx "
                                                "gameObject=0x%llx\n",
                                                i,
                                                static_cast<unsigned long long>(
                                                    reinterpret_cast<uintptr_t>(
                                                        character
                                                    )
                                                ),
                                                static_cast<unsigned long long>(
                                                    reinterpret_cast<uintptr_t>(
                                                        game_object
                                                    )
                                                )
                                            );
                                        }
                                    }

                                    if (log != nullptr) {
                                        std::fclose(log);
                                    }

                                    if (found_valid_entry) {
                                        g_otomon_entries_logged.store(
                                            true,
                                            std::memory_order_relaxed
                                        );
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

void request_base_pop_rate(int value) {
    g_requested_base_pop_rate.store(
        value,
        std::memory_order_relaxed
    );

    g_base_pop_rate_request_pending.store(
        true,
        std::memory_order_release
    );
}

void request_elder_end_battle_count(int value) {
    g_requested_elder_end_battle_count.store(
        value,
        std::memory_order_relaxed
    );

    g_elder_end_battle_count_request_pending.store(
        true,
        std::memory_order_release
    );
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
