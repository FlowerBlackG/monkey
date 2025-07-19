/*
 * Monkey Lab : For some experimental coding.
 *
 * Created on 2024.12.25 at Minhang, Shanghai
 * 
 * 
 * feng.yt [at]  sjtu  [dot] edu [dot] cn
 * gongty  [at] tongji [dot] edu [dot] cn
 * 
 */


#include <adl/sys/types.h>

#include <libc/component.h>
#include <base/heap.h>

#include <adl/collections/HashMap.hpp>

#include <monkey/genodeutils/memory.h>
#include <monkey/tycoon/Tycoon.h>

#include "./config.h"

using namespace Genode;
using namespace monkey;


struct AppMain {
    Genode::Env& env;
    Genode::Heap heap { env.ram(), env.rm() };
    Tycoon tycoon;

    void initAdlAlloc() {
        adl::defaultAllocator.init({

            .alloc = [] (adl::size_t size, void* data) {
                return reinterpret_cast<Genode::Heap*>(data)->alloc(size);
            },
            
            .free = [] (void* addr, adl::size_t size, void* data) {
                reinterpret_cast<Genode::Heap*>(data)->free(addr, size);
            },
            
            .data = &heap
        });
    }


    Status initTycoon() {
        Genode::Attached_rom_dataspace configDs { env, "config" };
        auto configRoot = configDs.xml().sub_node("monkey-lab");
        auto tycoonRoot = configRoot.sub_node("monkey-tycoon");

        monkey::Tycoon::InitParams params;

        // determine params.nbuf
        {
            auto availMem = env.pd().avail_ram().value;
            availMem -= MONKEY_LAB_LOCAL_HEAP_MEMORY_RESERVED;
            auto availPages = availMem / 4096;
            if (availPages < 2) {
                Genode::error("Only ", availPages, " page(s) left. R u kidding me???");
                return Status::OUT_OF_RESOURCE;
            }

            params.nbuf = availPages;

            // todo
params.nbuf = 1;  // for testing.
            // todo
        }

        tycoon.init(tycoonRoot, params);

        return Status::SUCCESS;
    }

    
    AppMain(Genode::Env& env) : env(env), tycoon(env)
    {
        Libc::with_libc([&] () {
            
            Genode::log("monkey lab main");
            initAdlAlloc();

            Status status = Status::SUCCESS;

            try {
                status = initTycoon();
            }
            catch (...) {
                Genode::error("Something went wrong loading config. Exit.");
                return;
            }

            if (status != Status::SUCCESS) {
                return;
            }

            // probe vaddr for Tycoon
            {
                adl::uintptr_t addr = 0;
                adl::size_t size = 0;

#ifdef GENODE_SEL4
                Genode::log("Running on seL4");
                addr = 0x170002000;
                size = 0x3e8fffe000;
#elif defined(GENODE_NOVA)
                Genode::log("Running on NOVA");
                addr = 0x170002000;
                size = 0x7ffe4fffe000;
#elif defined(GENODE_HW)
                Genode::log("Running on HW");
                addr = 0x170002000;
                size = 0x3e8fffe000;
#else
                Genode::error("Unknown kernel\n");
                return;
#endif
                tycoon.start(addr, size); // sel4/hw
                adl::size_t tycoonRam = 0;
                tycoon.checkAvailableMem(&tycoonRam);
                Genode::log("There are ", tycoonRam, " bytes ram on monkey memory network.");
            } // end of memory probing

            ((char*)0x100000000030)[2] = 'c';


            ((char*)0x100002000030)[2] = 'd';

            Genode::log("((char*)0x100000000030)[2]: ", Genode::Hex( ((char*)0x100000000030)[2]));
            Genode::log("((char*)0x100002000030)[2]: ", Genode::Hex(((char*)0x100002000030)[2]));

            tycoon.stop();
            Genode::log("Monkey Lab end.");
        
        });
    }
};


void Libc::Component::construct(Libc::Env& env) {
    static AppMain appMain(env);
}
