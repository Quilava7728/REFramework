#include "Mods.hpp"
#include "REFramework.hpp"
#include <utility/Scan.hpp>
#include <utility/Module.hpp>
#include <utility/String.hpp>
#include <utility/Memory.hpp>

#include <sdk/GameIdentity.hpp>
#include "sdk/GUIPrimitiveSystem.hpp"
#include "sdk/Application.hpp"

#include "Hooks.hpp"
#include "ScriptRunner.hpp"

Hooks* g_hook = nullptr;

std::shared_ptr<Hooks>& Hooks::get() {
    static std::shared_ptr<Hooks> instance = std::make_shared<Hooks>();
    return instance;
}

Hooks::Hooks() {
    g_hook = this;
}

std::optional<std::string> Hooks::on_initialize() {
    auto game = g_framework->get_module().as<HMODULE>();

    const auto mod_size = utility::get_module_size(game);

    if (!mod_size) {
        return "Unable to get module size";
    }

    for (auto hook : m_hook_list) {
        spdlog::info("[Hooks] Entering hook...");

        auto result = hook();

        // Error occurred when hooking
        if (result) {
            return result;
        }
    }

    spdlog::info("[Hooks] Finished hooking");

    return Mod::on_initialize();
}

void Hooks::on_draw_ui() {
    if (!ImGui::CollapsingHeader("Performance")) {
        return;
    }

    ImGui::Checkbox("Enable Profiling", &m_profiling_enabled);

    if (!m_profiling_enabled) {
        return;
    }

    ImGui::Text("Application Entry Times");

    std::vector<const char*> sorted_times{};
    std::scoped_lock _{m_profiler_mutex};

    std::chrono::high_resolution_clock::duration total_reframework_time{};
    std::chrono::high_resolution_clock::duration total_game_time{};

    for (auto& entry : m_application_entry_times) {
        sorted_times.emplace_back(entry.first);
        total_reframework_time += entry.second.reframework_pre_time + entry.second.reframework_post_time;
        total_game_time += entry.second.callback_time;
    }

    std::sort(sorted_times.begin(), sorted_times.end(), [&](const char* a, const char* b) {
        const auto& a_entry = m_application_entry_times[a];
        const auto& b_entry = m_application_entry_times[b];

        return a_entry.callback_time + a_entry.reframework_pre_time + a_entry.reframework_post_time 
                > b_entry.callback_time + b_entry.reframework_pre_time + b_entry.reframework_post_time;
    });

    ImGui::Text("Total REFramework Time: %.3fms", std::chrono::duration_cast<std::chrono::duration<float, std::milli>>(total_reframework_time).count());
    ImGui::Text("Total Game Time: %.3fms", std::chrono::duration_cast<std::chrono::duration<float, std::milli>>(total_game_time).count());

    for (auto name : sorted_times) {
        auto& entry = m_application_entry_times[name];
        
        ImGui::SetNextItemOpen(true);

        if (ImGui::TreeNode(name)) {
            ImGui::Text("Game Time: %s: %.2fms", name, entry.callback_time.count() / 1000000.0f);
            ImGui::Text("REFramework Pre Time: %.2fms", entry.reframework_pre_time.count() / 1000000.0f);
            ImGui::Text("REFramework Post Time: %.2fms", entry.reframework_post_time.count() / 1000000.0f);
            ImGui::Text("Total Time: %.2fms", (entry.callback_time + entry.reframework_pre_time + entry.reframework_post_time).count() / 1000000.0f);
            
            ImGui::TreePop();
        }
    }
}

#define LAYER_HOOK_BODY(x, x2, x3) \
if (!g_framework->is_ready()) {\
    auto original_func = g_hook->m_layer_hooks.##x##.##x3##_hook->get_original<decltype(RenderLayerHook<sdk::renderer::layer::##x2##>::##x3##)>();\
    original_func(layer, render_ctx); \
    return; \
} \
bool any_false = false; \
const auto& mods = g_framework->get_mods()->get_mods(); \
for (auto& mod : mods) { \
    const auto result = mod->on_pre_##x##_layer_##x3##(layer, render_ctx); \
    if (!result) { \
        any_false = true; \
    } \
} \
if (!any_false) { \
    auto original_func = g_hook->m_layer_hooks.##x##.##x3##_hook->get_original<decltype(RenderLayerHook<sdk::renderer::layer::##x2##>::##x3##)>();\
    original_func(layer, render_ctx); \
} \
for (auto& mod : mods) { \
    mod->on_##x##_layer_##x3##(layer, render_ctx); \
}

void Hooks::RenderLayerHook<sdk::renderer::layer::Scene>::update(sdk::renderer::layer::Scene* layer, void* render_ctx) {
    LAYER_HOOK_BODY(scene, Scene, update);
}

void Hooks::RenderLayerHook<sdk::renderer::layer::Scene>::draw(sdk::renderer::layer::Scene* layer, void* render_ctx) {
    LAYER_HOOK_BODY(scene, Scene, draw);
}

void Hooks::RenderLayerHook<sdk::renderer::layer::PostEffect>::update(sdk::renderer::layer::PostEffect* layer, void* render_ctx) {
    LAYER_HOOK_BODY(post_effect, PostEffect, update);
}

void Hooks::RenderLayerHook<sdk::renderer::layer::PostEffect>::draw(sdk::renderer::layer::PostEffect* layer, void* render_ctx) {
    LAYER_HOOK_BODY(post_effect, PostEffect, draw);
}

void Hooks::RenderLayerHook<sdk::renderer::layer::Overlay>::update(sdk::renderer::layer::Overlay* layer, void* render_ctx) {
    LAYER_HOOK_BODY(overlay, Overlay, update);
}

void Hooks::RenderLayerHook<sdk::renderer::layer::Overlay>::draw(sdk::renderer::layer::Overlay* layer, void* render_ctx) {
    LAYER_HOOK_BODY(overlay, Overlay, draw);
}

std::optional<std::string> Hooks::hook_update_transform() {
    auto game = g_framework->get_module().as<HMODULE>();

    // The 48 8B 4D 40 bit might change.
    // Version 1.0 jmp stub: game+0x1dc7de0
    // Version 1
    //auto updateTransformCall = utility::scan(game, "E8 ? ? ? ? 48 8B 5B ? 48 85 DB 75 ? 48 8B 4D 40 48 31 E1");

    // Version 2 Dec 17th, 2019 (works on old version too) game.exe+0x1DD3FF0
    // If this ever changes, get the singleton for via.SceneManager, find its
    // constructor function, and look for the job function added near the end of the constructor
    // UpdateTransform gets called near the end of the job, looks like this:
    /*
      if ( *(_BYTE *)(v2 + 0x114) )
        UpdateTransform(v14, 0, v10);
      else
        sub_141DD4140(v14, 0i64, v10);
    */

    struct TransformPattern {
        std::string pat;
        uint32_t offset;
    };

    /*
        these instructions are near the UpdateTransform call
        mov     eax, 1
        lock xadd [rsi+318h], eax
        cdqe
    */
    std::vector<TransformPattern> pats {
        { "E8 ? ? ? ? 48 8B 5B ? 48 85 DB 75 ? 48 8B 4D 40 48 ? ?", 1 }, // RE2 - MHRise v1.0
        { "33 D2 E8 ? ? ? ? B8 01 00 00 00 F0 0F", 3 }, // RE7/RE2/RE3 update to TDB v70/newer games?
        { "0F B6 D1 48 8B CB E8 ? ? ? ? 48 8B 9B ? ? ? ?", 7 }, // RE7
        { "0F B6 D0 48 8B CB E8 ? ? ? ? 48 8B 9B ? ? ? ?", 7 }, // RE7 Demo
        { "31 D2 41 ? F8 E8 ? ? ? ? EB", 6}, // MHWILDS/TDB74+
        { "31 D2 41 ? F8 E8 ? ? ? ? B8 01 00 00 00 F0", 6 }, // MHS3/TDB82+ (lock xadd after call)
    };

    uintptr_t update_transform = 0;

    for (auto& pat : pats) {
        auto result = utility::scan(game, pat.pat.c_str());

        if (result) {
            update_transform = utility::calculate_absolute(*result + pat.offset);
            break;
        }
    }

    if (update_transform == 0) {
        spdlog::error("Unable to find UpdateTransform pattern.");
        return std::nullopt; // Allow it to continue anyways, it's not strictly necessary except for freecam
    }

    spdlog::info("UpdateTransform: {:x}", update_transform);

    // Can be found by breakpointing RETransform's worldTransform
    m_update_transform_hook = std::make_unique<FunctionHook>(update_transform, &update_transform_hook);

    if (!m_update_transform_hook->create()) {
        //return "Failed to hook UpdateTransform";
        spdlog::error("Failed to hook UpdateTransform");
        return std::nullopt; // who cares
    }

    return std::nullopt;
}

std::optional<std::string> Hooks::hook_update_camera_controller() {
    if (!(sdk::GameIdentity::get().is_re2() || sdk::GameIdentity::get().is_re3())) {
        return std::nullopt;
    }

    // Version 1.0 jmp stub: game+0xB4685A0
    // Version 1
    /*auto updatecamera_controllerCall = utility::scan(game, "75 ? 48 89 FA 48 89 D9 E8 ? ? ? ? 48 8B 43 50 48 83 78 18 00 75 ? 45 89");

    if (!updatecamera_controllerCall) {
        return "Unable to find Updatecamera_controller pattern.";
    }

    auto updatecamera_controller = utility::calculate_absolute(*updatecamera_controllerCall + 9);*/

    // Version 2 Dec 17th, 2019 game.exe+0x7CF690 (works on old version too)
    //auto update_camera_controller = utility::scan(game, "40 55 56 57 48 8D AC 24 ? ? ? ? 48 81 EC ? ? 00 00 48 8B 41 50");

    // Version 3 June 2nd, 2020 game.exe+0xD41AD0 (works on old version too)
    auto update_camera_controller = sdk::find_native_method(game_namespace("camera.PlayerCameraController"), "updateCameraPosition");

    if (update_camera_controller == nullptr) {
        return std::string{"Failed to find "} + game_namespace("camera.PlayerCameraController") + "::updateCameraPosition";
    }

    spdlog::info("camera.PlayerCameraController.updateCameraPosition: {:x}", (uintptr_t)update_camera_controller);

    // Can be found by breakpointing camera controller's worldPosition
    m_update_camera_controller_hook = std::make_unique<FunctionHook>(update_camera_controller, &update_camera_controller_hook);

    if (!m_update_camera_controller_hook->create()) {
        return "Failed to hook UpdateCameraController";
    }

    return std::nullopt;
}

std::optional<std::string> Hooks::hook_update_camera_controller2() {
    if (!(sdk::GameIdentity::get().is_re2() || sdk::GameIdentity::get().is_re3())) {
        return std::nullopt;
    }

    // Version 1.0 jmp stub: game+0xCF2510
    // Version 1.0 function: game+0xB436230
    
    // Version 1
    //auto updatecamera_controller2 = utility::scan(game, "40 53 57 48 81 ec ? ? ? ? 48 8b 41 ? 48 89 d7 48 8b 92 ? ? 00 00");
    // Version 2 Dec 17th, 2019 game.exe+0x6CD9C0 (works on old version too)
    auto update_camera_controller2 = sdk::find_native_method(game_namespace("camera.TwirlerCameraControllerRoot"), "update");

    if (update_camera_controller2 == nullptr) {
        return std::string{"Failed to find "} + game_namespace("camera.TwirlerCameraControllerRoot") + "::update";
    }

    spdlog::info("camera.TwirlerCameraControllerRoot.update: {:x}", (uintptr_t)update_camera_controller2);

    // Can be found by breakpointing camera controller's worldRotation
    m_update_camera_controller2_hook = std::make_unique<FunctionHook>(update_camera_controller2, &update_camera_controller2_hook);

    if (!m_update_camera_controller2_hook->create()) {
        return "Failed to hook Updatecamera_controller2";
    }

    return std::nullopt;
}

