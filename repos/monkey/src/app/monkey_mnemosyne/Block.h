/*

    Memory Block

    Created on 2025.2.7 at Xiangzhou, Zhuhai, Guangdong

    gongty [at] tongji [dot] edu [dot] cn

*/


#pragma once

#include <adl/sys/types.h>
#include <adl/collections/HashMap.hpp>

struct Block {
    adl::size_t size = 0;
    char* data = nullptr;

    adl::int64_t id = 0;

    struct {
        adl::int64_t readonly;
        adl::int64_t readwrite;
    } accessKey;


    enum class ReferenceType : adl::int8_t {
        READ_ONLY,
        READ_WRITE
    };


    adl::HashMap<adl::int64_t, ReferenceType> references;  // app id -> whether this app has referenced this block.
    adl::int64_t version = 0;  // Version of this block's data. Incremented when data is written.
    
    bool isReferenced() {
        return references.size() > 0;
    }
    
    adl::size_t getReferenceCount() {
        return references.size();
    }

};

