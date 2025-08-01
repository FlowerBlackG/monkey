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

#include "./Apps.h"

using namespace Genode;
using namespace monkey;


struct AppMain {
    Genode::Env& env;
    Genode::Heap heap { env.ram(), env.rm() };
    Tycoon tycoon;
    Genode::Attached_rom_dataspace configDs { env, "config" };

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


    Xml_node configRoot() {
        return configDs.xml().sub_node("monkey-lab");
    }


    Xml_node tycoonConfigRoot() {
        return configRoot().sub_node("monkey-tycoon");
    }


    const adl::TString& appKey() {
        static adl::TString appkey;

        if (appkey.length() == 0) {
            auto keyNode = tycoonConfigRoot().sub_node("app").sub_node("key");
            appkey = genodeutils::config::getText(keyNode);
        }

        return appkey;
    }


    Status initTycoon() {
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

        return tycoon.init(tycoonConfigRoot(), params);
    }


    monkey::Status runLabApp() {
        auto& key = appKey();
        if (key == "f578bd06-6f8e-42b3-8be9-860c7c645549") {
            return App1(env, heap, tycoon).run();
        }
        else if (key == "8370c1fe-d422-42d9-a261-05aed72313c3") {
            return App2(env, heap, tycoon).run();
        }
        else {
            return monkey::Status::NOT_FOUND;
        }
    }

    
    AppMain(Genode::Env& env) : env(env), tycoon(env)
    {
        Libc::with_libc([&] () {
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
            do {
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
                status = tycoon.start(addr, size); // sel4/hw
                if (status != monkey::Status::SUCCESS) {
                    break;
                }

                Genode::log("Tycoon started.");
                adl::size_t tycoonRam = 0;
                tycoon.checkAvailableMem(&tycoonRam);
                Genode::log("There are ", tycoonRam, " bytes ram on monkey memory network.");
            } while (0); // end of memory probing

            if (status != monkey::Status::SUCCESS) {
                Genode::error("Tycoon start failed with status ", adl::int32_t(status));
                tycoon.stop();
                return;
            }

            auto appStatus = runLabApp();
            
            Genode::log("App exited with code ", adl::int32_t(appStatus), ", Monkey Lab end.");

            tycoon.stop();
        });
    }
};


void Libc::Component::construct(Libc::Env& env) {
    static AppMain appMain(env);
}