std::optional<std::string> Hooks::hook_gui_draw() {
    spdlog::info("[Hooks] Attempting to hook GUI functions...");

    auto game = g_framework->get_module().as<HMODULE>();
    auto application = sdk::Application::get();

    // This pattern appears to work all the way from RE2 to RE8.
    // If this ever breaks, its parent function is found within via.gui.GUIManager.
    // It is used as a draw callback. The assignment can be found within the constructor near the end.
    // "onEnd(via.gui.TextAnimationEndArg)" can be used as a reference to find the constructor.
    // "copyProperties(via.gui.PlayObject)" also works in RE7 and onwards
    // In RE2:
    /*  
    *(_QWORD *)(v23 + 8 * v22) = &vtable_thing;
    *(_QWORD *)(v23 + 8 * v22 + 8) = gui_manager;
    *(_OWORD *)(v23 + 8 * v22 + 16) = v34;
    *(_QWORD *)(v23 + 8 * v22 + 32) = gui_manager;
    ++*(_DWORD *)(gui_manager + 232);
    *(_QWORD *)&v35 = draw_task_function; <-- "gui_draw_call" is found within this function.
    */
    size_t offset = 12;
    spdlog::info("[Hooks] Scanning for first GUI draw call...");
    auto gui_draw_call = utility::scan(game, "49 8B 0C CE 48 83 79 10 00 74 ? E8 ? ? ? ?");

    if (!gui_draw_call) {
        spdlog::info("[Hooks] Scanning for fallback GUI draw call...");
        // RE7 (+0x20 grabs the owner ptr, 0x10 in others)
        gui_draw_call = utility::scan(game, "49 8B 0C CE 48 83 79 20 00 74 ? E8 ? ? ? ?");

        if (!gui_draw_call) {
            // MHWILDS
            gui_draw_call = utility::scan(game, "48 8B 0C C3 48 83 79 ? 00 74 ? 48 89 ? E8 ? ? ? ?");
            offset = 15;

            if (!gui_draw_call) {
                // PRAGMATA
                gui_draw_call = utility::scan(game, "49 8B 0C C6 48 83 79 ? 00 74 ? E8 ? ? ? ?");
                offset = 12;

                if (!gui_draw_call) {
                    //return "Unable to find gui_draw_call pattern.";
                    spdlog::error("[Hooks] Unable to find gui_draw_call pattern.");
                    return std::nullopt; // Don't bother erroring out the entire mod just because of this
                }
            }
        }
    }

    spdlog::info("[Hooks] Found gui_draw_call at {:x}", *gui_draw_call);

    auto gui_draw = utility::calculate_absolute(*gui_draw_call + offset);
    spdlog::info("[Hooks] gui_draw: {:x}", gui_draw);

    m_gui_draw_hook = std::make_unique<FunctionHook>(gui_draw, &gui_draw_hook);

    if (!m_gui_draw_hook->create()) {
        return "Failed to hook GUI::draw";
    }

    return std::nullopt;
}

std::optional<std::string> Hooks::hook_application_entry(std::string name, std::unique_ptr<FunctionHook>& hook, void (*hook_fn)(void*)) {
    auto application = sdk::Application::get();

    if (application == nullptr) {
        return "Failed to get via.Application";
    }

    auto entry = application->get_function(name);

    if (entry == nullptr) {
        return "Unable to find via::Application::" + name;
    }

    auto func = entry->func;

    if (func == nullptr) {
        return "via::Application::" + name + " is null";
    }

    spdlog::info("{} entry: {:x}", name, (uintptr_t)entry);
    spdlog::info("{}: {:x}", name, (uintptr_t)func - g_framework->get_module());

    hook = std::make_unique<FunctionHook>(func, hook_fn);

    if (!hook->create()) {
        return "Failed to hook via::Application::" + name;
    }
    
    spdlog::info("Hooked via::Application::{}", name);

    return std::nullopt;
}

// MHS3 diagnostic:
// Hook ONLY via.Application::BeginRendering instead of replacing every
// application-entry function pointer.
std::optional<std::string> Hooks::hook_begin_rendering_only() {
    auto application = sdk::Application::get();

    if (application == nullptr) {
        return "Failed to get via.Application";
    }

    auto entry = application->get_function("BeginRendering");

    if (entry == nullptr) {
        return "Unable to find via::Application::BeginRendering";
    }

    if (entry->func == nullptr) {
        return "via::Application::BeginRendering is null";
    }

    m_begin_rendering_original = entry->func;
    entry->func = &begin_rendering_hook;

    spdlog::info("[Hooks] MHS3 diagnostic: hooked ONLY via.Application::BeginRendering");

    return std::nullopt;
}


namespace {
struct MHS3EntryCadenceStats {
    std::chrono::steady_clock::time_point last_call{};
    std::chrono::steady_clock::time_point last_report{std::chrono::steady_clock::now()};

    uint64_t call_count{0};

    uint64_t gap_count{0};
    uint64_t gap_total_us{0};
    uint64_t gap_max_us{0};
    uint64_t gap_over_50ms{0};
    uint64_t gap_over_100ms{0};
    uint64_t gap_over_200ms{0};

    uint64_t original_count{0};
    uint64_t original_total_us{0};
    uint64_t original_max_us{0};
};

// Build #20:
// Measure wall-clock time BETWEEN selected via.Application stages.
// Build #19 showed that the individual stage functions could be fast while
// the same-stage frame cadence still contained ~70-80 ms gaps.
//
// These four transitions split one selected frame cycle into:
//   UpdateBehavior -> PrepareRendering
//   PrepareRendering -> WaitRendering
//   WaitRendering -> BeginRendering
//   BeginRendering -> next UpdateBehavior
enum class MHS3CrossStage : uint8_t {
    None = 0,
    UpdateBehavior,
    PrepareRendering,
    WaitRendering,
    BeginRendering
};

struct MHS3CrossStageStats {
    std::atomic<uint64_t> calls{0};
    std::atomic<uint64_t> total_us{0};
    std::atomic<uint64_t> max_us{0};
    std::atomic<uint64_t> over_50ms{0};
    std::atomic<uint64_t> over_100ms{0};
    std::atomic<uint64_t> over_200ms{0};
    std::atomic<uint64_t> over_500ms{0};
};

MHS3CrossStageStats g_mhs3_cross_update_to_prepare{};
MHS3CrossStageStats g_mhs3_cross_prepare_to_wait{};
MHS3CrossStageStats g_mhs3_cross_wait_to_begin{};
MHS3CrossStageStats g_mhs3_cross_begin_to_update{};

std::atomic<uint64_t> g_mhs3_cross_last_report_us{0};

thread_local MHS3CrossStage g_mhs3_cross_last_stage = MHS3CrossStage::None;
thread_local uint64_t g_mhs3_cross_last_stage_us = 0;

static uint64_t mhs3_cross_now_us() {
    return (uint64_t)std::chrono::duration_cast<std::chrono::microseconds>(
        std::chrono::steady_clock::now().time_since_epoch()
    ).count();
}

static void mhs3_cross_update_max(
    std::atomic<uint64_t>& target,
    uint64_t value
) {
    auto old = target.load(std::memory_order_relaxed);

    while (
        value > old &&
        !target.compare_exchange_weak(
            old,
            value,
            std::memory_order_relaxed,
            std::memory_order_relaxed
        )
    ) {
    }
}

static void mhs3_cross_record_duration(
    MHS3CrossStageStats& stats,
    uint64_t elapsed_us
) {
    stats.calls.fetch_add(1, std::memory_order_relaxed);
    stats.total_us.fetch_add(elapsed_us, std::memory_order_relaxed);
    mhs3_cross_update_max(stats.max_us, elapsed_us);

    if (elapsed_us >= 50000) {
        stats.over_50ms.fetch_add(1, std::memory_order_relaxed);
    }

    if (elapsed_us >= 100000) {
        stats.over_100ms.fetch_add(1, std::memory_order_relaxed);
    }

    if (elapsed_us >= 200000) {
        stats.over_200ms.fetch_add(1, std::memory_order_relaxed);
    }

    if (elapsed_us >= 500000) {
        stats.over_500ms.fetch_add(1, std::memory_order_relaxed);
    }
}

struct MHS3CrossStageSnapshot {
    uint64_t calls{};
    uint64_t total_us{};
    uint64_t max_us{};
    uint64_t over_50ms{};
    uint64_t over_100ms{};
    uint64_t over_200ms{};
    uint64_t over_500ms{};
};

static MHS3CrossStageSnapshot mhs3_cross_take_snapshot(
    MHS3CrossStageStats& stats
) {
    MHS3CrossStageSnapshot out{};

    out.calls = stats.calls.exchange(0, std::memory_order_relaxed);
    out.total_us = stats.total_us.exchange(0, std::memory_order_relaxed);
    out.max_us = stats.max_us.exchange(0, std::memory_order_relaxed);
    out.over_50ms = stats.over_50ms.exchange(0, std::memory_order_relaxed);
    out.over_100ms = stats.over_100ms.exchange(0, std::memory_order_relaxed);
    out.over_200ms = stats.over_200ms.exchange(0, std::memory_order_relaxed);
    out.over_500ms = stats.over_500ms.exchange(0, std::memory_order_relaxed);

    return out;
}

static void mhs3_cross_log_snapshot(
    const char* name,
    const MHS3CrossStageSnapshot& stats
) {
    const double avg_us =
        stats.calls > 0
            ? (double)stats.total_us / (double)stats.calls
            : 0.0;

    spdlog::info(
        "[MHS3 CROSS] {} calls={} avg={:.2f} us max={} us "
        ">50ms={} >100ms={} >200ms={} >500ms={}",
        name,
        stats.calls,
        avg_us,
        stats.max_us,
        stats.over_50ms,
        stats.over_100ms,
        stats.over_200ms,
        stats.over_500ms
    );
}

static void record_mhs3_cross_stage(MHS3CrossStage current) {
    const auto now_us = mhs3_cross_now_us();

    if (
        g_mhs3_cross_last_stage != MHS3CrossStage::None &&
        g_mhs3_cross_last_stage_us != 0 &&
        now_us >= g_mhs3_cross_last_stage_us
    ) {
        const auto elapsed_us = now_us - g_mhs3_cross_last_stage_us;

        if (
            g_mhs3_cross_last_stage == MHS3CrossStage::UpdateBehavior &&
            current == MHS3CrossStage::PrepareRendering
        ) {
            mhs3_cross_record_duration(
                g_mhs3_cross_update_to_prepare,
                elapsed_us
            );
        }
        else if (
            g_mhs3_cross_last_stage == MHS3CrossStage::PrepareRendering &&
            current == MHS3CrossStage::WaitRendering
        ) {
            mhs3_cross_record_duration(
                g_mhs3_cross_prepare_to_wait,
                elapsed_us
            );
        }
        else if (
            g_mhs3_cross_last_stage == MHS3CrossStage::WaitRendering &&
            current == MHS3CrossStage::BeginRendering
        ) {
            mhs3_cross_record_duration(
                g_mhs3_cross_wait_to_begin,
                elapsed_us
            );
        }
        else if (
            g_mhs3_cross_last_stage == MHS3CrossStage::BeginRendering &&
            current == MHS3CrossStage::UpdateBehavior
        ) {
            mhs3_cross_record_duration(
                g_mhs3_cross_begin_to_update,
                elapsed_us
            );
        }
    }

    g_mhs3_cross_last_stage = current;
    g_mhs3_cross_last_stage_us = now_us;

    auto last_report =
        g_mhs3_cross_last_report_us.load(std::memory_order_relaxed);

    if (last_report == 0) {
        g_mhs3_cross_last_report_us.compare_exchange_strong(
            last_report,
            now_us,
            std::memory_order_relaxed,
            std::memory_order_relaxed
        );
        return;
    }

    if (now_us - last_report < 5000000) {
        return;
    }

    if (
        !g_mhs3_cross_last_report_us.compare_exchange_strong(
            last_report,
            now_us,
            std::memory_order_relaxed,
            std::memory_order_relaxed
        )
    ) {
        return;
    }

    mhs3_cross_log_snapshot(
        "UpdateBehavior->PrepareRendering",
        mhs3_cross_take_snapshot(g_mhs3_cross_update_to_prepare)
    );

    mhs3_cross_log_snapshot(
        "PrepareRendering->WaitRendering",
        mhs3_cross_take_snapshot(g_mhs3_cross_prepare_to_wait)
    );

    mhs3_cross_log_snapshot(
        "WaitRendering->BeginRendering",
        mhs3_cross_take_snapshot(g_mhs3_cross_wait_to_begin)
    );

    mhs3_cross_log_snapshot(
        "BeginRendering->UpdateBehavior",
        mhs3_cross_take_snapshot(g_mhs3_cross_begin_to_update)
    );
}

void run_mhs3_entry_cadence(
    const char* name,
    void* entry,
    void (*original)(void*),
    MHS3EntryCadenceStats& stats
) {
    if (original == nullptr) {
        return;
    }

    const auto now = std::chrono::steady_clock::now();

    if (stats.last_call.time_since_epoch().count() != 0) {
        const auto gap_us =
            (uint64_t)std::chrono::duration_cast<std::chrono::microseconds>(
                now - stats.last_call
            ).count();

        ++stats.gap_count;
        stats.gap_total_us += gap_us;
        stats.gap_max_us = std::max(stats.gap_max_us, gap_us);

        if (gap_us >= 50000) {
            ++stats.gap_over_50ms;
        }

        if (gap_us >= 100000) {
            ++stats.gap_over_100ms;
        }

        if (gap_us >= 200000) {
            ++stats.gap_over_200ms;
        }
    }

    stats.last_call = now;
    ++stats.call_count;

    const auto original_start = std::chrono::steady_clock::now();

    original(entry);

    const auto original_us =
        (uint64_t)std::chrono::duration_cast<std::chrono::microseconds>(
            std::chrono::steady_clock::now() - original_start
        ).count();

    ++stats.original_count;
    stats.original_total_us += original_us;
    stats.original_max_us = std::max(stats.original_max_us, original_us);

    const auto report_now = std::chrono::steady_clock::now();

    if (report_now - stats.last_report >= std::chrono::seconds(5)) {
        const double gap_avg_us =
            stats.gap_count > 0
                ? (double)stats.gap_total_us / (double)stats.gap_count
                : 0.0;

        const double original_avg_us =
            stats.original_count > 0
                ? (double)stats.original_total_us / (double)stats.original_count
                : 0.0;

        spdlog::info(
            "[MHS3 EKG] {} calls={} Original avg={:.2f} us max={} us "
            "Gap={} avg={:.2f} us max={} us >50ms={} >100ms={} >200ms={}",
            name,
            stats.call_count,
            original_avg_us,
            stats.original_max_us,
            stats.gap_count,
            gap_avg_us,
            stats.gap_max_us,
            stats.gap_over_50ms,
            stats.gap_over_100ms,
            stats.gap_over_200ms
        );

        stats.call_count = 0;

        stats.gap_count = 0;
        stats.gap_total_us = 0;
        stats.gap_max_us = 0;
        stats.gap_over_50ms = 0;
        stats.gap_over_100ms = 0;
        stats.gap_over_200ms = 0;

        stats.original_count = 0;
        stats.original_total_us = 0;
        stats.original_max_us = 0;

        stats.last_report = report_now;
    }
}
}

