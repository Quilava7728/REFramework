#pragma once

#include <windows.h>

namespace mhs3::micro_runtime {

class MicroPointerHook {
public:
    bool install(void** slot, void* replacement);
    void uninstall();

    template <typename T>
    T original() const {
        return reinterpret_cast<T>(m_original);
    }

private:
    void** m_slot{};
    void* m_original{};
};

}
