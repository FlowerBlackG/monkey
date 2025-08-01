// SPDX-License-Identifier: MulanPSL-2.0

/*
 * 数据类型定义。
 * 创建于 2022年7月2日。
 */

// forked from yros stdlib.
// github.com/FlowerBlackG/YurongOS

// modified for Amkos

#pragma once

#include "../stdint.h"

/** 文件结尾。 */
#ifndef EOF
    #define EOF -1
#endif

/** 空指针。 */
#ifndef NULL
    #define NULL 0
#endif

#ifdef ADL_DEFINE_GCC_SHORT_MACROS
/** 结构体紧凑。 */
#ifndef __packed
    #define __packed __attribute__((packed))
#endif

#ifndef __no_return
    #define __no_return __attribute__((__noreturn__))
#endif

/** 令函数不保存 rsp 和 rbp。 */
#ifndef __omit_frame_pointer
    #define __omit_frame_pointer __attribute__((optimize("omit-frame-pointer")))
#endif

/** 强制内联。 */
#ifndef __force_inline
    #define __force_inline __attribute__((always_inline)) inline
#endif

/** 内联汇编。 */
#ifndef __asm
    #define __asm __asm__ __volatile__
#endif

/** 指定代码存放的区域。 */
#ifndef __section
    #define __section(name) __attribute__((section(name)))
#endif
#endif
namespace adl {

typedef unsigned long size_t;

}