std::optional<std::string> Hooks::hook_mhs3_cadence_entries() {
    auto application = sdk::Application::get();

    if (application == nullptr) {
        return "Failed to get via.Application";
    }

    auto install = [&](const char* name, void (**original)(void*), void (*hook)(void*)) -> std::optional<std::string> {
        auto entry = application->get_function(name);

        if (entry == nullptr) {
            return std::string{"Unable to find via::Application::"} + name;
        }

        if (entry->func == nullptr) {
            return std::string{"via::Application::"} + name + " is null";
        }

        *original = entry->func;
        entry->func = hook;

        spdlog::info("[Hooks] MHS3 diagnostic: hooked via.Application::{}", name);

        return std::nullopt;
    };

    if (auto error = install(
        "UpdateBehavior",
        &m_update_behavior_original,
        &update_behavior_hook
    ); error.has_value()) {
        return error;
    }

    if (auto error = install(
        "PrepareRendering",
        &m_prepare_rendering_original,
        &prepare_rendering_hook
    ); error.has_value()) {
        return error;
    }

    if (auto error = install(
        "WaitRendering",
        &m_wait_rendering_original,
        &mhs3_wait_rendering_hook
    ); error.has_value()) {
        return error;
    }

    return std::nullopt;
}


namespace {
struct MHS3WaitCallsiteStats {
    uint64_t calls{0};
    uint64_t total_us{0};
    uint64_t max_us{0};
    uint64_t over_50ms{0};
    uint64_t over_100ms{0};
    uint64_t over_200ms{0};
    uint64_t over_500ms{0};
};

thread_local std::chrono::steady_clock::time_point g_mhs3_wait_a_start{};
thread_local std::chrono::steady_clock::time_point g_mhs3_wait_b_start{};

std::atomic<uintptr_t> g_mhs3_wait_a_handle{0};
std::atomic<uint64_t> g_mhs3_wait_a_epoch{0};

// Build #18:
// Shared timestamps are required because WaitA is waited on by one thread
// and signaled by another. The old thread_local wait start could never be
// observed correctly from the signaling thread.
std::atomic<uint64_t> g_mhs3_wait_a_start_us{0};
std::atomic<uint64_t> g_mhs3_wait_a_signal_epoch{0};
std::atomic<uint64_t> g_mhs3_wait_a_signal_us{0};

// Build #25:
// Dynamically remember the event that blocks the signaling worker.
// Handle values are intentionally NOT hardcoded because Wine/Windows
// HANDLE values can change between launches.
std::atomic<uintptr_t> g_mhs3_worker_wait_handle{0};
std::atomic<uint64_t> g_mhs3_worker_wait_start_us{0};
std::atomic<uint64_t> g_mhs3_worker_wait_epoch{0};

// Build #26:
// Three-stage upstream timing:
//
// A = producer SetEvent(+0x3a80), return RVA 0x003fef5f
// B = thread 564 returns from Wait(+0x3a80), RVA 0x0482d745
// C = thread 564 SetEvent(+0x3a70), return RVA 0x0482d7c1
//
// Keep absolute steady-clock timestamps so we can distinguish:
//   before A
//   A -> B
//   B -> C
std::atomic<uintptr_t> g_mhs3_upstream_wait_handle{0};
std::atomic<uint64_t> g_mhs3_upstream_wait_start_us{0};
std::atomic<uint64_t> g_mhs3_upstream_wait_epoch{0};
std::atomic<uint64_t> g_mhs3_upstream_signal_us{0};
std::atomic<uint64_t> g_mhs3_upstream_signal_epoch{0};
std::atomic<uint64_t> g_mhs3_upstream_wake_us{0};
std::atomic<uint64_t> g_mhs3_upstream_wake_epoch{0};

// Build #27:
// Time the actual Stage-A producer work.
//
// P0 = producer routine after prologue
// P1 = initial setup/acquire complete
// P2 = object-processing region complete
// P3 = post-loop call group #1 complete
// P4 = post-loop call group #2 complete
// A  = SetEvent(+0x3a80)
// Build #31G:
// Minimal object-loop timing bookkeeping.
// Thread-local so independent producer threads never share a timing pair.
thread_local uint64_t g_mhs3_object_loop_begin_us = 0;
thread_local uint64_t g_mhs3_object_loop_count = 0;
thread_local uint64_t g_mhs3_object_loop_total_us = 0;
thread_local uint64_t g_mhs3_object_loop_max_us = 0;

thread_local uint64_t g_mhs3_producer_p0_us = 0;
thread_local uint64_t g_mhs3_producer_p1_us = 0;
thread_local uint64_t g_mhs3_producer_p2_us = 0;
thread_local uint64_t g_mhs3_producer_p3_us = 0;
thread_local uint64_t g_mhs3_producer_p4_us = 0;

// Build #19:
// Probe the crash site at RVA 0x237559. At this point the game has already
// executed:
//     mov 0x48(%rsi), %rax
// but has NOT yet executed:
//     vbroadcastss 0x34(%rax), %xmm7
//
// Therefore RAX is the exact +0x48 child pointer that was NULL in the
// Build #18 crash, and observing it here requires no dereference of RAX.
std::atomic<uint64_t> g_mhs3_crash_probe_calls{0};
std::atomic<uint64_t> g_mhs3_crash_probe_null48{0};
std::atomic<uintptr_t> g_mhs3_crash_probe_last_object{0};
std::atomic<uintptr_t> g_mhs3_crash_probe_last_child40{0};
std::atomic<uintptr_t> g_mhs3_crash_probe_last_child48{0};

MHS3WaitCallsiteStats g_mhs3_wait_a_stats{};
MHS3WaitCallsiteStats g_mhs3_wait_b_stats{};

static uint64_t mhs3_steady_now_us() {
    return (uint64_t)std::chrono::duration_cast<std::chrono::microseconds>(
        std::chrono::steady_clock::now().time_since_epoch()
    ).count();
}

std::chrono::steady_clock::time_point g_mhs3_wait_last_report =
    std::chrono::steady_clock::now();

void record_mhs3_wait_duration(
    MHS3WaitCallsiteStats& stats,
    std::chrono::steady_clock::time_point start
) {
    if (start.time_since_epoch().count() == 0) {
        return;
    }

    const auto elapsed_us =
        (uint64_t)std::chrono::duration_cast<std::chrono::microseconds>(
            std::chrono::steady_clock::now() - start
        ).count();

    ++stats.calls;
    stats.total_us += elapsed_us;
    stats.max_us = std::max(stats.max_us, elapsed_us);

    if (elapsed_us >= 50000) {
        ++stats.over_50ms;
    }

    if (elapsed_us >= 100000) {
        ++stats.over_100ms;
    }

    if (elapsed_us >= 200000) {
        ++stats.over_200ms;
    }

    if (elapsed_us >= 500000) {
        ++stats.over_500ms;
    }
}

void maybe_report_mhs3_wait_callsites() {
    const auto now = std::chrono::steady_clock::now();

    if (now - g_mhs3_wait_last_report < std::chrono::seconds(5)) {
        return;
    }

    const double a_avg =
        g_mhs3_wait_a_stats.calls > 0
            ? (double)g_mhs3_wait_a_stats.total_us /
              (double)g_mhs3_wait_a_stats.calls
            : 0.0;

    const double b_avg =
        g_mhs3_wait_b_stats.calls > 0
            ? (double)g_mhs3_wait_b_stats.total_us /
              (double)g_mhs3_wait_b_stats.calls
            : 0.0;

    spdlog::info(
        "[MHS3 WAIT EKG] "
        "WaitA calls={} avg={:.2f} us max={} us "
        ">50ms={} >100ms={} >200ms={} >500ms={} | "
        "WaitB calls={} avg={:.2f} us max={} us "
        ">50ms={} >100ms={} >200ms={} >500ms={}",
        g_mhs3_wait_a_stats.calls,
        a_avg,
        g_mhs3_wait_a_stats.max_us,
        g_mhs3_wait_a_stats.over_50ms,
        g_mhs3_wait_a_stats.over_100ms,
        g_mhs3_wait_a_stats.over_200ms,
        g_mhs3_wait_a_stats.over_500ms,
        g_mhs3_wait_b_stats.calls,
        b_avg,
        g_mhs3_wait_b_stats.max_us,
        g_mhs3_wait_b_stats.over_50ms,
        g_mhs3_wait_b_stats.over_100ms,
        g_mhs3_wait_b_stats.over_200ms,
        g_mhs3_wait_b_stats.over_500ms
    );

    spdlog::info(
        "[MHS3 CRASH PROBE] calls={} null48={} "
        "last_object=0x{:x} last_child40=0x{:x} last_child48=0x{:x}",
        g_mhs3_crash_probe_calls.load(std::memory_order_acquire),
        g_mhs3_crash_probe_null48.load(std::memory_order_acquire),
        g_mhs3_crash_probe_last_object.load(std::memory_order_acquire),
        g_mhs3_crash_probe_last_child40.load(std::memory_order_acquire),
        g_mhs3_crash_probe_last_child48.load(std::memory_order_acquire)
    );

    g_mhs3_wait_a_stats = {};
    g_mhs3_wait_b_stats = {};
    g_mhs3_wait_last_report = now;
}
}

