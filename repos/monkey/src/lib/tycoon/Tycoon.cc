/*

    monkey tycoon : monkey system app driver.

    created on 2025.2.17 at Wujing, Minhang, Shanghai


*/


#include <monkey/tycoon/Tycoon.h>

#include "./yros/MemoryManager.h"
#include "./yros/KernelMemoryAllocator.h"
#include "./yros/FreeMemoryManager.h"

using namespace monkey;

using HelloMode = net::ProtocolConnection::HelloMode;

/* ---------------- UserAllocator ---------------- */


void* UserAllocator::alloc(adl::size_t size) {
    return yros::memory::KernelMemoryAllocator::malloc(size);
}


void* UserAllocator::allocPage(adl::size_t nPages) {
    return yros::memory::KernelMemoryAllocator::allocPage(nPages);
}


void UserAllocator::free(void* addr) {
    yros::memory::KernelMemoryAllocator::free(addr);
}


void UserAllocator::freePage(void* addr, adl::size_t nPages) {
    yros::memory::KernelMemoryAllocator::freePage(addr, nPages);
}


/* ---------------- Tycoon ---------------- */


Tycoon::~Tycoon() {
    stop();

    for (auto& it : buffers) {
        env.pd().free(it);
    }
    buffers.clear();
}


monkey::Status Tycoon::loadConfig(const Genode::Xml_node& xml) {
    Status status = Status::SUCCESS;
    
    try {
        const auto& conciergeNode = xml.sub_node("concierge");
        const auto& ipNode = conciergeNode.sub_node("ip");
        const auto& portNode = conciergeNode.sub_node("port");
        const auto& appNode = xml.sub_node("app");
        const auto& keyNode = appNode.sub_node("key");
        
        status = config.concierge.ip.set(genodeutils::config::getText(ipNode));
        if (status != Status::SUCCESS) {
            throw status;
        }

        config.concierge.port = (adl::uint16_t) genodeutils::config::getText(portNode).toInt64();

        config.app.key = genodeutils::config::getText(keyNode);

        Genode::log(
            "Tycoon: Config loaded.\n"
            "> app key       : ", config.app.key.toString().c_str(), "\n"
            "> concierge ip  : ", config.concierge.ip.toString().c_str(), "\n"
            "> concierge port: ", config.concierge.port
        );
    }
    catch (...) {
        Genode::error("Tycoon: Something went wrong parsing config.");
        status = Status::INVALID_PARAMETERS;
    }

    return status;
}


monkey::Status Tycoon::init(const Genode::Xml_node& xml, const InitParams& params) {
    if (initialized) {
        Genode::warning("Tycoon: Already initialized. New call ignored.");
        return Status::SUCCESS;
    }

    yros::memory::FreeMemoryManager::tycoon = this;


    Status status = loadConfig(xml);
    if (status != Status::SUCCESS) {
        return status;
    }

    if (params.nbuf == 0) {
        return Status::INVALID_PARAMETERS;
    }

    for (adl::size_t i = 0; i < params.nbuf; i++) {
        auto ds = env.pd().alloc(4096);
        buffers.append(ds);
    }

    return Status::SUCCESS;
}


