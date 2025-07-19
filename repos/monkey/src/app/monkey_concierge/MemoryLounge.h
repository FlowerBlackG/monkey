/*
 * Memory Lounge : Serves Memory Node connection
 * 
 * gongty [at] tongji [dot] edu [dot] cn
 * Created on 2025.1.25 at Xiangzhou, Zhuhai, Guangdong
 */


#pragma once

#include <monkey/net/SunflowerLounge.h>
#include <monkey/net/IP4Addr.h>
#include "./main.h"

struct MemoryLounge : public monkey::net::SunflowerLounge<ConciergeMain, monkey::net::Protocol1Connection> {

    MemoryLounge(
        ConciergeMain& context,
        monkey::net::Protocol1Connection& conn
    )
    : SunflowerLounge(context, conn)
    {}


    monkey::Status processMemoryNodeClockIn(const monkey::net::IP4Addr& ip4Addr, adl::uint16_t port);
    monkey::Status processGetIdentityKeys();


    virtual monkey::Status serve() override;
};
