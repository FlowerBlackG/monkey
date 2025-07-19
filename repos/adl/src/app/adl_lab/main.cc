/*
 * ADL Lab : For some experimental coding.
 *
 * Created on 2025.1.2 at Minhang, Shanghai
 * 
 * 
 * gongty  [at] tongji [dot] edu [dot] cn
 * 
 */


#define _BYTEORDER_FUNC_DEFINED
#define _BYTEORDER_PROTOTYPED

#include <base/component.h>
#include <libc/component.h>
#include <base/heap.h>
#include <base/allocator.h>


#include <adl/string.h>
#include <adl/TString.h>
#include <adl/Allocator.h>
#include <adl/config.h>
#include <adl/arpa/inet.h>

#include <adl/collections/RedBlackTree.hpp>


using namespace Genode;


struct Main {
    Genode::Env& env;
    Genode::Sliced_heap slicedHeap { env.ram(), env.rm() };

    void initAdlAlloc() {
        adl::defaultAllocator.init({

            .alloc = [] (adl::size_t size, void* data) {
                return reinterpret_cast<Genode::Sliced_heap*>(data)->alloc(size);
            },
            
            .free = [] (void* addr, adl::size_t size, void* data) {
                reinterpret_cast<Genode::Sliced_heap*>(data)->free(addr, size);
            },
            
            .data = &slicedHeap
        });
    }

    Main(Genode::Env& env) : env(env)
    {
        initAdlAlloc();

        adl::RedBlackTree<adl::TString, adl::TString> rb;
        rb.setData("hello", "world");
        rb.setData("qzl", "nice");
        rb.setData("fyt", "ppppp-pppp");
        rb.setData("cym", "ly");
        rb.setData("zsa", "yes!");

        Genode::log(rb.getData("qzl").c_str());
        Genode::log(rb.getData("cym").c_str());
        Genode::log(rb.getData("fyt").c_str());
    }
};


void Component::construct(Env& env) {
    static Main main(env);
}