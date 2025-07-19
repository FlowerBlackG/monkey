/*
 * Monkey Mnemosyne : Memory provider.
 *
 * Created on 2025.2.4 at Xiangzhou, Zhuhai, Guangdong
 * 
 * 
 * gongty  [at] tongji [dot] edu [dot] cn
 * 
 */


#pragma once

#include "./config.h"

#include <adl/sys/types.h>
#include <adl/collections/ArrayList.hpp>
#include <adl/collections/HashMap.hpp>

#include <libc/component.h>
#include <base/heap.h>
#include <base/thread.h>

#include <monkey/Status.h>
#include <monkey/net/IP4Addr.h>
#include <monkey/net/Socket4.h>

#include "./Block.h"

struct MnemosyneMain {

    Genode::Env& env;
    Genode::Heap heap { env.ram(), env.rm() };
    adl::int64_t nodeId = 0;

    // block id -> block struct
    // TODO: consider using wayland-style linked list so we can find apps' block faster by app id.
    adl::HashMap<adl::int64_t, Block> memoryBlocks;

    adl::HashMap<adl::int64_t, adl::ByteArray> appKeys;

    // node id -> lounge thread
    adl::HashMap<adl::int64_t, Genode::Thread*> loungeThreads;

    struct {
        adl::ArrayList<Genode::Thread*> bin;
        Genode::Mutex lock;
    } threadRecycleBin;


    struct {
        struct {
            monkey::net::IP4Addr ip;
            adl::uint16_t port;
        } concierge;

        struct {
            monkey::net::IP4Addr ip;
            adl::uint16_t port;
            adl::uint16_t listenPort;
            adl::ByteArray key;
        } mnemosyne;
    } config;

    MnemosyneMain(Genode::Env&);

    /**
     * For app lounge.
     * @param size block size
     * @param record `true` then it would be recorded by mnemosyne main.
     * @return if failed, Block's size would be set to 0.
     */
    Block allocMemoryBlock(adl::size_t size, adl::int64_t owner, bool record = true);
    Genode::Mutex memoryBlockAllocationLock;
    adl::int64_t nextMemoryBlockId = 5000000001;

    void freeMemoryBlock(Block&);

    monkey::Status loadConfig();
    monkey::Status init();

    monkey::Status clockIn();
    void serveClient(monkey::net::Socket4& conn);
    monkey::Status runServer();

    monkey::Status run();
    void cleanup();


};


