#pragma once

#include <array>
#include <cstddef>

namespace mhs3::micro_runtime {

class MicroVtableHook {
public:
    static constexpr std::size_t kSwapChain3VtableSize = 40;

    bool install(void* instance);
    bool hook_method(std::size_t index, void* replacement);
    void uninstall();

    template <typename T>
    T original(std::size_t index) const {
        if (m_original_vtable == nullptr || index >= kSwapChain3VtableSize) {
            return nullptr;
        }

        return reinterpret_cast<T>(m_original_vtable[index]);
    }

    bool installed() const {
        return m_instance != nullptr;
    }

private:
    void* m_instance{};
    void** m_original_vtable{};
    std::array<void*, kSwapChain3VtableSize> m_cloned_vtable{};
};

}
