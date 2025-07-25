// SPDX-License-Identifier: MulanPSL-2.0

// Forked from YurongOS: github.com/FlowerBlackG/YurongOS

/*

    内核堆内存管理器。

    创建于 2023年2月2日 江西省上饶市玉山县

*/

#include "./MemoryManager.h"
#include "./ArenaMemoryManager.h"
#include "./KernelMemoryAllocator.h"
#include <adl/string.h>

#include <base/log.h>

using namespace adl;

namespace yros {
namespace memory {

/**
 * 内核内存分配器。实现动态内存管理。
 * 管理器运行在虚地址空间的物理内存映射区。
 * 
 * 管理器支持小内存管理，可以无忧无虑地以字节为单位申请和释放内存。
 * 
 * 当申请的内存较小时，分配器内部借助 arena 技术完成内存碎片管理。
 * 
 * 当申请的内存较大时，分配器会在内存块开头写入一个 arena 结构。
 * 此时，返回的内存地址是紧邻在 arena 结构后的。
 * 
 * 如果直接使用本管理器的页分配功能，将不启用 arena 技术。
 * 
 * 使用 malloc 申请的内存，使用 free 释放。
 * 使用 allocPage 申请的内存，使用 freePage 释放。
 */
namespace KernelMemoryAllocator {

void* malloc(size_t size) {

    if (!size) {
        return nullptr;
    }
    
    if (size > ArenaMemoryManager::MAXIMUM_LINKABLE_BLOCK_SIZE) {
        uint64_t requiredSize = size + sizeof(ArenaStage);

        // 按照 4KB 向上取整。计算需要的页数。
        requiredSize += (MemoryManager::PAGE_SIZE - 1);
        uint64_t requiredPages = requiredSize /= MemoryManager::PAGE_SIZE;
        
        void* page = allocPage(requiredPages);
        if (page == nullptr) {
            Genode::error("Failed to allocate memory.");
            return nullptr;
        }

        ArenaStage* stage = (ArenaStage*) page;
        stage->count = requiredPages;
        stage->large = 1;
        stage->descriptor = nullptr;

        return stage + 1;
    }

    ArenaDescriptor* descriptor;

    for (int idx = 0; idx < ArenaMemoryManager::DESCRIPTOR_COUNT; idx++) {
        descriptor = &ArenaMemoryManager::arenaDescriptors[idx];
        if (descriptor->blockSize >= size) {
            break;
        }
    }

    ArenaBlockNode* list = descriptor->firstFreeBlock;

    if (list == nullptr) {
        ArenaStage* newStage = (ArenaStage*) allocPage();

        if (!newStage) {
            Genode::error("Failed to allocate memory (stage).");
            return nullptr;
        }

        newStage->descriptor = descriptor;
        newStage->large = 0;
        newStage->count = descriptor->blocksPerPage;

        ArenaBlockNode* firstBlock = newStage->getBlock(0);
        descriptor->firstFreeBlock = firstBlock;
        list = firstBlock;
        firstBlock->prev = nullptr;

        for (uint32_t idx = 1; idx < newStage->count; idx++) {
            ArenaBlockNode* block = newStage->getBlock(idx);
            block->prev = newStage->getBlock(idx - 1);
            newStage->getBlock(idx - 1)->next = block;
        }

        newStage->getBlock(newStage->count - 1)->next = nullptr;


        newStage->count --;  // We will detach one block to malloc caller.
    }

    if (list->next) {
        list->next->prev = nullptr;
    }

    descriptor->firstFreeBlock = list->next;

    return list;
}

void free(void* addr) {

    if (!addr) {
        return;
    }

    ArenaStage* stage = ArenaMemoryManager::getStage(addr);
    if (stage->large) {
        uint64_t realAddr = uint64_t(addr) - sizeof(ArenaStage);
        freePage((void*) realAddr, stage->count);
        return;
    }

    stage->count++;

    ArenaDescriptor* descriptor = stage->descriptor;
    ArenaBlockNode* block = (ArenaBlockNode*) addr;
    
    if (descriptor->firstFreeBlock) {

        block->next = descriptor->firstFreeBlock;
        descriptor->firstFreeBlock->prev = block;
        descriptor->firstFreeBlock = block;

    } else {

        descriptor->firstFreeBlock = block;
        block->next = nullptr;

    }

    block->prev = nullptr;


    // See whether the arena page is all recycled.
    if (stage->count == stage->descriptor->blocksPerPage) {
        // So all blocks inside this page is recycled. Maybe we can free the whole page?
        // TODO
        // Maybe: unlinkAndFreeArenaPage(stage);
    }


}


void* allocWhitePage(uint64_t count, uint8_t fill) {
    uint64_t addr = MemoryManager::allocWhitePage(count, fill);

    if (addr) {

        return (void*) addr;

    } else {
        return nullptr;
    }
}

void* allocPage(uint64_t count) {
    uint64_t addr = MemoryManager::allocPage(count);
    if (addr) {
    
        return (void*) addr;

    } else {
    
        return nullptr;
    
    }
}


/**
 * @param stage Must have all blocks recycled. Or, this method would cause UB. 
 *              Also, stage must NOT holds large page.
 */
static void unlinkAndFreeArenaPage(ArenaStage* stage) {
    // assert stage->count == stage->descriptor->blocksPerPage;

    auto descriptor = stage->descriptor;
    
    for (uint32_t idx = 0; idx < descriptor->blocksPerPage; idx ++) {
        auto block = stage->getBlock(idx);

        if (!block->prev) {  // block is descriptor's first block.
            descriptor->firstFreeBlock = block->next;
        }
        else {
            block->prev->next = block->next;
        }

        if (block->next)
            block->next->prev = block->prev;
    }

    freePage(stage, 1);
}  // TODO: Haven't tested.


void freePage(void* addr, uint64_t count) {
    uint64_t realAddr = (uint64_t) addr;
    MemoryManager::freePage(realAddr, count);
}


}  // namespace KernelMemoryAllocator
}  // namespace memory
}  // namespace yros
