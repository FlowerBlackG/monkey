/*
 * Tycoon Page
 *
 * Created on 2025.2.21 at Wujing, Minhang
 *
 * gongty
 */


#pragma once

#ifndef MONKEY_TYCOON_INCLUDE_INTERNALS
    #error "Do not include this file directly. Use #include <monkey/tycoon/Tycoon.h> instead."
#endif

#include <adl/sys/types.h>

#include <base/env.h>

namespace monkey::tycoon {


/**
 * A page have many possible states:
 *
 *   1. Allocated, but not mapped:
 *
 *       mapped present writable buf dirty
 *         no     no       no    no   no
 *
 *
 *   2. Accessed. It might be "write":
 *
 *       mapped present writable buf dirty
 *         yes    yes     yes    yes  yes
 *
 *
 *   3. Mapped, read-only:
 *
 *       mapped present writable buf dirty
 *         yes    yes     no     yes yes/no
 *
 *      This state is used for clock algorithm.
 *
 *   4. Not mapped, but not released:
 *
 *       mapped present writable buf dirty
 *         no     yes      no    yes yes/no
 *
 *
 *   5. Synced, released:
 *
 *       mapped present writable buf dirty
 *         no      no      no    no   no
 *
 */
struct Page {
    adl::uintptr_t addr = 0;  // aligned to 4KB.
    // size must be 4KB.

    bool mapped = false;
    bool present = false;
    bool writable = false;

    /**
     * Whether once set to `writable` after last sync.
     */
    bool dirty = false;
    
    adl::int64_t mnemosyneId = 0;
    adl::int64_t blockId = 0;

    Genode::Ram_dataspace_capability buf;
};


}
