/*
 * main.h : defines struct Main.
 *
 * gongty [at] tongji [dot] edu [dot] cn
 * Created on 2025.1.25 at Xiangzhou, Zhuhai, Guangdong
 *
 */


#pragma once

#include <adl/collections/HashMap.hpp>
#include <monkey/net/protocol.h>
#include <monkey/net/Socket4.h>

#include <libc/component.h>
#include <base/heap.h>


/**
* Used on runtime.
*/
struct MemoryNodeInfo {
    adl::int64_t id;
    monkey::net::IP4Addr ip;
    adl::uint32_t port;
};


struct ConciergeMain {

    Genode::Env& env;
    Genode::Heap heap { env.ram(), env.rm() };

    monkey::net::Socket4 server;
    adl::uint16_t port = 0;

    adl::HashMap<adl::int64_t, MemoryNodeInfo> memoryNodes;  // id -> info

    struct {
        adl::ArrayList<adl::ByteArray> memoryNodes;
        adl::HashMap<adl::int64_t, adl::ByteArray> apps;  // id -> key
    } keyrings;


    ConciergeMain(Genode::Env&);


    adl::int64_t nextMemoryNodeId = 900000001LL;
    inline adl::int64_t genMemoryNodeId() { return nextMemoryNodeId++; }

    void initAdlAlloc();
    monkey::Status loadConfig();
    monkey::Status init();

    /**
     * This method is NOT responsible for closing socket passed to it.
     * Caller must manage life cycle of the Socket4 in the argument list.
     */
    void serveClient(monkey::net::Socket4&);
    monkey::Status run();
    void cleanup();


};
