/*
 * 
 * gongty [at] tongji [dot] edu [dot] cn
 * Created on 2025.2.10 at Yushan, Shangrao
 */


#pragma once

#include <monkey/net/SunflowerLounge.h>
#include <adl/collections/HashMap.hpp>

#include "./main.h"
#include "./Block.h"

struct AppLounge : monkey::net::SunflowerLounge<MnemosyneMain, monkey::net::Protocol2Connection>
{
    AppLounge(
        MnemosyneMain& main,
        monkey::net::Protocol2Connection& client
    )
    : SunflowerLounge(main, client)
    {}

    // block id -> block
    adl::HashMap<adl::int64_t, Block> memoryBlocks;

    monkey::Status processTryAlloc();
    monkey::Status processReadBlock(adl::int64_t blockId);
    monkey::Status processWriteBlock(adl::int64_t blockId, const adl::ByteArray& data);
    monkey::Status processCheckAvailMem();
    monkey::Status processFreeBlock(adl::int64_t blockId);

    monkey::Status processRefBlock(adl::int64_t accessKey);
    monkey::Status processUnrefBlock(adl::int64_t blockId);
    monkey::Status processGetBlockDataVersion(adl::int64_t blockId);

    virtual monkey::Status serve() override;
};