monkey::Status Tycoon::openConnection(bool concierge, adl::int64_t id, bool force) {

    if (force) { // close connection if `force`.
        if (concierge) {
            connections.concierge.close();
        }
        else if (connections.mnemosynes.hasKey(id)) {
            connections.mnemosynes[id]->close();
            adl::defaultAllocator.free(connections.mnemosynes[id]);
            connections.mnemosynes.removeKey(id);
        }
    }
    else {  // just return if connection already exists.
        if (concierge) {
            if (connections.concierge.valid())
                return Status::SUCCESS;
        }
        else if (connections.mnemosynes.hasKey(id)) {
            if (connections.mnemosynes[id]->valid())
                return Status::SUCCESS;
            else {
                adl::defaultAllocator.free(connections.mnemosynes[id]);
                connections.mnemosynes.removeKey(id);
            }
        }
    }


    Genode::log("Tycoon: Opening connection to ", (concierge ? "concierge" : "mnemosyne"), " with id ", id, ".");


    // ------ open new connection. ------

    Status status = Status::SUCCESS;
    net::Protocol2ConnectionDock conn;
    conn.dock = &dock;

    // determine ip and port.
    if (concierge) {
        conn.ip = config.concierge.ip;
        conn.port = config.concierge.port;
    }
    else {
        status = Status::NOT_FOUND;
        for (auto& it : memoryNodesInfo) {
            if (it.id == id) {
                status = Status::SUCCESS;
                conn.ip = it.ip;
                conn.port = it.port;
                conn.memoryNodeId = it.id;
                break;
            }
        }
        if (status != Status::SUCCESS) {
            Genode::error("Tycoon: Failed to find mnemosyne info of id ", id, ".");
            return status;
        }
    }
    

    // connect
    if ((status = conn.connect()) != Status::SUCCESS) {
        Genode::error("Failed to connect to ", conn.ip.toString().c_str(), ":", conn.port);
        return status;
    }

    // hello
    if ((status = conn.hello(net::protocol::LATEST_VERSION, HelloMode::CLIENT)) != Status::SUCCESS) {
        Genode::error("Failed on Hello.");
        conn.close();
        return status;
    }


    // auth
    if ((status = conn.auth(config.app.key)) != Status::SUCCESS) {
        Genode::error("Failed on auth.");
        conn.close();
        return status;
    }

    // save this connection
    net::Protocol2ConnectionDock* pConn = nullptr;
    if (concierge) {
        pConn = &connections.concierge;
    }
    else {
        auto newConn = adl::defaultAllocator.alloc<net::Protocol2ConnectionDock>();
        if (!newConn) {
            conn.close();
            return Status::OUT_OF_RESOURCE;
        }

        connections.mnemosynes[id] = newConn;
        pConn = newConn;
    }

    pConn->ip = conn.ip;
    pConn->port = conn.port;
    pConn->socketFd = conn.socketFd;
    pConn->memoryNodeId = conn.memoryNodeId;
    pConn->dock = &dock;

    return Status::SUCCESS;
}


void Tycoon::handlePageFaultSignal() {
    if (!memSpace.manage) {
        return;
    }

    Genode::Region_map& rm = env.rm();
    Genode::Region_map::State state = rm.state();
    auto faultAddr = state.addr;
    auto pageAddr = state.addr & ~0xffful;
    if (faultAddr < memSpace.vaddr || faultAddr >= memSpace.vaddr + memSpace.size) {
        // address not managed by Tycoon.
        return;
    }

    Genode::log("====== Tycoon: Page fault on ", Genode::Hex(state.addr), " caught. ======");
    Genode::log(
        "> Type: ",
        (
            state.type == Genode::Region_map::State::READ_FAULT  ? "READ_FAULT"  :
            state.type == Genode::Region_map::State::WRITE_FAULT ? "WRITE_FAULT" :
            state.type == Genode::Region_map::State::EXEC_FAULT  ? "EXEC_FAULT"  : "READY"
        ),
        "> Page Addr: ", Genode::Hex(pageAddr)
    );

    if (state.type == Genode::Region_map::State::EXEC_FAULT) {
        Genode::error("EXEC_FAULT not supported by Tycoon.");
        return;
    }


    adl::recursive_mutex::guard _g {this->pageMaintenanceLock};


    bool pageRegistered = pages.hasKey(pageAddr);
    if (pageRegistered) {
        auto& page = pages[pageAddr];
        if (page.present && page.mapped) {
            env.rm().detach(page.addr);
            env.rm().attach_at(page.buf, page.addr);
            page.writable = true;
            page.dirty = true;
            return;
        }
        else if (page.present) {
            env.rm().attach_at(page.buf, page.addr);
            page.writable = page.dirty = page.mapped = true;
            return;
        }
    }
    else {
        Status status = allocPage(pageAddr);
        if (status != Status::SUCCESS) {
            Genode::error("Tycoon handlePageFaultSignal: Failed to alloc page.");
            return;
        }
    }


    if (buffers.isEmpty()) {
        Status status = swapOut();
        if (status != Status::SUCCESS) {
            Genode::error("Failed to swap out. 7886c6f7-f1ba-4f04-816e-da295c363329");
            return;
        }
    }

    // assert: buffers not empty, pages[addr] allocated.

    auto& page = pages[pageAddr];

    page.present = true;
    page.mapped = true;
    page.addr = pageAddr;
    
    if (page.sharing == tycoon::Page::Sharing::READ_WRITE || page.sharing == tycoon::Page::Sharing::NONE) {
        page.writable = true;
    }
    else {
        page.writable = false;
    }
    
    if (!page.dirty)
        page.dirty = page.writable;

    page.buf = buffers.pop();
    page.present = true;

    if (!pageRegistered) {
        env.rm().attach(
            page.buf,
            4096,
            0,
            true,
            page.addr,
            false,
            page.writable
        );
        return;
    }
    

    // fetch old page stored on remote.

    Status status = loadPage(page);

    if (status != Status::SUCCESS) {
        page.mapped = false;
        page.dirty = false;
        Genode::error("Something went wrong. 5c9c81ec-0c49-40df-8f34-332a5b5f43a7");
        return;
    }
    
    env.rm().attach(
        page.buf,
        4096,
        0,
        true,
        page.addr,
        false,
        page.writable
    );
}


