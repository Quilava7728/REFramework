#define BAREBONES 1
#include <spdlog/spdlog.h>

#include <sdk/GameIdentity.hpp>
#include "mods/BackBufferRenderer.hpp"
#include "mods/APIProxy.hpp"
#include "mods/Camera.hpp"
#include "mods/Graphics.hpp"
#include "mods/DeveloperTools.hpp"
#include "mods/FirstPerson.hpp"
#include "mods/FreeCam.hpp"
#include "mods/Hooks.hpp"
#include "mods/IntegrityCheckBypass.hpp"
#include "mods/ManualFlashlight.hpp"
#include "mods/PluginLoader.hpp"
#include "mods/REFrameworkConfig.hpp"
#include "mods/MethodDatabase.hpp"
#include "mods/Scene.hpp"
#include "mods/ScriptRunner.hpp"
#include "mods/VR.hpp"
#include "mods/LooseFileLoader.hpp"
#include "mods/FaultyFileDetector.hpp"
#include "mods/vr/games/RE8VR.hpp"

#include "Mods.hpp"

Mods::Mods() {
    // MHS3 Runtime v0.1:
    // Keep only the minimum known-survivable runtime components.

    m_mods.emplace_back(REFrameworkConfig::get());

    // Keep anti-tamper handling intact for v0.1.
    // We will split bootstrap/runtime behavior in v0.2.
    if (sdk::GameIdentity::get().is_reengine_at()) {
        m_mods.emplace_back(IntegrityCheckBypass::get_shared_instance());
    }

    // Reduced MHS3 BeginRendering hook used as the engine-thread heartbeat.
    m_mods.emplace_back(Hooks::get());

    // Script/plugin runtime.
    m_mods.emplace_back(APIProxy::get());
    m_mods.emplace_back(PluginLoader::get());
    m_mods.emplace_back(ScriptRunner::get());
}

std::optional<std::string> Mods::on_initialize() const {
    for (auto& mod : m_mods) {
        spdlog::info("{:s}::on_initialize()", mod->get_name().data());

        if (auto e = mod->on_initialize(); e != std::nullopt) {
            spdlog::info("{:s}::on_initialize() has failed: {:s}", mod->get_name().data(), *e);
            return e;
        }
    }

    utility::Config cfg{ (REFramework::get_persistent_dir() / REFrameworkConfig::REFRAMEWORK_CONFIG_NAME).string() };

    for (auto& mod : m_mods) {
        spdlog::info("{:s}::on_config_load()", mod->get_name().data());
        mod->on_config_load(cfg);
    }

    return std::nullopt;
}


std::optional<std::string> Mods::on_initialize_d3d_thread() const {
    auto do_not_hook_d3d = g_framework->acquire_do_not_hook_d3d();

    utility::Config cfg{ (REFramework::get_persistent_dir() / REFrameworkConfig::REFRAMEWORK_CONFIG_NAME).string() };

    // once here to at least setup the values
    for (auto& mod : m_mods) {
        spdlog::info("{:s}::on_config_load()", mod->get_name().data());
        mod->on_config_load(cfg);
    }

    for (auto& mod : m_mods) {
        spdlog::info("{:s}::on_initialize_d3d_thread()", mod->get_name().data());

        if (auto e = mod->on_initialize_d3d_thread(); e != std::nullopt) {
            spdlog::info("{:s}::on_initialize_d3d_thread() has failed: {:s}", mod->get_name().data(), *e);
            return e;
        }
    }

    for (auto& mod : m_mods) {
        spdlog::info("{:s}::on_config_load()", mod->get_name().data());
        mod->on_config_load(cfg);
    }

    return std::nullopt;
}

void Mods::on_pre_imgui_frame() const {
    for (auto& mod : m_mods) {
        mod->on_pre_imgui_frame();
    }
}

void Mods::on_frame() const {
    for (auto& mod : m_mods) {
        mod->on_frame();
    }
}

void Mods::on_present() const {
    for (auto& mod : m_mods) {
        mod->on_present();
    }
}

void Mods::on_post_frame() const {
    for (auto& mod : m_mods) {
        mod->on_post_frame();
    }
}

void Mods::on_draw_ui() const {
    for (auto& mod : m_mods) {
        mod->on_draw_ui();
    }
}

void Mods::on_device_reset() const {
    for (auto& mod : m_mods) {
        mod->on_device_reset();
    }
}
