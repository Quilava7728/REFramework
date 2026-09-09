#include "MicroVtableHook.hpp"

// Tiny Chungus 🐇
// One swapchain. One cloned vtable. No cathedral.

namespace mhs3::micro_runtime {

bool MicroVtableHook::install(void* instance) {
    if (instance == nullptr || m_instance != nullptr) {
        return false;
    }

    auto*** instance_vtable =
        reinterpret_cast<void***>(instance);

    if (instance_vtable == nullptr || *instance_vtable == nullptr) {
        return false;
    }

    m_instance = instance;
    m_original_vtable = *instance_vtable;

    for (std::size_t i = 0; i < kSwapChain3VtableSize; ++i) {
        m_cloned_vtable[i] = m_original_vtable[i];
    }

    *instance_vtable = m_cloned_vtable.data();

    return true;
}

bool MicroVtableHook::hook_method(std::size_t index, void* replacement) {
    if (m_instance == nullptr ||
        m_original_vtable == nullptr ||
        replacement == nullptr ||
        index >= kSwapChain3VtableSize) {
        return false;
    }

    m_cloned_vtable[index] = replacement;
    return true;
}

void MicroVtableHook::uninstall() {
    if (m_instance == nullptr || m_original_vtable == nullptr) {
        return;
    }

    auto*** instance_vtable =
        reinterpret_cast<void***>(m_instance);

    if (instance_vtable != nullptr &&
        *instance_vtable == m_cloned_vtable.data()) {
        *instance_vtable = m_original_vtable;
    }

    m_instance = nullptr;
    m_original_vtable = nullptr;
    m_cloned_vtable.fill(nullptr);
}

}