monkey::Status Tycoon::allocPage(adl::uintptr_t addr) {
    adl::recursive_mutex::guard _g {this->pageMaintenanceLock};

    adl::uintptr_t pageAddr = addr & ~0xffful;
    if (pages.hasKey(pageAddr)) {
        Genode::warning("Page at ", Genode::Hex(pageAddr), " already exists.");
        return Status::SUCCESS;
    }

    Status status;
    
    adl::int64_t mnemosyneId;

    tycoon::Page page;

    for (auto& it : memoryNodesInfo) {
        status = openConnection(false, it.id);
        if (status != Status::SUCCESS)
            continue;

        auto conn = connections.mnemosynes[it.id];

        status = conn->tryAlloc(&page.blockId, &page.dataVersion, &page.readKey, &page.writeKey);
        if (status != Status::SUCCESS) {
            Genode::log("Tycoon: Failed to alloc on ", it.ip.toString().c_str(), ":", it.port);
            Genode::log(
                "> ", conn->lastError.api.c_str(), ": ", 
                conn->lastError.code, ": ", 
                conn->lastError.msg.c_str()
            );
            continue;
        }

        mnemosyneId = it.id;
        break;
    }

    if (status != Status::SUCCESS) {
        return status;
    }

    page.addr = pageAddr;
    page.dirty = false;
    page.present = false;
    page.writable = false;
    page.mapped = false;
    page.mnemosyneId = mnemosyneId;

    pages[pageAddr] = page;

    Genode::log("Tycoon: Allocated page from remote.");
    Genode::log("> addr        : ", Genode::Hex(page.addr));
    Genode::log("> blockId     : ", page.blockId);
    Genode::log("> dataVersion : ", page.dataVersion);
    Genode::log("> readKey     : ", page.readKey);
    Genode::log("> writeKey    : ", page.writeKey);
    Genode::log("> mnemosyneId : ", page.mnemosyneId);
    
    return Status::SUCCESS;
}



monkey::Status Tycoon::freePage(adl::uintptr_t addr) {
    adl::uintptr_t pageAddr = addr & ~0xffful;
    adl::recursive_mutex::guard _g {this->pageMaintenanceLock};
    if (!pages.hasKey(pageAddr)) {
        return Status::NOT_FOUND;
    }

    auto& page = pages[pageAddr];

    // free

    Status status = openConnection(false, page.mnemosyneId);
    if (status != Status::SUCCESS) {
        Genode::error("Failed to free page: failed to open connection.");
        return status;
    }

    if ((status = connections.mnemosynes[page.mnemosyneId]->unrefBlock(page.blockId)) != Status::SUCCESS) {
        Genode::error("Failed to free page.");
        return status;
    }

    if (page.present) {
        buffers.append(page.buf);
        page.present = false;
    }

    pages.removeKey(pageAddr);
    return Status::SUCCESS;
}


void Tycoon::disconnectConcierge() {
    connections.concierge.close();
}


monkey::Status Tycoon::swapOut() {
    tycoon::Page* best = nullptr;
    adl::recursive_mutex::guard _g {this->pageMaintenanceLock};

    // TODO: This procedure should be optimized.
    for (auto it : pages) {
        auto& page = it.second;
        if (!page.present)
            continue;

        if (!best)
            best = &page;

        if (!page.dirty && !page.mapped) {
            best = &page;
            break;
        }
    }

    if (!best)
        return Status::NOT_FOUND;

    if (best->dirty) {
        Status status = sync(*best);
        if (status != Status::SUCCESS) {
            Genode::error("Failed to sync page. Failed to swap out.");
            return status;
        }
    }

    if (best->mapped) {
        env.rm().detach(best->addr);
        best->mapped = false;
    }

    best->present = false;
    buffers.append(best->buf);
    return Status::SUCCESS;
}


monkey::Status Tycoon::sync(tycoon::Page& page) {
    if (!page.present || !page.dirty)
        return Status::SUCCESS;
    adl::recursive_mutex::guard _g {this->pageMaintenanceLock};
    
    // assert: page.present, page.dirty

    Status status = openConnection(false, page.mnemosyneId);
    if (status != Status::SUCCESS) {
        Genode::error("Something went wrong. 3d6c38c7-4885-432c-8974-bd0fe95d28e2");
        return status;
    }

    if (!page.mapped) {
        env.rm().attach_at(page.buf, page.addr);
    }

    status = connections.mnemosynes[page.mnemosyneId]->writeBlock(
        page.blockId, 
        (void*) page.addr,
        &page.dataVersion
    );

    if (!page.mapped) {
        env.rm().detach(page.addr);
    }

    if (status != Status::SUCCESS) {
        Genode::error("Something went wrong. ccf7a0ae-eb41-4d48-af0e-0bbbb857f12d");
        return status;
    }

    if (!page.writable)
        page.dirty = false;
    return Status::SUCCESS;
}