std::optional<std::string> Hooks::hook_mhs3_waitrendering_callsites() {
    const auto base = (uintptr_t)g_framework->get_module();

    // MHS3 Build #16:
    // WaitRendering callsite A:
    //   call @ RVA 0x236ff9
    //   return/next instruction @ RVA 0x236fff
    //
    // WaitRendering callsite B:
    //   call @ RVA 0x2370ba
    //   return/next instruction @ RVA 0x2370c0
    constexpr uintptr_t wait_a_before_rva = 0x236ff9;
    constexpr uintptr_t wait_a_after_rva  = 0x236fff;
    constexpr uintptr_t wait_b_before_rva = 0x2370ba;
    constexpr uintptr_t wait_b_after_rva  = 0x2370c0;

    const auto wait_a_before = base + wait_a_before_rva;
    const auto wait_a_after  = base + wait_a_after_rva;
    const auto wait_b_before = base + wait_b_before_rva;
    const auto wait_b_after  = base + wait_b_after_rva;

    spdlog::info(
        "[MHS3 WAIT EKG] Installing WaitRendering callsite probes: "
        "A {:x}->{:x}, B {:x}->{:x}",
        wait_a_before,
        wait_a_after,
        wait_b_before,
        wait_b_after
    );

    m_mhs3_wait_a_before_hook =
        safetyhook::create_mid((void*)wait_a_before, &Hooks::mhs3_wait_a_before);

    if (!m_mhs3_wait_a_before_hook) {
        return "Failed to install MHS3 WaitRendering WaitA-before probe";
    }

    m_mhs3_wait_a_after_hook =
        safetyhook::create_mid((void*)wait_a_after, &Hooks::mhs3_wait_a_after);

    if (!m_mhs3_wait_a_after_hook) {
        return "Failed to install MHS3 WaitRendering WaitA-after probe";
    }

    m_mhs3_wait_b_before_hook =
        safetyhook::create_mid((void*)wait_b_before, &Hooks::mhs3_wait_b_before);

    if (!m_mhs3_wait_b_before_hook) {
        return "Failed to install MHS3 WaitRendering WaitB-before probe";
    }

    m_mhs3_wait_b_after_hook =
        safetyhook::create_mid((void*)wait_b_after, &Hooks::mhs3_wait_b_after);

    if (!m_mhs3_wait_b_after_hook) {
        return "Failed to install MHS3 WaitRendering WaitB-after probe";
    }

    // Build #27:
    // Time the Stage-A producer itself instead of another wait primitive.
    constexpr uintptr_t producer_p0_rva = 0x03fee50;
    constexpr uintptr_t producer_p1_rva = 0x03fee6f;
    constexpr uintptr_t producer_p2_rva = 0x03fef13;
    constexpr uintptr_t producer_p3_rva = 0x03fef30;
    constexpr uintptr_t producer_p4_rva = 0x03fef42;

    // BUILD31G:
    // Paired timing plus count/total/max bookkeeping.
    constexpr uintptr_t producer_object_loop_begin_bookkeeping_rva = 0x03fee8a;
    constexpr uintptr_t producer_object_loop_end_bookkeeping_rva   = 0x03fef06;

    m_mhs3_producer_object_loop_begin_bookkeeping_hook =
        safetyhook::create_mid(
            (void*)(base + producer_object_loop_begin_bookkeeping_rva),
            &Hooks::mhs3_producer_object_loop_begin_bookkeeping
        );

    if (!m_mhs3_producer_object_loop_begin_bookkeeping_hook) {
        return "Failed to install MHS3 Build #31G begin-bookkeeping probe";
    }

    m_mhs3_producer_object_loop_end_bookkeeping_hook =
        safetyhook::create_mid(
            (void*)(base + producer_object_loop_end_bookkeeping_rva),
            &Hooks::mhs3_producer_object_loop_end_bookkeeping
        );

    if (!m_mhs3_producer_object_loop_end_bookkeeping_hook) {
        return "Failed to install MHS3 Build #31G end-bookkeeping probe";
    }

    spdlog::info(
        "[MHS3 BUILD31G] object-loop bookkeeping probes installed: "
        "begin=0x{:x} end=0x{:x}",
        base + producer_object_loop_begin_bookkeeping_rva,
        base + producer_object_loop_end_bookkeeping_rva
    );

    m_mhs3_producer_p0_hook =
        safetyhook::create_mid(
            (void*)(base + producer_p0_rva),
            &Hooks::mhs3_producer_p0
        );

    if (!m_mhs3_producer_p0_hook) {
        return "Failed to install MHS3 producer P0 probe";
    }

    m_mhs3_producer_p1_hook =
        safetyhook::create_mid(
            (void*)(base + producer_p1_rva),
            &Hooks::mhs3_producer_p1
        );

    if (!m_mhs3_producer_p1_hook) {
        return "Failed to install MHS3 producer P1 probe";
    }

    m_mhs3_producer_p2_hook =
        safetyhook::create_mid(
            (void*)(base + producer_p2_rva),
            &Hooks::mhs3_producer_p2
        );

    if (!m_mhs3_producer_p2_hook) {
        return "Failed to install MHS3 producer P2 probe";
    }

    m_mhs3_producer_p3_hook =
        safetyhook::create_mid(
            (void*)(base + producer_p3_rva),
            &Hooks::mhs3_producer_p3
        );

    if (!m_mhs3_producer_p3_hook) {
        return "Failed to install MHS3 producer P3 probe";
    }

    m_mhs3_producer_p4_hook =
        safetyhook::create_mid(
            (void*)(base + producer_p4_rva),
            &Hooks::mhs3_producer_p4
        );

    if (!m_mhs3_producer_p4_hook) {
        return "Failed to install MHS3 producer P4 probe";
    }

    spdlog::info(
        "[MHS3 PRODUCER TIMING] checkpoints installed: "
        "P0=0x{:x} P1=0x{:x} P2=0x{:x} P3=0x{:x} P4=0x{:x}",
        base + producer_p0_rva,
        base + producer_p1_rva,
        base + producer_p2_rva,
        base + producer_p3_rva,
        base + producer_p4_rva
    );

    // Build #19:
    // Crash site from the Build #18 minidump:
    //
    //   140237555  mov 0x48(%rsi), %rax
    //   140237559  vbroadcastss 0x34(%rax), %xmm7   <-- crashed here
    //
    // Hooking at 0x237559 lets us inspect RAX after the +0x48 load but
    // before the game dereferences it.
    constexpr uintptr_t crash_state_probe_rva = 0x237559;
    const auto crash_state_probe = base + crash_state_probe_rva;

    m_mhs3_crash_state_hook =
        safetyhook::create_mid(
            (void*)crash_state_probe,
            &Hooks::mhs3_crash_state_probe
        );

    if (!m_mhs3_crash_state_hook) {
        return "Failed to install MHS3 crash-state +0x48 probe";
    }

    spdlog::info(
        "[MHS3 CRASH PROBE] installed at 0x{:x}",
        crash_state_probe
    );

    // Build #18:
    // Hook the actual runtime SetEvent export instead of guessing at an
    // executable-side thunk. Under Proton/Wine this resolves to the real
    // implementation used by the game's IAT.
    auto kernel32 = GetModuleHandleA("kernel32.dll");

    if (kernel32 == nullptr) {
        return "Failed to get kernel32.dll for MHS3 SetEvent probe";
    }

    auto setevent = GetProcAddress(kernel32, "SetEvent");

    if (setevent == nullptr) {
        return "Failed to resolve runtime SetEvent for MHS3 probe";
    }

    m_mhs3_setevent_hook = std::make_unique<FunctionHookMinHook>(
        setevent,
        &Hooks::mhs3_setevent_hook
    );

    if (!m_mhs3_setevent_hook->create()) {
        m_mhs3_setevent_hook.reset();
        return "Failed to hook runtime SetEvent for MHS3 WaitA probe";
    }

    spdlog::info(
        "[MHS3 WAIT EKG] WaitRendering probes + runtime SetEvent hook installed at 0x{:x}",
        (uintptr_t)setevent
    );

    // Build #24:
    // The worker loop at RVA 0x07992b4d calls through the game's IAT slot
    // at RVA 0x08dab118. Build #23 proved that mid-hooking the tiny indirect
    // callsite itself is unsafe, so hook the resolved runtime target instead.
    //
    // The hook filters by _ReturnAddress(), so unrelated users of this shared
    // wait-like primitive immediately pass through without diagnostic work.
    constexpr uintptr_t worker_wait_iat_rva = 0x08dab118;

    auto worker_wait_target =
        *(void**)(base + worker_wait_iat_rva);

    if (worker_wait_target == nullptr) {
        return "Failed to resolve MHS3 worker wait target from IAT";
    }

    m_mhs3_worker_wait_hook =
        std::make_unique<FunctionHookMinHook>(
            worker_wait_target,
            &Hooks::mhs3_worker_wait_hook
        );

    if (!m_mhs3_worker_wait_hook->create()) {
        m_mhs3_worker_wait_hook.reset();
        return "Failed to hook MHS3 worker wait runtime target";
    }

    spdlog::info(
        "[MHS3 WORKER WAIT] runtime target hook installed at 0x{:x}",
        (uintptr_t)worker_wait_target
    );

    return std::nullopt;
}

void Hooks::mhs3_producer_object_loop_begin_bookkeeping(
    safetyhook::Context& context
) {
    (void)context;

    g_mhs3_object_loop_begin_us =
        mhs3_steady_now_us();
}

void Hooks::mhs3_producer_object_loop_end_bookkeeping(
    safetyhook::Context& context
) {
    (void)context;

    const auto end_us = mhs3_steady_now_us();
    const auto begin_us = g_mhs3_object_loop_begin_us;

    if (begin_us == 0 || end_us < begin_us) {
        return;
    }

    const auto elapsed_us = end_us - begin_us;

    ++g_mhs3_object_loop_count;
    g_mhs3_object_loop_total_us += elapsed_us;

    if (elapsed_us > g_mhs3_object_loop_max_us) {
        g_mhs3_object_loop_max_us = elapsed_us;
    }
}

void Hooks::mhs3_producer_p0(safetyhook::Context& context) {
    (void)context;

    g_mhs3_producer_p0_us = mhs3_steady_now_us();
    g_mhs3_producer_p1_us = 0;
    g_mhs3_producer_p2_us = 0;
    g_mhs3_producer_p3_us = 0;
    g_mhs3_producer_p4_us = 0;
}

void Hooks::mhs3_producer_p1(safetyhook::Context& context) {
    (void)context;

    if (g_mhs3_producer_p0_us != 0) {
        g_mhs3_producer_p1_us = mhs3_steady_now_us();
    }
}

void Hooks::mhs3_producer_p2(safetyhook::Context& context) {
    (void)context;

    if (g_mhs3_producer_p0_us != 0) {
        g_mhs3_producer_p2_us = mhs3_steady_now_us();
    }
}

void Hooks::mhs3_producer_p3(safetyhook::Context& context) {
    (void)context;

    if (g_mhs3_producer_p0_us != 0) {
        g_mhs3_producer_p3_us = mhs3_steady_now_us();
    }
}

void Hooks::mhs3_producer_p4(safetyhook::Context& context) {
    (void)context;

    if (g_mhs3_producer_p0_us != 0) {
        g_mhs3_producer_p4_us = mhs3_steady_now_us();
    }
}

void Hooks::mhs3_wait_a_before(safetyhook::Context& context) {
    const auto now = std::chrono::steady_clock::now();
    const auto now_us = mhs3_steady_now_us();

    g_mhs3_wait_a_start = now;

    // RCX is the exact HANDLE passed to WaitForSingleObject(WaitA, INFINITE).
    g_mhs3_wait_a_handle.store(
        (uintptr_t)context.rcx,
        std::memory_order_release
    );

    const auto epoch =
        g_mhs3_wait_a_epoch.fetch_add(1, std::memory_order_acq_rel) + 1;

    // Shared across threads so the SetEvent thread can correlate itself with
    // the currently outstanding WaitA.
    g_mhs3_wait_a_start_us.store(now_us, std::memory_order_release);
    g_mhs3_wait_a_signal_epoch.store(0, std::memory_order_release);
    g_mhs3_wait_a_signal_us.store(0, std::memory_order_release);

    (void)epoch;
}

