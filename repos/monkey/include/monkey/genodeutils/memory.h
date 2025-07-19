/*
    Memory 

    Created on 2025.1.14

    gongty  [at] tongji [dot] edu [dot] cn
    feng.yt [at]   sjtu [dot] edu [dot] cn
    Jie Yingqi

*/


#pragma once

#include <adl/sys/types.h>
#include <adl/collections/ArrayList.hpp>
#include <monkey/Status.h>

namespace Genode { class Env; }

namespace monkey::genodeutils {

struct MemoryMapEntry {
    enum class Type : adl::int32_t {
        FREE = 0,
        OCCUPIED,
        UNKNOWN
    } type;

    adl::uintptr_t addr;
    adl::uintptr_t size;
};


Status getMemoryMap(
    Genode::Env&, 
    adl::ArrayList<MemoryMapEntry>& out,

    adl::uintptr_t from = 0x170002000ull,
    adl::uintptr_t until = 0x4000000000ull 
);


}