monkey::Status Tycoon::sync() {
    Status status;
    for (auto it : pages) {
        status = sync(it.second);
        if (status != Status::SUCCESS) {
            return status;
        }
    }

    return Status::SUCCESS;
}


monkey::Status Tycoon::fetchPageDataVersion(tycoon::Page& page, adl::int64_t* out) {
    Status status = openConnection(false, page.mnemosyneId);
    if (status != Status::SUCCESS) {
        Genode::error("Something went wrong.");
        return status;
    }

    adl::recursive_mutex::guard _g {this->pageMaintenanceLock};
    if (!page.mapped) {
        env.rm().attach_at(page.buf, page.addr);
    }

    adl::int64_t dataVer;
    status = connections.mnemosynes[page.mnemosyneId]->getBlockDataVersion(
        page.blockId, 
        &dataVer
    );

    if (status == Status::SUCCESS) {
        *out = dataVer;
    }

    return status;
}


monkey::Status Tycoon::refPage(
    adl::uintptr_t vaddr, 
    adl::int64_t accessKey,
    PageAccessRight accessRight
) {
    if (!memSpace.manage) {
        return Status::INVALID_PARAMETERS;
    }

    adl::uintptr_t pageAddr = vaddr & ~0xffful;

    adl::recursive_mutex::guard _g {this->pageMaintenanceLock};

    if (pages.hasKey(pageAddr)) {
        return Status::INVALID_PARAMETERS;  // Address already used.
    }

    tycoon::Page page;
    page.addr = pageAddr;
    page.sharing = accessRight == PageAccessRight::READ_ONLY
        ? tycoon::Page::Sharing::READ_ONLY
        : tycoon::Page::Sharing::READ_WRITE;
    
    monkey::Status status = Status::NOT_FOUND;

    // try to find a mnemosyne to ref this page.
    for (auto it : this->memoryNodesInfo) {
        status = openConnection(false, it.id, false);
        if (status != Status::SUCCESS)
            continue;
        auto conn = connections.mnemosynes[it.id];
        status = conn->refBlock(accessKey, &page.blockId);
        if (status != Status::SUCCESS)
            continue;
        page.mnemosyneId = conn->memoryNodeId;
        Genode::error("page mnemosyne id set to: ", conn->memoryNodeId);
        Genode::error(">  real id: ", it.id);
        break;
    }


    if (status != Status::SUCCESS) {
        Genode::error("Tycoon: Failed to ref page. No available mnemosyne.");
        return status;
    }


    if (accessRight == PageAccessRight::READ_WRITE) {
        page.writable = true;
    }

    page.present = false;
    page.mapped = false;
    page.readKey = 0;
    page.writeKey = 0;
    
    pages[pageAddr] = page;
    return status;
}


monkey::Status Tycoon::unrefPage(adl::uintptr_t vaddr) {
    adl::uintptr_t pageAddr = vaddr & ~0xffful;

    adl::recursive_mutex::guard _g {this->pageMaintenanceLock};

    if (!pages.hasKey(pageAddr)) {
        return Status::NOT_FOUND;  // Address not used.
    }

    tycoon::Page& page = pages[pageAddr];

    if (page.sharing == tycoon::Page::Sharing::NONE) {
        Genode::error("Tycoon: Page not shared. Cannot unref.");
        return Status::INVALID_PARAMETERS;
    }

    if (page.mapped) {
        env.rm().detach(vaddr);
        page.mapped = false;
    }


    if (page.dirty) {
        sync(page);
        page.dirty = false;
    }

    openConnection(false, page.mnemosyneId, false);
    Status status = connections.mnemosynes[page.mnemosyneId]->unrefBlock(page.blockId);

    pages.removeKey(pageAddr);
    return status;
}