void Hooks::mhs3_crash_state_probe(safetyhook::Context& context) {
    const auto object = (uintptr_t)context.rsi;
    const auto child40 = (uintptr_t)context.rcx;
    const auto child48 = (uintptr_t)context.rax;

    g_mhs3_crash_probe_calls.fetch_add(1, std::memory_order_relaxed);
    g_mhs3_crash_probe_last_object.store(object, std::memory_order_release);
    g_mhs3_crash_probe_last_child40.store(child40, std::memory_order_release);
    g_mhs3_crash_probe_last_child48.store(child48, std::memory_order_release);

    if (child48 != 0) {
        return;
    }

    g_mhs3_crash_probe_null48.fetch_add(1, std::memory_order_relaxed);

    const auto now_us = mhs3_steady_now_us();
    const auto wait_start_us =
        g_mhs3_wait_a_start_us.load(std::memory_order_acquire);

    uint64_t outstanding_wait_us = 0;

    if (wait_start_us != 0 && now_us >= wait_start_us) {
        outstanding_wait_us = now_us - wait_start_us;
    }

    spdlog::warn(
        "[MHS3 CRASH PROBE] NULL +0x48 BEFORE CRASH "
        "tid={} object=0x{:x} child40=0x{:x} child48=0x{:x} "
        "waitA_outstanding={} us waitA_epoch={} handle=0x{:x}",
        GetCurrentThreadId(),
        object,
        child40,
        child48,
        outstanding_wait_us,
        g_mhs3_wait_a_epoch.load(std::memory_order_acquire),
        g_mhs3_wait_a_handle.load(std::memory_order_acquire)
    );
}

DWORD WINAPI Hooks::mhs3_worker_wait_hook(HANDLE handle, DWORD timeout) {
    auto original =
        g_hook->m_mhs3_worker_wait_hook->get_original<
            decltype(WaitForSingleObject)
        >();

    const auto game_base =
        (uintptr_t)g_framework->get_module();

    const auto return_address =
        (uintptr_t)_ReturnAddress();

    constexpr uintptr_t downstream_wait_return_rva = 0x07992b4f;
    constexpr uintptr_t upstream_wait_return_rva = 0x0482d745;

    const auto downstream_wait_return =
        game_base + downstream_wait_return_rva;

    const auto upstream_wait_return =
        game_base + upstream_wait_return_rva;

    // --------------------------------------------------------
    // Build #26 upstream wait:
    // thread 564 waiting on object +0x3a80.
    // --------------------------------------------------------
    if (return_address == upstream_wait_return) {
        const auto start_us = mhs3_steady_now_us();

        const auto epoch =
            g_mhs3_upstream_wait_epoch.fetch_add(
                1,
                std::memory_order_acq_rel
            ) + 1;

        g_mhs3_upstream_wait_handle.store(
            (uintptr_t)handle,
            std::memory_order_release
        );

        g_mhs3_upstream_wait_start_us.store(
            start_us,
            std::memory_order_release
        );

        g_mhs3_upstream_signal_us.store(
            0,
            std::memory_order_release
        );

        g_mhs3_upstream_signal_epoch.store(
            0,
            std::memory_order_release
        );

        const auto result = original(handle, timeout);
        const auto end_us = mhs3_steady_now_us();

        const auto signal_epoch =
            g_mhs3_upstream_signal_epoch.load(
                std::memory_order_acquire
            );

        const auto signal_us =
            g_mhs3_upstream_signal_us.load(
                std::memory_order_acquire
            );

        // B timestamp.
        g_mhs3_upstream_wake_us.store(
            end_us,
            std::memory_order_release
        );

        g_mhs3_upstream_wake_epoch.store(
            epoch,
            std::memory_order_release
        );

        g_mhs3_upstream_wait_start_us.store(
            0,
            std::memory_order_release
        );

        const auto total_us =
            end_us >= start_us
                ? end_us - start_us
                : 0;

        if (total_us >= 50000) {
            if (
                signal_epoch == epoch &&
                signal_us >= start_us &&
                signal_us <= end_us
            ) {
                const auto wait_to_a_us =
                    signal_us - start_us;

                const auto a_to_b_us =
                    end_us - signal_us;

                spdlog::warn(
                    "[MHS3 THREE-STAGE AB] "
                    "total={} us wait_to_A={} us A_to_B={} us "
                    "epoch={} handle=0x{:x} result=0x{:x}",
                    total_us,
                    wait_to_a_us,
                    a_to_b_us,
                    epoch,
                    (uintptr_t)handle,
                    result
                );
            } else {
                spdlog::warn(
                    "[MHS3 THREE-STAGE AB] "
                    "total={} us NO_MATCHING_A "
                    "epoch={} handle=0x{:x} result=0x{:x}",
                    total_us,
                    epoch,
                    (uintptr_t)handle,
                    result
                );
            }
        }

        return result;
    }

    // --------------------------------------------------------
    // Existing downstream worker wait:
    // thread 380 waiting on object +0x3a70.
    // --------------------------------------------------------
    if (return_address != downstream_wait_return) {
        return original(handle, timeout);
    }

    const auto start_us = mhs3_steady_now_us();

    const auto worker_epoch =
        g_mhs3_worker_wait_epoch.fetch_add(
            1,
            std::memory_order_acq_rel
        ) + 1;

    g_mhs3_worker_wait_handle.store(
        (uintptr_t)handle,
        std::memory_order_release
    );

    g_mhs3_worker_wait_start_us.store(
        start_us,
        std::memory_order_release
    );

    const auto result = original(handle, timeout);
    const auto end_us = mhs3_steady_now_us();

    g_mhs3_worker_wait_start_us.store(
        0,
        std::memory_order_release
    );

    const auto elapsed_us =
        end_us >= start_us
            ? end_us - start_us
            : 0;

    if (elapsed_us >= 50000) {
        spdlog::warn(
            "[MHS3 WORKER WAIT] "
            "tid={} duration={} us handle=0x{:x} timeout=0x{:x} "
            "result=0x{:x} caller=0x{:x} worker_epoch={} "
            "waitA_epoch={} waitA_handle=0x{:x}",
            GetCurrentThreadId(),
            elapsed_us,
            (uintptr_t)handle,
            timeout,
            result,
            return_address,
            worker_epoch,
            g_mhs3_wait_a_epoch.load(std::memory_order_acquire),
            g_mhs3_wait_a_handle.load(std::memory_order_acquire)
        );
    }

    return result;
}

BOOL WINAPI Hooks::mhs3_setevent_hook(HANDLE event) {
    // Build #26:
    // Low-volume three-stage synchronization timing.
    //
    // A = producer signals +0x3a80, return RVA 0x003fef5f
    // B = upstream Wait(+0x3a80) returns at RVA 0x0482d745
    // C = thread 564 signals +0x3a70, return RVA 0x0482d7c1

    const auto game_base =
        (uintptr_t)g_framework->get_module();

    const auto return_address =
        (uintptr_t)_ReturnAddress();

    constexpr uintptr_t stage_a_return_rva = 0x003fef5f;
    constexpr uintptr_t stage_c_return_rva = 0x0482d7c1;

    const auto stage_a_return =
        game_base + stage_a_return_rva;

    const auto stage_c_return =
        game_base + stage_c_return_rva;

    // --------------------------------------------------------
    // Stage A:
    // Producer signals the event that thread 564 waits on.
    // --------------------------------------------------------
    if (return_address == stage_a_return) {
        const auto upstream_handle =
            g_mhs3_upstream_wait_handle.load(
                std::memory_order_acquire
            );

        const auto upstream_start_us =
            g_mhs3_upstream_wait_start_us.load(
                std::memory_order_acquire
            );

        const auto upstream_epoch =
            g_mhs3_upstream_wait_epoch.load(
                std::memory_order_acquire
            );

        if (
            upstream_handle != 0 &&
            upstream_start_us != 0 &&
            (uintptr_t)event == upstream_handle
        ) {
            const auto now_us = mhs3_steady_now_us();

            // Build #27:
            // If this producer cycle was slow, break down where its
            // pre-signal time actually went.
            const auto p0 = g_mhs3_producer_p0_us;
            const auto p1 = g_mhs3_producer_p1_us;
            const auto p2 = g_mhs3_producer_p2_us;
            const auto p3 = g_mhs3_producer_p3_us;
            const auto p4 = g_mhs3_producer_p4_us;

            if (
                p0 != 0 &&
                p1 >= p0 &&
                p2 >= p1 &&
                p3 >= p2 &&
                p4 >= p3 &&
                now_us >= p4
            ) {
                const auto total_us = now_us - p0;

                if (total_us >= 50000) {
                    spdlog::warn(
                        "[MHS3 PRODUCER TIMING] "
                        "total={} us setup={} us object_region={} us "
                        "post_group1={} us post_group2={} us "
                        "final_to_A={} us epoch={} tid={}",
                        total_us,
                        p1 - p0,
                        p2 - p1,
                        p3 - p2,
                        p4 - p3,
                        now_us - p4,
                        upstream_epoch,
                        GetCurrentThreadId()
                    );
                }
            }

            // Stage A completes this producer sample.
            g_mhs3_producer_p0_us = 0;
            g_mhs3_producer_p1_us = 0;
            g_mhs3_producer_p2_us = 0;
            g_mhs3_producer_p3_us = 0;
            g_mhs3_producer_p4_us = 0;

            g_mhs3_upstream_signal_us.store(
                now_us,
                std::memory_order_release
            );

            g_mhs3_upstream_signal_epoch.store(
                upstream_epoch,
                std::memory_order_release
            );

            if (now_us >= upstream_start_us) {
                const auto wait_to_a_us =
                    now_us - upstream_start_us;

                if (wait_to_a_us >= 50000) {
                    spdlog::warn(
                        "[MHS3 THREE-STAGE A] "
                        "wait_to_A={} us epoch={} "
                        "handle=0x{:x}",
                        wait_to_a_us,
                        upstream_epoch,
                        upstream_handle
                    );
                }
            }
        }
    }

    // --------------------------------------------------------
    // Stage C:
    // Thread 564 signals the downstream worker event.
    //
    // Report only if either B->C itself is slow OR the
    // downstream worker has already been blocked for >=50 ms.
    // --------------------------------------------------------
    if (return_address == stage_c_return) {
        const auto worker_handle =
            g_mhs3_worker_wait_handle.load(
                std::memory_order_acquire
            );

        if (
            worker_handle != 0 &&
            (uintptr_t)event == worker_handle
        ) {
            const auto now_us = mhs3_steady_now_us();

            const auto upstream_wake_us =
                g_mhs3_upstream_wake_us.load(
                    std::memory_order_acquire
                );

            const auto upstream_wake_epoch =
                g_mhs3_upstream_wake_epoch.load(
                    std::memory_order_acquire
                );

            const auto worker_start_us =
                g_mhs3_worker_wait_start_us.load(
                    std::memory_order_acquire
                );

            uint64_t b_to_c_us = 0;
            uint64_t downstream_wait_us = 0;

            // Ignore obviously stale B timestamps.
            if (
                upstream_wake_us != 0 &&
                now_us >= upstream_wake_us &&
                now_us - upstream_wake_us <= 1000000
            ) {
                b_to_c_us = now_us - upstream_wake_us;
            }

            if (
                worker_start_us != 0 &&
                now_us >= worker_start_us
            ) {
                downstream_wait_us =
                    now_us - worker_start_us;
            }

            if (
                b_to_c_us >= 50000 ||
                downstream_wait_us >= 50000
            ) {
                spdlog::warn(
                    "[MHS3 THREE-STAGE C] "
                    "B_to_C={} us downstream_wait={} us "
                    "upstream_epoch={} worker_epoch={} "
                    "handle=0x{:x}",
                    b_to_c_us,
                    downstream_wait_us,
                    upstream_wake_epoch,
                    g_mhs3_worker_wait_epoch.load(
                        std::memory_order_acquire
                    ),
                    worker_handle
                );
            }
        }
    }

    const auto wait_a_handle =
        g_mhs3_wait_a_handle.load(std::memory_order_acquire);

    if (
        wait_a_handle != 0 &&
        (uintptr_t)event == wait_a_handle
    ) {
        const auto epoch =
            g_mhs3_wait_a_epoch.load(std::memory_order_acquire);

        const auto start_us =
            g_mhs3_wait_a_start_us.load(std::memory_order_acquire);

        if (epoch != 0 && start_us != 0) {
            const auto now_us = mhs3_steady_now_us();

            if (now_us >= start_us) {
                const auto elapsed_us = now_us - start_us;

                g_mhs3_wait_a_signal_us.store(
                    elapsed_us,
                    std::memory_order_release
                );

                g_mhs3_wait_a_signal_epoch.store(
                    epoch,
                    std::memory_order_release
                );

                // Build #21:
                // Identify the thread and callsite that finally signals the
                // event currently blocking WaitA. Keep this strictly inside
                // the matching-handle path so unrelated SetEvent traffic is
                // not logged.
                const auto return_address =
                    (uintptr_t)_ReturnAddress();

                const auto game_base =
                    (uintptr_t)g_framework->get_module();

                uintptr_t game_rva = 0;

                if (
                    return_address >= game_base &&
                    game_base != 0
                ) {
                    game_rva = return_address - game_base;
                }

                spdlog::info(
                    "[MHS3 WAITA SIGNALER] "
                    "tid={} return_address=0x{:x} game_rva=0x{:x} "
                    "wait_to_signal={} us epoch={} handle=0x{:x}",
                    GetCurrentThreadId(),
                    return_address,
                    game_rva,
                    elapsed_us,
                    epoch,
                    wait_a_handle
                );

                // Build #22:
                // For genuinely slow WaitA signals, capture a small raw
                // stack from the signaling thread. Do not symbolize here;
                // logging raw addresses keeps this probe lightweight and
                // avoids doing expensive module/symbol work in SetEvent.
                if (elapsed_us >= 50000) {
                    void* frames[12]{};

                    const auto frame_count =
                        CaptureStackBackTrace(
                            0,
                            (DWORD)std::size(frames),
                            frames,
                            nullptr
                        );

                    spdlog::warn(
                        "[MHS3 WAITA STACK] "
                        "tid={} wait_to_signal={} us epoch={} "
                        "frames={} "
                        "f0=0x{:x} f1=0x{:x} f2=0x{:x} f3=0x{:x} "
                        "f4=0x{:x} f5=0x{:x} f6=0x{:x} f7=0x{:x} "
                        "f8=0x{:x} f9=0x{:x} f10=0x{:x} f11=0x{:x}",
                        GetCurrentThreadId(),
                        elapsed_us,
                        epoch,
                        frame_count,
                        frame_count > 0 ? (uintptr_t)frames[0] : 0,
                        frame_count > 1 ? (uintptr_t)frames[1] : 0,
                        frame_count > 2 ? (uintptr_t)frames[2] : 0,
                        frame_count > 3 ? (uintptr_t)frames[3] : 0,
                        frame_count > 4 ? (uintptr_t)frames[4] : 0,
                        frame_count > 5 ? (uintptr_t)frames[5] : 0,
                        frame_count > 6 ? (uintptr_t)frames[6] : 0,
                        frame_count > 7 ? (uintptr_t)frames[7] : 0,
                        frame_count > 8 ? (uintptr_t)frames[8] : 0,
                        frame_count > 9 ? (uintptr_t)frames[9] : 0,
                        frame_count > 10 ? (uintptr_t)frames[10] : 0,
                        frame_count > 11 ? (uintptr_t)frames[11] : 0
                    );
                }
            }
        }
    }

    auto original =
        g_hook->m_mhs3_setevent_hook->get_original<decltype(SetEvent)>();

    return original(event);
}

