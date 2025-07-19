/*
    Memory Map

    Created on 2025.1.14

    gongty  [at] tongji [dot] edu [dot] cn
    feng.yt [at]  sjtu  [dot] edu [dot] cn
    Jie Yingqi

*/


#include <base/env.h>
#include <monkey/genodeutils/memory.h>

namespace monkey::genodeutils {

Status getMemoryMap(
    Genode::Env& env, 
    adl::ArrayList<MemoryMapEntry>& out,
    adl::uintptr_t from,
    adl::uintptr_t until
) {
    
    // `from` and `until` should align to 4K boundary.
    if ((from & 0xfff) || (until & 0xfff))
        return Status::INVALID_PARAMETERS;


    out.clear();
    if (from >= until)
        return Status::SUCCESS;

    auto availMem = env.pd().avail_ram().value;
    if (availMem < 128 * 1024) {  // n KB
        return Status::OUT_OF_RESOURCE;
    }

    Status status = Status::SUCCESS;

    
    // determine large prober size

    const adl::size_t PROBE_SIZE_MAX = 128 * 1024 * 1024; // 64MB
    const adl::size_t PROBE_SIZE_MIN = 4096;                // 4K
    adl::size_t proberSize = PROBE_SIZE_MAX;

    if (availMem < PROBE_SIZE_MAX * 2) { // To prevent the maximum prober from being too large
        auto size = availMem / 2;
        if (size < 4096)
            return Status::OUT_OF_RESOURCE;

#ifdef __GNUC__
        size = 1 << (sizeof(size) * 8 - 1 - unsigned(__builtin_clzl(size)) );
#else
        {
            adl::size_t mask = 0x1;
            while (mask < size) {
                mask <<= 1;
                mask |= 1;
            }

            size &= ( ~ mask ) >> 1;
        }
#endif

        proberSize = size;
    }


    if (proberSize > until - from)
        proberSize = until - from;

    // allocate probers

    auto dynamicProber = env.ram().alloc(proberSize);

    // probe memory

    const auto addRecord = [&] (adl::uintptr_t addr, adl::size_t size, MemoryMapEntry::Type type) {
        if (out.isEmpty() || out.back().type != type || out.back().addr + out.back().size != addr) {
            out.append({
                .type = type,
                .addr = addr,
                .size = size
            });
        }
        else {
            out.back().size += size;
        }


    };

    adl::uintptr_t curr = from;
    int freeSuccessNum = 0;
    int ocpSuccessNum = 0;
    int i = 0;

    while (curr < until) {
        i++;
        try {
            env.rm().attach_at(dynamicProber, curr);
            env.rm().detach(curr);
            freeSuccessNum++;
            ocpSuccessNum = 0;
            addRecord(curr, proberSize, MemoryMapEntry::Type::FREE);
            Genode::log("addRecord: ", curr, " - ", curr + proberSize, " FREE");
        } catch (...) {
            freeSuccessNum = 0;
            if (ocpSuccessNum >= 3 || proberSize == PROBE_SIZE_MIN) {
                ocpSuccessNum++;
                addRecord(curr, proberSize, MemoryMapEntry::Type::OCCUPIED);
                Genode::log("addRecord: ", curr, " - ", curr + proberSize, " OCCUPIED");
            } else {
                proberSize = PROBE_SIZE_MIN; // vaddr occupied.
                env.ram().free(dynamicProber);
                dynamicProber = env.ram().alloc(proberSize);
                Genode::log("Prober down to ", proberSize);
                continue;
            }
        }

        curr += proberSize;

        if (proberSize < PROBE_SIZE_MAX && (freeSuccessNum >= 3 || ocpSuccessNum >= 3)) {
            proberSize *= 2;
            env.ram().free(dynamicProber);
            dynamicProber = env.ram().alloc(proberSize);
            Genode::log("Prober doubled to ", proberSize);
        }
    }

    Genode::log("Memory map probed.");
    Genode::log("Probed ", i, " times.");
    // cleanup
    
    env.ram().free(dynamicProber);
    return status;
}


}

