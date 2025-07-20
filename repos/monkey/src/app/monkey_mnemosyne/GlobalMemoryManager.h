/*
    Mnemosyne Global Memory Manager

    By
        gongty [at] alumni [dot] tongji [dot] edu [dot] cn
    
    Created on 2025.6.15 at Minhang, Shanghai

*/

#pragma once

#include <adl/sys/types.h>
#include <base/env.h>
#include <base/allocator.h>
#include <base/heap.h>

#include <adl/collections/HashMap.hpp>
#include <adl/collections/ArrayList.hpp>

#include "./Block.h"
#include "./config.h"


// Directed by Monkey Protocol V2
class GlobalMemoryManager
{
protected:
    Genode::Env& env;
    Genode::Heap heap { env.ram(), env.rm() };


    // block id -> block struct
    // TODO: consider using wayland-style linked list so we can find apps' block faster by app id.
    adl::HashMap<adl::int64_t, Block*> memoryBlocks;

    struct {
        Genode::Mutex blockMap;
        Genode::Mutex blockAllocation;
    } locks;

    adl::int64_t nextMemoryBlockId = 5000000001;
    adl::int64_t nextAccessKey = 10000001;


    enum {
        LOCK_BLOCK_MAP = true,
        DONT_LOCK_BLOCK_MAP = false,
    };

    void freeMemoryBlock(Block* b, bool lockBlockMap = LOCK_BLOCK_MAP) {
        
        if (lockBlockMap == LOCK_BLOCK_MAP) {
            locks.blockMap.acquire();
        }
        
        memoryBlocks.removeKey(b->id, true);
        if (lockBlockMap == LOCK_BLOCK_MAP) {
            locks.blockMap.release();
        }
        
        {
            Genode::Mutex::Guard _g { locks.blockAllocation };
            heap.free(b->data, b->size);
        }

        adl::defaultAllocator.free(b);
        Genode::log("Released Block id: ", b->id, ", size ", b->size);
    }
public:
    GlobalMemoryManager(Genode::Env& env)
    : env {env}
    {
        
    }

    virtual ~GlobalMemoryManager() {}


    /**
     * For app lounge.
     * @param nodeId the node id of this block, used to generate access key.
     * @return alloced block pointer if success
     */
    Block* allocMemoryBlock(adl::int64_t appId, adl::int64_t nodeId) {
        const adl::size_t size = 4096;

        Genode::Mutex::Guard _g { locks.blockAllocation };

        const adl::size_t finalSize = size;

        if (env.pd().avail_ram().value < MONKEY_MNEMOSYNE_HEAP_MEMORY_RESERVED + finalSize) {
            return nullptr;
        }

        Block* b = adl::defaultAllocator.alloc<Block>();
        b->data = (char*) heap.alloc(finalSize);
        if (!b->data) {
            Genode::error("Failed to allocate memory block. Memory is not enough !!!");
            adl::defaultAllocator.free(b);
            return nullptr;
        }

        b->id = nextMemoryBlockId++;
        b->size = finalSize;

        // only use 16 bit of node_id, 48 bits of nextAccessKey , we assume node_id
        // is less than 65536.
        b->accessKey.readonly = ((nodeId & 0xffff) << 48 | (nextAccessKey++ & 0xFFFFFFFFFFFF));
        b->accessKey.readwrite = ((nodeId & 0xffff) << 48 | (nextAccessKey++ & 0xFFFFFFFFFFFF));
        b->version = 0;


        Genode::log("Allocated block. Block id: ", b->id, ", size ", b->size);

        {
            Genode::Mutex::Guard _g { locks.blockMap };
            this->memoryBlocks[b->id] = b;
        }

        b->references[appId] = Block::ReferenceType::READ_WRITE;  // The creator of this block is the first one to reference it.

        return b;
    }


    /**
     * Block's creator should call this since it has been
     * seen as normal client as other who ref this block.
     * 
     * @param accessKey 
     * @return -1 for failure, otherwise the block id. 
     */
    adl::int64_t refMemoryBlock(adl::int64_t appId, adl::int64_t accessKey) {
        Genode::Mutex::Guard _g { locks.blockMap };
        for (auto it : memoryBlocks) {
            Block& block = *it.second;
            bool canRead = (block.accessKey.readonly == accessKey);
            bool canWrite = (block.accessKey.readwrite == accessKey);
            if (canRead || canWrite) {
                block.references[appId] = (canWrite) ? Block::ReferenceType::READ_WRITE : Block::ReferenceType::READ_ONLY;
                return block.id;
            }
        }

        return -1;
    }

    void unrefMemoryBlock(adl::int64_t appId, adl::int64_t blockId) {
        
        Genode::Mutex::Guard _g { locks.blockMap };
        if (!memoryBlocks.contains(blockId)) {
            return;
        }

        Block* b = memoryBlocks[blockId];

        // remove using noExcept mode. So nothing happens if appId is not found.
        b->references.removeKey(appId, true);
        
        if (b->references.size() == 0) {
            // No one references this block, we can free it.
            freeMemoryBlock(b, DONT_LOCK_BLOCK_MAP);
        }
    }


    enum GET_BLOCK_PERMISSION {
        READ = 0,
        WRITE = 1
    };

    Block* getMemoryBlock(adl::int64_t appId, adl::int64_t blockId, adl::int8_t permission) {
        Genode::Mutex::Guard _g { locks.blockMap };
        if (!memoryBlocks.contains(blockId)) {
            Genode::warning("[GlobalMemoryManager] No such block. Block id: ", blockId);
            return nullptr;  // No such block.
        }

        Block& block = *memoryBlocks[blockId];
        if (!block.references.contains(appId)) {
            Genode::warning("[GlobalMemoryManager] No permission for app ", appId, " to access block ", blockId);
            return nullptr;  // No any permission.
        }

        // Check permission.

        if (permission == WRITE && block.references[appId] != Block::ReferenceType::READ_WRITE) {
            Genode::warning("[GlobalMemoryManager] App ", appId, " tries to write block ", blockId, 
                            " but it is not writable. Permission: ", (int) block.references[appId]);
            return nullptr;  // You don't have write permission.
        }
        return &block;  
    }

};