void Hooks::mhs3_wait_a_after(safetyhook::Context& context) {
    (void)context;

    if (g_mhs3_wait_a_start.time_since_epoch().count() == 0) {
        return;
    }

    const auto now = std::chrono::steady_clock::now();

    const auto total_us =
        (uint64_t)std::chrono::duration_cast<std::chrono::microseconds>(
            now - g_mhs3_wait_a_start
        ).count();

    record_mhs3_wait_duration(
        g_mhs3_wait_a_stats,
        g_mhs3_wait_a_start
    );

    const auto epoch =
        g_mhs3_wait_a_epoch.load(std::memory_order_acquire);

    const auto signal_epoch =
        g_mhs3_wait_a_signal_epoch.load(std::memory_order_acquire);

    const auto signal_us =
        g_mhs3_wait_a_signal_us.load(std::memory_order_acquire);

    if (total_us >= 50000) {
        if (signal_epoch == epoch && signal_us <= total_us) {
            const auto wake_after_signal_us = total_us - signal_us;

            spdlog::warn(
                "[MHS3 WAITA SIGNAL] total={} us wait_to_signal={} us signal_to_wake={} us handle=0x{:x}",
                total_us,
                signal_us,
                wake_after_signal_us,
                g_mhs3_wait_a_handle.load(std::memory_order_acquire)
            );
        } else {
            spdlog::warn(
                "[MHS3 WAITA SIGNAL] total={} us NO_MATCHING_SETEVENT handle=0x{:x}",
                total_us,
                g_mhs3_wait_a_handle.load(std::memory_order_acquire)
            );
        }
    }

    g_mhs3_wait_a_start = {};
    g_mhs3_wait_a_start_us.store(0, std::memory_order_release);

    maybe_report_mhs3_wait_callsites();
}

void Hooks::mhs3_wait_b_before(safetyhook::Context& context) {
    (void)context;
    g_mhs3_wait_b_start = std::chrono::steady_clock::now();
}

void Hooks::mhs3_wait_b_after(safetyhook::Context& context) {
    (void)context;

    record_mhs3_wait_duration(
        g_mhs3_wait_b_stats,
        g_mhs3_wait_b_start
    );

    g_mhs3_wait_b_start = {};
    maybe_report_mhs3_wait_callsites();
}

void Hooks::update_behavior_hook_internal(void* entry) {
    record_mhs3_cross_stage(MHS3CrossStage::UpdateBehavior);
    static MHS3EntryCadenceStats stats{};

    run_mhs3_entry_cadence(
        "UpdateBehavior",
        entry,
        m_update_behavior_original,
        stats
    );
}

void Hooks::update_behavior_hook(void* entry) {
    g_hook->update_behavior_hook_internal(entry);
}

void Hooks::prepare_rendering_hook_internal(void* entry) {
    record_mhs3_cross_stage(MHS3CrossStage::PrepareRendering);
    static MHS3EntryCadenceStats stats{};

    run_mhs3_entry_cadence(
        "PrepareRendering",
        entry,
        m_prepare_rendering_original,
        stats
    );
}

void Hooks::prepare_rendering_hook(void* entry) {
    g_hook->prepare_rendering_hook_internal(entry);
}

void Hooks::mhs3_wait_rendering_hook_internal(void* entry) {
    record_mhs3_cross_stage(MHS3CrossStage::WaitRendering);
    static MHS3EntryCadenceStats stats{};

    run_mhs3_entry_cadence(
        "WaitRendering",
        entry,
        m_wait_rendering_original,
        stats
    );
}

void Hooks::mhs3_wait_rendering_hook(void* entry) {
    g_hook->mhs3_wait_rendering_hook_internal(entry);
}

void Hooks::begin_rendering_hook_internal(void* entry) {
    record_mhs3_cross_stage(MHS3CrossStage::BeginRendering);
    if (m_begin_rendering_original == nullptr) {
        return;
    }

    // MHS3 Build #10/#11 instrumentation.
    static uint64_t begin_rendering_count = 0;

    static uint64_t script_runner_count = 0;
    static uint64_t script_runner_total_us = 0;
    static uint64_t script_runner_max_us = 0;

    static uint64_t original_count = 0;
    static uint64_t original_total_us = 0;
    static uint64_t original_max_us = 0;

    static auto last_report = std::chrono::steady_clock::now();

    // MHS3 Build #12 instrumentation: measure time between BeginRendering calls.
    static auto last_begin_rendering = std::chrono::steady_clock::time_point{};
    static uint64_t begin_gap_count = 0;
    static uint64_t begin_gap_total_us = 0;
    static uint64_t begin_gap_max_us = 0;
    static uint64_t begin_gap_over_50ms = 0;
    static uint64_t begin_gap_over_100ms = 0;
    static uint64_t begin_gap_over_200ms = 0;

    const auto begin_now = std::chrono::steady_clock::now();

    if (last_begin_rendering.time_since_epoch().count() != 0) {
        const auto begin_gap_us = (uint64_t)std::chrono::duration_cast<std::chrono::microseconds>(
            begin_now - last_begin_rendering
        ).count();

        ++begin_gap_count;
        begin_gap_total_us += begin_gap_us;
        begin_gap_max_us = std::max(begin_gap_max_us, begin_gap_us);

        if (begin_gap_us >= 50000) {
            ++begin_gap_over_50ms;
        }

        if (begin_gap_us >= 100000) {
            ++begin_gap_over_100ms;
        }

        if (begin_gap_us >= 200000) {
            ++begin_gap_over_200ms;
        }
    }

    last_begin_rendering = begin_now;

    ++begin_rendering_count;

    // MHS3 diagnostic: run ONLY the Lua/ScriptRunner heartbeat.
    if (g_framework->is_game_data_initialized()) {
        const auto start = std::chrono::steady_clock::now();

        ScriptRunner::get()->on_frame();

        const auto elapsed_us = (uint64_t)std::chrono::duration_cast<std::chrono::microseconds>(
            std::chrono::steady_clock::now() - start
        ).count();

        ++script_runner_count;
        script_runner_total_us += elapsed_us;
        script_runner_max_us = std::max(script_runner_max_us, elapsed_us);

        if (elapsed_us >= 50000) {
            spdlog::warn(
                "[MHS3 EKG] Slow ScriptRunner::on_frame(): {} us ({:.2f} ms)",
                elapsed_us,
                elapsed_us / 1000.0
            );
        }
    }

    // Build #11: time the game's real BeginRendering callback separately.
    const auto original_start = std::chrono::steady_clock::now();

    m_begin_rendering_original(entry);

    const auto original_elapsed_us = (uint64_t)std::chrono::duration_cast<std::chrono::microseconds>(
        std::chrono::steady_clock::now() - original_start
    ).count();

    ++original_count;
    original_total_us += original_elapsed_us;
    original_max_us = std::max(original_max_us, original_elapsed_us);

    if (original_elapsed_us >= 50000) {
        spdlog::warn(
            "[MHS3 EKG] Slow original BeginRendering: {} us ({:.2f} ms)",
            original_elapsed_us,
            original_elapsed_us / 1000.0
        );
    }

    const auto now = std::chrono::steady_clock::now();

    if (now - last_report >= std::chrono::seconds(5)) {
        const double script_avg_us = script_runner_count > 0
            ? (double)script_runner_total_us / (double)script_runner_count
            : 0.0;

        const double original_avg_us = original_count > 0
            ? (double)original_total_us / (double)original_count
            : 0.0;

        const double begin_gap_avg_us = begin_gap_count > 0
            ? (double)begin_gap_total_us / (double)begin_gap_count
            : 0.0;

        spdlog::info(
            "[MHS3 EKG] BeginRendering={} ScriptRunner={} avg={:.2f} us max={} us "
            "Original={} avg={:.2f} us max={} us "
            "Gap={} avg={:.2f} us max={} us >50ms={} >100ms={} >200ms={}",
            begin_rendering_count,
            script_runner_count,
            script_avg_us,
            script_runner_max_us,
            original_count,
            original_avg_us,
            original_max_us,
            begin_gap_count,
            begin_gap_avg_us,
            begin_gap_max_us,
            begin_gap_over_50ms,
            begin_gap_over_100ms,
            begin_gap_over_200ms
        );

        begin_rendering_count = 0;

        script_runner_count = 0;
        script_runner_total_us = 0;
        script_runner_max_us = 0;

        original_count = 0;
        original_total_us = 0;
        original_max_us = 0;

        begin_gap_count = 0;
        begin_gap_total_us = 0;
        begin_gap_max_us = 0;
        begin_gap_over_50ms = 0;
        begin_gap_over_100ms = 0;
        begin_gap_over_200ms = 0;

        last_report = now;
    }
}

void Hooks::begin_rendering_hook(void* entry) {
    g_hook->begin_rendering_hook_internal(entry);
}

