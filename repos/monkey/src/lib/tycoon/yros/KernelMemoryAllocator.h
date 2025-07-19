// SPDX-License-Identifier: MulanPSL-2.0

// Forked from YurongOS: github.com/FlowerBlackG/YurongOS

/*

    内核堆内存管理器。

    创建于 2023年2月2日 江西省上饶市玉山县

*/


#pragma once
#define ADL_DEFINE_GCC_SHORT_MACROS
#include <adl/sys/types.h>

namespace yros {
namespace memory {

/**
 * 内核内存分配器。
 * 负责给内核提供内存分配与释放服务。
 */
namespace KernelMemoryAllocator {

    void* malloc(adl::size_t size);
    void free(void* addr);

    /**
     * 分配count个连续页。页内可能有残留的数据。
     *  
     * @return 分配失败时，返回 nullptr。
     *         返回的地址是线性地址，0xFFFFCxxxxxxxx000。
     */
    void* allocPage(adl::uint64_t count = 1);

    /**
     * 分配 count 个连续的页，并用 fill 填充页内数据。 
     */
    void* allocWhitePage(adl::uint64_t count = 1, adl::uint8_t fill = 0);

    /**
     * 从 addr 开始，释放 count 个连续的页。
     * 
     * @param addr 线性地址。
     */
    void freePage(void* addr, adl::uint64_t count = 1);

};
}
}