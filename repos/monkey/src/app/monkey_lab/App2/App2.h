/*
    App 2

    Created on 2025.6.22 at Minhang
    By gongty
*/

#pragma once

#include "../LabApp.h"
#include <timer_session/connection.h>

using namespace monkey;

class App2 : public LabApp {
protected:
    Timer::Connection timer;


public:

    App2(
        Genode::Env& env,
        Genode::Heap& heap,
        monkey::Tycoon& tycoon
    ) : LabApp(env, heap, tycoon), timer(env) { }

    monkey::Status run() override;

};


inline monkey::Status App2::run() {
    Genode::log("hello from app2");

    const adl::intptr_t VADDR = 0x100000000000;

    
    const adl::int64_t ACCESS_KEY = ((900000001LL & 0xffff) << 48 | (10000002 & 0xFFFFFFFFFFFF));
    tycoon.refPage(VADDR, ACCESS_KEY, Tycoon::READ_WRITE);

    while (true) {
        timer.msleep(1500);
        char c = ((char*)VADDR)[2];
        char s[2] = "";
        s[0] = c;
        Genode::log("[App2] Offset 0x2 read character: ", (const char*)s);
    }


    return monkey::Status::SUCCESS;
}