std::optional<std::string> Hooks::hook_all_application_entries() {
    spdlog::info("[Hooks] Attempting to application entries...");

    auto application = sdk::Application::get();

    if (application == nullptr) {
        return "Failed to get via.Application";
    }

    spdlog::info("[Hooks] Found via.Application at {:x}", (uintptr_t)application);
    
    auto generate_mov_rdx = [](uintptr_t target) {
        std::vector<uint8_t> mov_rdx{ 0x48, 0xBA, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00 };
        *(uintptr_t*)&mov_rdx[2] = target;

        return mov_rdx;
    };

    auto generate_mov_r8 = [](uintptr_t target) {
        std::vector<uint8_t> mov_r8{ 0x49, 0xB8, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00 };
        *(uintptr_t*)&mov_r8[2] = target;

        return mov_r8;
    };

    auto generate_mov_r9 = [](uintptr_t target) {
        std::vector<uint8_t> mov_r9{ 0x49, 0xB9, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00 };
        *(uintptr_t*)&mov_r9[2] = target;

        return mov_r9;
    };

    auto generate_jmp_r9 = []() {
        std::vector<uint8_t> jmp_r8{ 0x41, 0xFF, 0xE1 };
        return jmp_r8;
    };

    // movabs rdx, entry_name_addr
    // movabs r8, entry_name_hash
    // movabs r9, hook_addr
    // jmp r9 (Hooks::global_application_entry_hook)
    // The purpose of this is so we can pass some state to the hook callback
    // So we can know which hook is being called, as a global hook handler
    // gets called for every hook (Hooks::global_application_entry_hook)
    auto generate_hook_func = [&](const char* name, uintptr_t target) {
        auto mov_rdx = generate_mov_rdx((uintptr_t)name);
        auto mov_r8 = generate_mov_r8(utility::hash(name));
        auto mov_r9 = generate_mov_r9(target);
        auto jmp_r9 = generate_jmp_r9();

        // Concats the above vectors into a single vector.
        std::vector<uint8_t> hook{};
        hook.insert(hook.end(), mov_rdx.begin(), mov_rdx.end());
        hook.insert(hook.end(), mov_r8.begin(), mov_r8.end());
        hook.insert(hook.end(), mov_r9.begin(), mov_r9.end());
        hook.insert(hook.end(), jmp_r9.begin(), jmp_r9.end());

        // Allocate some permanent memory for the hook
        // and copy the hook into it. Set the permissions to RWX.
        auto hook_addr = VirtualAlloc(nullptr, hook.size(), MEM_COMMIT | MEM_RESERVE, PAGE_EXECUTE_READWRITE);
        memcpy(hook_addr, hook.data(), hook.size());

        return hook_addr;
    };

    // Seemingly only necessary when using MinHook rather than pointer hooking.
    /*std::unordered_set<void*> bad_funcs{};
    std::unordered_map<void*, sdk::Application::Function*> funcs_to_entries{};

    for (auto i = 0; i < 1024; ++i) {
        auto entry = application->get_function(i);

        if (entry == nullptr || entry->description == nullptr) {
            continue;
        }

        auto func = entry->func;

        if (func == nullptr) {
            continue;
        }

        if (funcs_to_entries.find(func) == funcs_to_entries.end()) {
            funcs_to_entries[func] = entry;
        } else {
            bad_funcs.insert(func);
        }
    }*/

    for (auto i = 0; i < 1024; ++i) {
        auto entry = application->get_function(i);

        if (entry == nullptr || entry->get_description() == nullptr) {
            continue;
        }

        auto func = entry->func;

        if (func == nullptr) {
            continue;
        }

        /*if (bad_funcs.find(func) != bad_funcs.end()) {
            spdlog::warn("Duplicate function: {}", entry->description);
            continue;
        }*/

        spdlog::info("{} {} entry: {:x}", i, entry->get_description(), (uintptr_t)entry);

        auto generated_hook = generate_hook_func((const char*)entry->get_description(), (uintptr_t)&global_application_entry_hook);

        //m_application_entry_hooks[entry->description] = std::make_unique<FunctionHook>(func, generated_hook);
        
        // We are just going to replace the pointer to the function for now
        // Doing a full hook with FunctionHook eats up a lot of initialization time because of
        // the constant thread suspension. 
        m_application_entry_hooks[entry->get_description()] = func;
        entry->func = (void (*)(void*))generated_hook;

        spdlog::info("Hooked {} {:x}->{:x}", entry->get_description(), (uintptr_t)func, (uintptr_t)generated_hook);
    }

    /*for (auto& entry : m_application_entry_hooks) {
        if (!entry.second->create()) {
            return "Failed to hook via::Application::" + std::string{entry.first};
        }
    }*/

    return std::nullopt;
}

std::optional<std::string> Hooks::hook_update_before_lock_scene() {
    // This function is removed (or not reflected) >= TDB74...
    // Additionally, it never existed in RE7 or MHRISE — attempting to hook
    // there returns an error string and aborts the whole Hooks init chain.
    {
        const auto& gi = sdk::GameIdentity::get();
        if (gi.is_re7() || gi.is_mhrise()) {
            return std::nullopt;
        }
    }
    if (sdk::GameIdentity::get().tdb_ver() < 74) {
    // Hook updateBeforeLockScene
    auto update_before_lock_scene = sdk::find_native_method("via.render.EntityRenderer", "updateBeforeLockScene");

    if (update_before_lock_scene == nullptr) {
        return "Unable to find via::render::EntityRenderer::updateBeforeLockScene";
    }

    spdlog::info("updateBeforeLockScene: {:x}", (uintptr_t)update_before_lock_scene);

    m_update_before_lock_scene_hook = std::make_unique<FunctionHook>(update_before_lock_scene, &update_before_lock_scene_hook);

    if (!m_update_before_lock_scene_hook->create()) {
        return "Failed to hook via::render::EntityRenderer::updateBeforeLockScene";
    }
    }

    return std::nullopt;
}

std::optional<std::string> Hooks::hook_lightshaft_draw() {
#if 0
    // Create a fake via.render.LightShaft instance
    // so we can get the draw method and hook it.
    auto lightshaft_t = sdk::find_type_definition("via.render.LightShaft");

    if (lightshaft_t == nullptr) {
        return "Unable to find via::render::LightShaft";
    }

    auto lightshaft = lightshaft_t->create_instance();

    if (lightshaft == nullptr) {
        return "Unable to create via::render::LightShaft instance";
    }

    auto lightshaft_vtable = *(void***)lightshaft;

    if (lightshaft_vtable == nullptr) {
        return "Unable to get via::render::LightShaft vtable";
    }

    auto draw = lightshaft_vtable[(sdk::GameIdentity::get().is_re8() || sdk::GameIdentity::get().is_mhrise()) ? 13 : 10];

    if (draw == nullptr) {
        return "Unable to get via::render::LightShaft::draw";
    }

    spdlog::info("LightShaft::draw: {:x}", (uintptr_t)draw);

    m_lightshaft_draw_hook = std::make_unique<FunctionHook>((uintptr_t)draw, (uintptr_t)&lightshaft_draw_hook);

    if (!m_lightshaft_draw_hook->create()) {
        return "Failed to hook via::render::LightShaft::draw";
    }
#endif

    return std::nullopt;
}

std::optional<std::string> Hooks::hook_view_get_size() {
    // We're going to hook via.SceneView.get_Size so we can
    // spoof the render target size to the HMD's resolution.
    auto get_size_func = sdk::find_native_method("via.SceneView", "get_Size");

    if (get_size_func == nullptr) {
        return "Hook init failed: via.SceneView.get_Size function not found.";
    }

    spdlog::info("via.SceneView.get_Size: {:x}", (uintptr_t)get_size_func);

    // Pattern scan for the native function call
    //auto ref = utility::scan((uintptr_t)get_size_func, 0x100, "49 8B C8 E8");
    auto ref = utility::find_pattern_in_path((uint8_t*)get_size_func, 1000, false, "49 8B C8 E8");

    if (!ref) {
        ref = utility::find_pattern_in_path((uint8_t*)get_size_func, 1000, false, "48 8B CB E8");
    }

    
    if (!ref) {
        ref = utility::find_pattern_in_path((uint8_t*)get_size_func, 1000, false, "48 89 F2 E8"); // >= TDB74 (MHWILDS)
    }

    if (!ref) {
        ref = utility::find_pattern_in_path((uint8_t*)get_size_func, 1000, false, "48 8B CF E8"); // Pragmata
    }

    if (!ref) {
        return "Hook init failed: via.SceneView.get_Size native function not found. Pattern scan failed.";
    }

    auto native_func = utility::calculate_absolute(ref->addr + 4);

    // Hook the native function
    m_view_get_size_hook = std::make_unique<FunctionHook>(native_func, view_get_size_hook);

    if (!m_view_get_size_hook->create()) {
        return "Hook init failed: via.SceneView.get_Size native function hook failed.";
    }

    return std::nullopt;
}

std::optional<std::string> Hooks::hook_camera_get_projection_matrix() {
    // We're going to hook via.Camera.get_ProjectionMatrix so we can
    // override the camera's Projection matrix with the HMD's Projection matrix (per-eye)
    auto func = sdk::find_native_method("via.Camera", "get_ProjectionMatrix");

    if (func == nullptr) {
        return "Hook init failed: via.Camera.get_ProjectionMatrix function not found.";
    }

    spdlog::info("via.Camera.get_ProjectionMatrix: {:x}", (uintptr_t)func);
    
    // Pattern scan for the native function call
    auto ref = utility::find_pattern_in_path((uint8_t*)func, 1000, false, "49 8B C8 E8");

    if (!ref) {
        ref = utility::find_pattern_in_path((uint8_t*)func, 1000, false, "48 8B CB E8");
    }
    
    if (!ref) {
        ref = utility::find_pattern_in_path((uint8_t*)func, 1000, false, "48 89 F2 E8"); // >= TDB74?
    }

    if (!ref) {
        return "Hook init failed: via.Camera.get_ProjectionMatrix native function not found. Pattern scan failed.";
    }

    auto native_func = utility::calculate_absolute(ref->addr + 4);

    // Hook the native function
    m_camera_get_projection_matrix_hook = std::make_unique<FunctionHook>(native_func, camera_get_projection_matrix_hook);

    if (!m_camera_get_projection_matrix_hook->create()) {
        return "Hook init failed: via.Camera.get_ProjectionMatrix native function hook failed.";
    }

    spdlog::info("Hooked via.Camera.get_ProjectionMatrix");

    return std::nullopt;
}

std::optional<std::string> Hooks::hook_camera_get_view_matrix() {
    auto func = sdk::find_native_method("via.Camera", "get_ViewMatrix");

    if (func == nullptr) {
        return "Hook init failed: via.Camera.get_ViewMatrix function not found.";
    }

    spdlog::info("via.Camera.get_ViewMatrix: {:x}", (uintptr_t)func);

    // Pattern scan for the native function call
    auto ref = utility::find_pattern_in_path((uint8_t*)func, 1000, false, "49 8B C8 E8");

    if (!ref) {
        ref = utility::find_pattern_in_path((uint8_t*)func, 1000, false, "48 8B CB E8");
    }

    if (!ref) {
        ref = utility::find_pattern_in_path((uint8_t*)func, 1000, false, "48 89 F2 E8"); // >= TDB74?
    }

    if (!ref) {
        return "Hook init failed: via.Camera.get_ViewMatrix native function not found. Pattern scan failed.";
    }

    auto native_func = utility::calculate_absolute(ref->addr + 4);

    // Hook the native function
    m_camera_get_view_matrix_hook = std::make_unique<FunctionHook>(native_func, camera_get_view_matrix_hook);

    if (!m_camera_get_view_matrix_hook->create()) {
        return "Hook init failed: via.Camera.get_ViewMatrix native function hook failed.";
    }

    return std::nullopt;
}

