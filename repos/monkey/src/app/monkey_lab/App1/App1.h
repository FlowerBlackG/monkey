/*
    App 1

    Created on 2025.6.22 at Minhang
    By gongty
*/

#pragma once

#include "../LabApp.h"
#include <timer_session/connection.h>

#include <libc/component.h>
#include <time.h>


using namespace monkey;

class App1 : public LabApp {
protected:
    Timer::Connection timer;


public:

    App1(
        Genode::Env& env,
        Genode::Heap& heap,
        monkey::Tycoon& tycoon
    ) : LabApp(env, heap, tycoon), timer(env) { }

    monkey::Status run() override;

};


inline monkey::Status App1::run() {
    // Implement the specific logic for App1 here

    Genode::log("hello from app1");

    char c = 'a';

    while (true) {
        ((char*)0x100000000000)[2] = c;
        char s[2] = "";
        s[0] = c;

        adl::int64_t currentTimeUs = -1;
        
        Libc::with_libc([&] () {

            struct timespec spec;
            clock_gettime(CLOCK_REALTIME, &spec);
            currentTimeUs = spec.tv_sec * 1000000 + spec.tv_nsec / 1000;

        });

        Genode::log("[App1] Offset 0x2 set to character: ", (const char*)s, ", time: ", currentTimeUs);

        c += 1;
        if (c > 'z')
            c = 'a';

        timer.msleep(1000);
    }

    return monkey::Status::SUCCESS;
}
