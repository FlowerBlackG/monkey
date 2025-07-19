/*

    Backgound maintenance thread for monkey tycoon.

    created on 2025.5.13 at Wujing, Minhang, Shanghai


*/

#include <monkey/tycoon/Tycoon.h>
#include <monkey/tycoon/MaintenanceThread.h>

using namespace monkey;


tycoon::MaintenanceThread::MaintenanceThread(
    Genode::Env& env,
    Tycoon& tycoon
) :
    Genode::Thread(env, "Tycoon Maintenance Thread", 16 * 1024),
    tycoon(tycoon),
    timer(env)
{

}


void tycoon::MaintenanceThread::doMaintenance() {

    Genode::Mutex::Guard _g {tycoon.pageMaintenanceLock};

    // TODO
}


void tycoon::MaintenanceThread::entry() {
    Genode::log("[Tycoon Maintenance Thread] Started.");

    running = true;
    
    while (running) {
        doMaintenance();
        timer.msleep(1000);
    }

    Genode::log("[Tycoon Maintenance Thread] Stopped.");
}


void tycoon::MaintenanceThread::stop() {
    running = false;
}



void tycoon::MaintenanceThread::stopAndJoin() {
    this->stop();
    this->join();
}

