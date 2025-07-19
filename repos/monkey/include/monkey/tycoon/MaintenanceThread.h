/*

    Backgound maintenance thread for monkey tycoon.

    created on 2025.5.13 at Wujing, Minhang, Shanghai


*/

#pragma once 

#include <base/thread.h>
#include <timer_session/connection.h>

namespace monkey { class Tycoon; }


namespace monkey::tycoon {


class MaintenanceThread : public Genode::Thread {
protected:
    Tycoon& tycoon;
    Timer::Connection timer;
    bool running = false;

    void doMaintenance();


public:
    virtual void entry() override;

    /**
     * Can be called from external threads.
     */
    void stop();


    /**
     * Can be called from external threads.
     */
    void stopAndJoin();



    MaintenanceThread(Genode::Env& env, Tycoon& tycoon);
};


}  // namespace monkey::tycoon
