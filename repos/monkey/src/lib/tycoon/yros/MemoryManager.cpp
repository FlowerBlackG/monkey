// SPDX-License-Identifier: MulanPSL-2.0

// Forked from YurongOS: github.com/FlowerBlackG/YurongOS

/*

    内存管理器。

    创建于 2023年1月5日

*/

#include "./memory.h"
#include "./MemoryManager.h"
#include "./ArenaMemoryManager.h"

#include <adl/string.h>
#include <adl/config.h>

#include <adl/sys/types.h>

using namespace adl;


namespace yros {
namespace memory {

namespace MemoryManager {

    adl::uint64_t systemTotalMemory;
    adl::uint64_t systemManagedMemory;
    adl::uint64_t systemManagedMildMemory;
}

void MemoryManager::init(
    void* firstPageOfLink,
    void* addr,
    adl::size_t size
) {

    FreeMemoryManager::init(firstPageOfLink);

    systemTotalMemory = 0;
    systemManagedMemory = 0;
    systemManagedMildMemory = 0;

    FreeMemoryManager::free((adl::uint64_t) addr, size, false, true);
    ArenaMemoryManager::init();

}


uint64_t MemoryManager::allocPage(uint64_t count) {
    return FreeMemoryManager::alloc(MemoryManager::PAGE_SIZE * count);
}

uint64_t MemoryManager::allocWhitePage(uint64_t count, uint8_t fill) {
    uintptr_t resAddr = FreeMemoryManager::alloc(MemoryManager::PAGE_SIZE * count);

    if (resAddr) {
        adl::memset((void*) (resAddr), fill, PAGE_SIZE);
    }

    return resAddr;
}

void MemoryManager::freePage(uint64_t addr, uint64_t count) {
    FreeMemoryManager::free(addr, MemoryManager::PAGE_SIZE * count);
}

}
}