std::optional<std::string> Hooks::hook_render_layer(Hooks::RenderLayerHook<sdk::renderer::RenderLayer>& hook) {
    auto t = sdk::find_type_definition(hook.name);

    if (t == nullptr) {
        return std::string{"Hooks init failed: "} + hook.name + " type not found.";
    }

    void* fake_obj = t->create_instance();

    if (fake_obj == nullptr) { 
        return std::string{"Hooks init failed: "} + "Failed to create fake " + hook.name + " instance.";
    }

    auto obj_vtable = *(uintptr_t**)fake_obj;

    if (obj_vtable == nullptr) {
        return std::string{"Hooks init failed: "} + hook.name + " vtable not found.";
    }

    spdlog::info("{:s} vtable: {:x}", hook.name, (uintptr_t)obj_vtable - g_framework->get_module());

    auto draw_native = obj_vtable[sdk::renderer::RenderLayer::get_draw_vtable_index()];

    if (draw_native == 0) {
        return std::string{"Hooks init failed: "} + hook.name + " draw native not found.";
    }

    spdlog::info("{:s}.Draw: {:x}", hook.name, (uintptr_t)draw_native);

    // Set the first byte to the ret instruction
    //m_overlay_draw_patch = Patch::create(draw_native, { 0xC3 });

    if (!utility::is_stub_code((uint8_t*)draw_native)) {
        if (!hook.hook_draw(draw_native)) {
            return std::string{"Hooks init failed: "} + hook.name + " draw native function hook failed.";
        }
    } else {
        spdlog::info("Skipping draw hook for {:s}, stub code detected", hook.name);
    }

    auto update_native = obj_vtable[sdk::renderer::RenderLayer::get_update_vtable_index()];

    if (update_native == 0) {
        return std::string{"Hooks init failed: "} + hook.name + " update native not found.";
    }

    spdlog::info("{:s}.Update: {:x}", hook.name, (uintptr_t)update_native);

    if (!utility::is_stub_code((uint8_t*)update_native)) {
        if (!hook.hook_update(update_native)) {
            return std::string{"Hooks init failed: "} + hook.name + " update native function hook failed.";
        }
    } else {
        spdlog::info("Skipping update hook for {:s}, stub code detected", hook.name);
    }

    return std::nullopt;
}

void* Hooks::update_transform_hook_internal(RETransform* t, uint8_t a2, uint32_t a3) {
    if (!g_framework->is_ready()) {
        return m_update_transform_hook->get_original<decltype(update_transform_hook)>()(t, a2, a3);
    }

    auto& mods = g_framework->get_mods()->get_mods();

    for (auto& mod : mods) {
        mod->on_pre_update_transform(t);
    }

    auto ret = m_update_transform_hook->get_original<decltype(update_transform_hook)>()(t, a2, a3);

    for (auto& mod : mods) {
        mod->on_update_transform(t);
    }

    return ret;
}

void* Hooks::update_transform_hook(RETransform* t, uint8_t a2, uint32_t a3) {
    return g_hook->update_transform_hook_internal(t, a2, a3);
}

void* Hooks::update_camera_controller_hook_internal(void* a1, RopewayPlayerCameraController* camera_controller) {
    if (!g_framework->is_ready()) {
        return m_update_camera_controller_hook->get_original<decltype(update_camera_controller_hook)>()(a1, camera_controller);
    }

    auto& mods = g_framework->get_mods()->get_mods();

    for (auto& mod : mods) {
        mod->on_pre_update_camera_controller(camera_controller);
    }

    auto ret = m_update_camera_controller_hook->get_original<decltype(update_camera_controller_hook)>()(a1, camera_controller);

    for (auto& mod : mods) {
        mod->on_update_camera_controller(camera_controller);
    }

    return ret;
}

void* Hooks::update_camera_controller_hook(void* a1, RopewayPlayerCameraController* camera_controller) {
    return g_hook->update_camera_controller_hook_internal(a1, camera_controller);
}

void* Hooks::update_camera_controller2_hook_internal(void* a1, RopewayPlayerCameraController* camera_controller) {
    if (!g_framework->is_ready()) {
        return m_update_camera_controller2_hook->get_original<decltype(update_camera_controller2_hook)>()(a1, camera_controller);
    }

    auto& mods = g_framework->get_mods()->get_mods();

    for (auto& mod : mods) {
        mod->on_pre_update_camera_controller2(camera_controller);
    }

    auto ret = m_update_camera_controller2_hook->get_original<decltype(update_camera_controller2_hook)>()(a1, camera_controller);

    for (auto& mod : mods) {
        mod->on_update_camera_controller2(camera_controller);
    }

    return ret;
}

void* Hooks::update_camera_controller2_hook(void* a1, RopewayPlayerCameraController* camera_controller) {
    return g_hook->update_camera_controller2_hook_internal(a1, camera_controller);
}

void* Hooks::gui_draw_hook_internal(REComponent* gui_element, void* primitive_context) {
    auto original_func = m_gui_draw_hook->get_original<decltype(gui_draw_hook)>();

    if (!g_framework->is_ready()) {
        return original_func(gui_element, primitive_context);
    }

    auto& mods = g_framework->get_mods()->get_mods();

    bool any_false = false;

    for (auto& mod : mods) {
        if (!mod->on_pre_gui_draw_element(gui_element, primitive_context)) {
            any_false = true;
        }
    }

    void* ret = nullptr;

    if (!any_false) {
        ret = original_func(gui_element, primitive_context);
    }

    for (auto& mod : mods) {
        mod->on_gui_draw_element(gui_element, primitive_context);
    }

    return ret;
}

void* Hooks::gui_draw_hook(REComponent* gui_element, void* primitive_context) {
    return g_hook->gui_draw_hook_internal(gui_element, primitive_context);
}

void Hooks::update_before_lock_scene_hook_internal(void* ctx) {
    auto original = m_update_before_lock_scene_hook->get_original<decltype(update_before_lock_scene_hook)>();

    if (!g_framework->is_ready()) {
        return original(ctx);
    }

    auto& mods = g_framework->get_mods()->get_mods();

    for (auto& mod : mods) {
        mod->on_pre_update_before_lock_scene(ctx);
    }

    original(ctx);

    for (auto& mod : mods) {
        mod->on_update_before_lock_scene(ctx);
    }
}

void Hooks::update_before_lock_scene_hook(void* ctx) {
    g_hook->update_before_lock_scene_hook_internal(ctx);
}

void Hooks::lightshaft_draw_hook_internal(void* shaft, void* render_context) {
    auto original = m_lightshaft_draw_hook->get_original<decltype(lightshaft_draw_hook)>();

    if (!g_framework->is_ready()) {
        return original(shaft, render_context);
    }

    auto& mods = g_framework->get_mods()->get_mods();

    for (auto& mod : mods) {
        mod->on_pre_lightshaft_draw(shaft, render_context);
    }

    original(shaft, render_context);

    for (auto& mod : mods) {
        mod->on_lightshaft_draw(shaft, render_context);
    }
}

void Hooks::lightshaft_draw_hook(void* shaft, void* render_context) {
    g_hook->lightshaft_draw_hook_internal(shaft, render_context);
}

void Hooks::global_application_entry_hook_internal(void* entry, const char* name, size_t hash) {
    //spdlog::info("{}", name);

    auto original = m_application_entry_hooks[name];

    if (!g_framework->is_game_data_initialized()) {
        return original(entry);
    }

    const auto should_allow_ignore = sdk::VM::s_tdb_version >= 73 ?
                                     (hash != 0x76b8100bec7c12c3 && hash != 0x9f63c0fc4eea6626) :
                                     true;

    if (should_allow_ignore) {
        std::shared_lock _{m_application_entry_data_mutex};

        if (m_ignored_application_entries.contains(hash)) {
            return;
        }
    }

    if (hash == "BeginRendering"_fnv) {
    if (sdk::GameIdentity::get().tdb_ver() >= 73) {
    if (auto primitive_system = sdk::gui::renderer::PrimitiveSystem::get(); primitive_system != nullptr) {
        auto primitive_buffer = primitive_system->get_primitive_buffer();
        
        if (primitive_buffer != nullptr) {
            if (primitive_buffer->scratch.used >= primitive_buffer->scratch.size) {
                spdlog::info("[GUI] Resizing scratch buffer from {} to {}", primitive_buffer->scratch.size, primitive_buffer->scratch.size * 2);
                primitive_buffer->scratch.resize(primitive_buffer->scratch.size * 2);
            }
        }
    }
    }
    }

    if (m_profiling_enabled) {
        Hooks::ApplicationEntryData profiler_entry{};
        
        auto now = std::chrono::high_resolution_clock::now();
        auto& mods = g_framework->get_mods()->get_mods();

        if (hash == "BeginRendering"_fnv) {
            g_framework->run_imgui_frame(false);
        }

        for (auto& mod : mods) {
            mod->on_pre_application_entry(entry, name, hash);
        }

        profiler_entry.reframework_pre_time = std::chrono::high_resolution_clock::now() - now;

        now = std::chrono::high_resolution_clock::now();
        
        original(entry);

        profiler_entry.callback_time = std::chrono::high_resolution_clock::now() - now;

        now = std::chrono::high_resolution_clock::now();

        for (auto& mod : mods) {
            mod->on_application_entry(entry, name, hash);
        }

        profiler_entry.reframework_post_time = std::chrono::high_resolution_clock::now() - now;
        
        std::scoped_lock _{m_profiler_mutex};
        m_application_entry_times[name] = profiler_entry;
    } else {
        if (hash == "BeginRendering"_fnv) {
            g_framework->run_imgui_frame(false);
        }

        auto& mods = g_framework->get_mods()->get_mods();

        for (auto& mod : mods) {
            mod->on_pre_application_entry(entry, name, hash);
        }
        
        original(entry);

        for (auto& mod : mods) {
            mod->on_application_entry(entry, name, hash);
        }
    }
}

void Hooks::global_application_entry_hook(void* entry, const char* name, size_t hash) {
    g_hook->global_application_entry_hook_internal(entry, name, hash);
}

float* Hooks::view_get_size_hook_internal(REManagedObject* scene_view, float* result) {
    if (!g_framework->is_ready()) {
        return m_view_get_size_hook->get_original<decltype(view_get_size_hook)>()(scene_view, result);
    }

    const auto& mods = g_framework->get_mods()->get_mods();

    for (auto& mod : mods) {
        mod->on_pre_view_get_size(scene_view, result);
    }

    auto original = m_view_get_size_hook->get_original<decltype(view_get_size_hook)>();

    auto ret = original(scene_view, result);

    for (auto& mod : mods) {
        mod->on_view_get_size(scene_view, result);
    }

    return ret;
}

float* Hooks::view_get_size_hook(REManagedObject* scene_view, float* result) {
    return g_hook->view_get_size_hook_internal(scene_view, result);
}

Matrix4x4f* Hooks::camera_get_projection_matrix_hook_internal(REManagedObject* camera, Matrix4x4f* result) {
    if (!g_framework->is_ready()) {
        return m_camera_get_projection_matrix_hook->get_original<decltype(camera_get_projection_matrix_hook)>()(camera, result);
    }

    const auto& mods = g_framework->get_mods()->get_mods();

    for (auto& mod : mods) {
        mod->on_pre_camera_get_projection_matrix(camera, result);
    }

    auto original = m_camera_get_projection_matrix_hook->get_original<decltype(camera_get_projection_matrix_hook)>();

    auto ret = original(camera, result);

    for (auto& mod : mods) {
        mod->on_camera_get_projection_matrix(camera, result);
    }

    return ret;
}

Matrix4x4f* Hooks::camera_get_projection_matrix_hook(REManagedObject* camera, Matrix4x4f* result) {
    return g_hook->camera_get_projection_matrix_hook_internal(camera, result);
}

Matrix4x4f* Hooks::camera_get_view_matrix_hook_internal(REManagedObject* camera, Matrix4x4f* result) {
    if (!g_framework->is_ready()) {
        return m_camera_get_view_matrix_hook->get_original<decltype(camera_get_view_matrix_hook)>()(camera, result);
    }

    const auto& mods = g_framework->get_mods()->get_mods();

    for (auto& mod : mods) {
        mod->on_pre_camera_get_view_matrix(camera, result);
    }

    auto original = m_camera_get_view_matrix_hook->get_original<decltype(camera_get_view_matrix_hook)>();

    auto ret = original(camera, result);

    for (auto& mod : mods) {
        mod->on_camera_get_view_matrix(camera, result);
    }

    return ret;
}

Matrix4x4f* Hooks::camera_get_view_matrix_hook(REManagedObject* camera, Matrix4x4f* result) {
    return g_hook->camera_get_view_matrix_hook_internal(camera, result);
}