monkey::Status Tycoon::loadPage(tycoon::Page& page) {
    adl::recursive_mutex::guard _g {this->pageMaintenanceLock};

    if (!page.present) {
        Genode::error("Tycoon: Page not present. Cannot load.");
        return Status::INVALID_PARAMETERS;
    }

    Status status = openConnection(false, page.mnemosyneId);
    if (status != Status::SUCCESS) {
        Genode::error("Failed to open connection to mnemosyne ", page.mnemosyneId);
        return status;
    }

    adl::uintptr_t tmpAddr = MAINTENANCE_TMP_ADDR;

    env.rm().attach_at(page.buf, tmpAddr);
    status = connections.mnemosynes[page.mnemosyneId]->readBlock(
        page.blockId,
        (void*) tmpAddr,
        &page.dataVersion
    );

    if (status != Status::SUCCESS) {
        Genode::error("Something went wrong. 5c9c81ec-0c49-40df-8f34-332a5b5f43a7");
        env.rm().detach(tmpAddr);
        return status;
    }

    env.rm().detach(tmpAddr);
    
    return Status::SUCCESS;
}


monkey::Status Tycoon::updateSharedPage(tycoon::Page& page) {
    adl::recursive_mutex::guard _g {this->pageMaintenanceLock};

    if (!page.present) {
        Genode::error("Page not present. No need to update.");
        return Status::INVALID_PARAMETERS;
    }

    adl::int64_t dataVer;
    monkey::Status status = fetchPageDataVersion(page, &dataVer);
    if (status != Status::SUCCESS) {
        Genode::error("Tycoon: Failed to fetch page data version.");
        return status;
    }
    

    if (dataVer == page.dataVersion) {
        // no need to update.
        return Status::SUCCESS;
    }

    loadPage(page);

    return Status::SUCCESS;
}


monkey::Status Tycoon::start(adl::uintptr_t vaddr, adl::size_t size) {
    stop();


    static bool yrosMemoryStarted = false;

    if (yrosMemoryStarted) {
        Genode::error("Monkey Tycoon: Restart is not supported.");
        return Status::INVALID_PARAMETERS;
    }

    void* pageLinkNodes = adl::defaultAllocator.alloc<char>(2, false, 4096);
    yros::memory::MemoryManager::init(pageLinkNodes, (void*) vaddr, size);


    Genode::log(
        "Tycoon: Starting... Addr to be managed: [", 
        Genode::Hex(vaddr), ", ", Genode::Hex(vaddr + size), ") (", size, " bytes)."
    );

    Status status = openConnection(true);
    if (status != Status::SUCCESS) {
        return status;
    }

    // get memory nodes

    if ((status = connections.concierge.locateMemoryNodes(memoryNodesInfo)) != Status::SUCCESS) {
        Genode::error("Failed to locate memory nodes.");
        goto ERROR;
    }

    for (auto& it : memoryNodesInfo) {
        Genode::log(
            "Memory node ", it.id, " : ", it.ip.toString().c_str(), " ", it.port,
            " TCP", it.tcpVersion
        );
    }


    memSpace.manage = true;
    memSpace.vaddr = vaddr;
    memSpace.size = size;
    Genode::log("Tycoon: Started.");

    maintenanceThread.start();

ERROR:
    disconnectConcierge();
    return status;
}


void Tycoon::stop() {
    if (!memSpace.manage) {
        return;
    }

    Status status = sync();
    if (status != Status::SUCCESS) {
        Genode::error("Tycoon: Something went wrong syncing data.");
    }

    
    // Release buffers.

    for (auto it : pages) {
        tycoon::Page& page = it.second;
        if (!page.present)
            continue;

        // assert false: page.dirty
        
        page.present = false;
        buffers.append(page.buf);
    }

    pages.clear();


    // Close connections.

    Genode::log("Tycoon: Closing connections...");
    disconnectConcierge();
    for (auto it : connections.mnemosynes) {
        it.second->close();
        adl::defaultAllocator.free(it.second);
    }
    connections.mnemosynes.clear();


    memSpace.manage = false;

    maintenanceThread.stopAndJoin();
}



monkey::Status Tycoon::checkAvailableMem(adl::size_t* res) {
    adl::size_t sum = 0;

    for (auto& it : memoryNodesInfo) {
        Genode::log(
            "Tycoon: Checking available memory on ", 
            it.ip.toString().c_str(), ":", it.port
        );
        Status status = openConnection(false, it.id);
        if (status != Status::SUCCESS) {
            continue;
        }

        adl::size_t mem;

        Genode::log("Sending request CheckAvailMem");
        status = connections.mnemosynes[it.id]->checkAvailMem(&mem);
        if (status == Status::SUCCESS) {
            Genode::log(
                "Tycoon: Discovered ", mem, " bytes of RAM on ", 
                it.ip.toString().c_str(), ":", it.port
            );
            sum += mem;
        }
        else {
            Genode::error("Something went wrong when connecting to mnemosyne ", it.id);
        }
    }

    *res = sum;

    return Status::SUCCESS;
}

