// SPDX-License-Identifier: MulanPSL-2.0

// Forked from YurongOS: github.com/FlowerBlackG/YurongOS

/*

    内存管理器。

    创建于 2023年1月5日

*/

#pragma once

#define ADL_DEFINE_GCC_SHORT_MACROS
#include <adl/sys/types.h>
#include "./FreeMemoryManager.h"
namespace yros {
namespace memory {
/**
 * 内存描述符结构。在启动阶段，从 bios 读取。
 */
struct Ards {
    adl::uint64_t base;
    adl::uint64_t size;

    /**
     * 
     * 1：可用。
     * 其他：不可用。
     */
    adl::uint32_t type;
    adl::uint32_t padding;
} __packed;

/**
 * 内存管理器。
 */
namespace MemoryManager {

    const int PAGE_SIZE = 4096;


    /**
     * @param firstPageOfLink First page of the page-link. It should point to a valid 8KB memory,
     *                        contains two page-link page.
     */
    void init(
        void* firstPageOfLink,
        void* addr,
        adl::size_t size
    );


    adl::uint64_t allocPage(adl::uint64_t count = 1);
    adl::uint64_t allocWhitePage(adl::uint64_t count = 1, adl::uint8_t fill = 0);
    void freePage(adl::uint64_t addr, adl::uint64_t count = 1);



    enum class WalkPageTablesCommand {
        SKIP_THIS_ENTRY,
        BACK_TO_UPPER,
        WALK_INTO
    };


    /**
     * 系统总内存。
     * 含所有内存，包括不让使用的。
     */
    extern adl::uint64_t systemTotalMemory;

    /**
     * 总共管控内存。
     * 所有可写内存，含crt显存映射。
     */
    extern adl::uint64_t systemManagedMemory;

    /**
     * 所有可以由系统自由控制的内存。
     * 不包含crt显存映射，因为它不能乱写。
     * 不含 kernel 二进制程序区，因为不能乱写。
     */
    extern adl::uint64_t systemManagedMildMemory;

};

}
}