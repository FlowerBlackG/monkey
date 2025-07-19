/*
 * App Lounge : Serves App connection
 * 
 * gongty [at] tongji [dot] edu [dot] cn
 * Created on 2025.1.25 at Xiangzhou, Zhuhai, Guangdong
 */


#pragma once

#include <monkey/net/SunflowerLounge.h>
#include "./main.h"

struct AppLounge : public monkey::net::SunflowerLounge<ConciergeMain, monkey::net::Protocol1Connection> {

    AppLounge(
        ConciergeMain& context,
        monkey::net::Protocol1Connection& conn
    )
    : SunflowerLounge(context, conn)
    {}

    monkey::Status processLocateMemoryNodes();

    virtual monkey::Status serve() override;
};
