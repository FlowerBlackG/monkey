/*

    monkey tycoon : monkey system app driver.

    created on 2025.2.17 at Wujing, Minhang, Shanghai


*/


#pragma once

#define MONKEY_TYCOON_INCLUDE_INTERNALS
    #include <monkey/tycoon/Page.h>
#undef MONKEY_TYCOON_INCLUDE_INTERNALS


#include <base/mutex.h>

#include <monkey/net/protocol.h>
#include <monkey/genodeutils/config.h>
#include <monkey/dock/Connection.h>

#include <region_map/client.h>

#include <monkey/tycoon/MaintenanceThread.h>

#include <adl/recursive_mutex>


namespace monkey {

class Tycoon;


class UserAllocator {
protected:
    Tycoon& tycoon;
public:
    UserAllocator(Tycoon& tycoon) : tycoon(tycoon) {}

    void* alloc(adl::size_t size);
    void* allocPage(adl::size_t nPages = 1);
    void free(void* addr);
    void freePage(void* addr, adl::size_t nPages = 1);
};

    
/**
 * Monkey Tycoon. Works like a daemon.
 * 
 * You activate it, and do your jobs like it doesn't exists.
 */
class Tycoon {

protected:
    Tycoon(const Tycoon&) = delete;
    void operator = (const Tycoon&) = delete;


protected:

    struct PageFaultSignalBridge : public Genode::Entrypoint {
        Tycoon& tycoon;
        Genode::Signal_handler<monkey::Tycoon::PageFaultSignalBridge> _handler;

        void handle() { tycoon.handlePageFaultSignal(); }

        PageFaultSignalBridge(Tycoon& tycoon, Genode::Env& env)
        :
        Genode::Entrypoint(
            env, 
            sizeof(Genode::addr_t) * 2048 /* todo */, 
            "Monkey Tycoon Page Fault Signal Bridge",
            Genode::Affinity::Location()
        ),
        tycoon(tycoon),
        _handler(*this, *this, &monkey::Tycoon::PageFaultSignalBridge::handle)
        {
            Genode::Region_map_client rm { env.pd().address_space() };
            rm.fault_handler(_handler);
        }
    
    } pageFaultSignalBridge;

    bool initialized = false;


    struct {
        struct {
            net::IP4Addr ip;
            adl::uint16_t port;
        } concierge;

        struct {
            adl::ByteArray key;
        } app;
    } config;

    Genode::Env& env;
    Genode::Region_map_client regionMapClient { env.pd().address_space() };

    struct {
        adl::uintptr_t vaddr;
        adl::size_t size;
        bool manage = false;
    } memSpace;


    struct {
        net::Protocol2ConnectionDock concierge;
        adl::HashMap<adl::int64_t, net::Protocol2ConnectionDock*> mnemosynes;
    } connections;

    adl::ArrayList<net::Protocol2Connection::MemoryNodeInfo> memoryNodesInfo;

    // addr (4KB aligned) to Page struct.
    adl::HashMap<adl::uintptr_t, tycoon::Page> pages;

    // only stores empty buffers.
    adl::ArrayList<Genode::Ram_dataspace_capability> buffers;
    
    dock::Connection dock;

    tycoon::MaintenanceThread maintenanceThread;

    adl::recursive_mutex pageMaintenanceLock;
    
    UserAllocator userAllocator;

protected:

    /**
     * Open a connection with concierge or mnemosyne.
     * On success, connection would be logged to `connections` struct.
     *
     * If connection already exists and `force` is false, previous connection would be used.
     *
     * @param concierge `true` if you want to connect with concierge, `false` if mnemosyne.
     * @param id Mnemosyne id you'd like to connect. Ignored for concierge.
     * @param force Close previous connection and re-open a new one.
     */
    monkey::Status openConnection(bool concierge, adl::int64_t id = 0, bool force = false);

    void handlePageFaultSignal();

    monkey::Status loadPage(tycoon::Page&);

    /**
     * Try allocate a page.
     * If success, page would be logged to `pages`.
     *
     * @param addr Should be 4KB aligned.
     */
    monkey::Status allocPage(adl::uintptr_t addr);

    void disconnectConcierge();

    /**
     * 
     * Xml example:
     * <>
     *     <concierge>
     *         <ip>10.0.2.2</ip>
     *         <port>5555</port>
     *     </concierge>
     * </>
     */
    monkey::Status loadConfig(const Genode::Xml_node&);

    monkey::Status swapOut();
    monkey::Status sync(tycoon::Page&);
    

    monkey::Status fetchPageDataVersion(tycoon::Page& page, adl::int64_t* out);
    monkey::Status updateSharedPage(tycoon::Page& page);
public:

    const adl::uintptr_t MAINTENANCE_TMP_ADDR = 0x90000000ul;  // todo: really this address?

    Tycoon(Genode::Env& env) 
    : 
    pageFaultSignalBridge(*this, env),
    env(env),
    dock(env),
    maintenanceThread(env, *this),
    userAllocator(*this)
    {
        dock.makeBuffer(2);
        dock.getBuffer();
        dock.mapBuffer(8192);

        connections.concierge.dock = &dock;
    }

    virtual ~Tycoon();
    

    struct InitParams {
        // how many pages used for buffering.
        adl::size_t nbuf;
    };

    monkey::Status init(const Genode::Xml_node&, const InitParams&);



    monkey::Status sync();

    monkey::Status start(adl::uintptr_t vaddr, adl::size_t size);
    void stop();

    monkey::Status checkAvailableMem(adl::size_t* res);

    // For FreeMemoryManager.
    monkey::Status freePage(adl::uintptr_t addr);

    UserAllocator& getUserAllocator() { return this->userAllocator; }

    enum PageAccessRight {
        READ_ONLY = 1,
        READ_WRITE = 2
    };
    monkey::Status refPage(
        adl::uintptr_t vaddr, 
        adl::int64_t accessKey,
        PageAccessRight accessRight
    );

    monkey::Status unrefPage(adl::uintptr_t vaddr);
public:
    friend PageFaultSignalBridge;
    friend tycoon::MaintenanceThread;
    friend UserAllocator;
};


}  // namespace monkey

