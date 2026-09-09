#include "MicroPointerHook.hpp"

// Tiny Chungus 🐇
// He replaces one pointer and asks no questions.

namespace mhs3::micro_runtime {

bool MicroPointerHook::install(void** slot, void* replacement) {
    if (slot == nullptr || replacement == nullptr || m_slot != nullptr) {
        return false;
    }

    DWORD old_protect{};

    if (!VirtualProtect(slot, sizeof(void*), PAGE_EXECUTE_READWRITE, &old_protect)) {
        return false;
    }

    m_slot = slot;
    m_original = *slot;
    *slot = replacement;

    DWORD restore_protect{};
    VirtualProtect(slot, sizeof(void*), old_protect, &restore_protect);
    FlushInstructionCache(GetCurrentProcess(), slot, sizeof(void*));

    return true;
}

void MicroPointerHook::uninstall() {
    if (m_slot == nullptr || m_original == nullptr) {
        return;
    }

    DWORD old_protect{};

    if (VirtualProtect(m_slot, sizeof(void*), PAGE_EXECUTE_READWRITE, &old_protect)) {
        *m_slot = m_original;

        DWORD restore_protect{};
        VirtualProtect(m_slot, sizeof(void*), old_protect, &restore_protect);
        FlushInstructionCache(GetCurrentProcess(), m_slot, sizeof(void*));
    }

    m_slot = nullptr;
    m_original = nullptr;
}

}
