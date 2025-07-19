/*
 * Monkey Mnemosyne : Memory provider.
 *
 * Created on 2025.2.4 at Xiangzhou, Zhuhai, Guangdong
 * 
 * 
 * gongty  [at] tongji [dot] edu [dot] cn
 * 
 */


#include "./main.h"

#include <adl/sys/types.h>
#include <libc/component.h>
#include <base/mutex.h>
#include <base/attached_rom_dataspace.h>
#include <base/thread.h>

#include <util/construct_at.h>

#include <monkey/Status.h>

#include <monkey/net/protocol.h>

#include <monkey/genodeutils/config.h>

#include "./AppLounge.h"

using namespace monkey;


using HelloMode = net::ProtocolConnection::HelloMode;


static const adl::TString memSizeToHumanReadable(adl::size_t size) {
    const char* levels[] = {
        " B", " KB", " MB", " GB", " TB"
    };

    for (adl::size_t i = 0; i < sizeof(levels) / sizeof(levels[0]); i++) {
        if (size < 8192)
            return adl::TString::to_string(size) + levels[i];
        size /= 1024;
    }

    return "+oo";  // Treat as infinity.
}


static void initAdlAlloc(Genode::Heap& heap) {

    static struct {
        Genode::Heap* heap;
        Genode::Mutex lock;
    } adlAllocData;

    adlAllocData.heap = &heap;

    adl::defaultAllocator.init({
        .alloc = [] (adl::size_t size, void* data) {
            auto* allocData = (decltype(adlAllocData)*) data;
            Genode::Mutex::Guard _g {allocData->lock};
            return allocData->heap->alloc(size);
        },
        
        .free = [] (void* addr, adl::size_t size, void* data) {
            auto* allocData = (decltype(adlAllocData)*) data;
            Genode::Mutex::Guard _g {allocData->lock};
            allocData->heap->free(addr, size);
        },
        
        .data = &adlAllocData
    });
}


Block MnemosyneMain::allocMemoryBlock(adl::size_t size, adl::int64_t owner, bool record) {
    if (size == 0) {
        return {};
    }

    Genode::Mutex::Guard _g { memoryBlockAllocationLock };

    adl::size_t finalSize = (size + 4095) & ~0xFFFul;

    if (env.pd().avail_ram().value < MONKEY_MNEMOSYNE_HEAP_MEMORY_RESERVED + finalSize) {
        return {};
    }

    Block b;
    b.data = (char*) heap.alloc(finalSize);
    if (!b.data) {
        return {};
    }

    b.id = nextMemoryBlockId++;
    b.owner = owner;
    b.size = finalSize;

    Genode::log("Allocated block for ", owner, ". Block id: ", b.id, ", size ", b.size);

    if (record) {
        this->memoryBlocks[b.id] = b;
    }

    return b;
}


void MnemosyneMain::freeMemoryBlock(Block& b) {
    Genode::Mutex::Guard _g { memoryBlockAllocationLock };

    this->memoryBlocks.removeKey(b.id, true);

    Genode::log("Released block for ", b.owner, ". Block id: ", b.id, ", size ", b.size);
    
    heap.free(b.data, b.size);
}


Status MnemosyneMain::loadConfig() {
    Genode::Attached_rom_dataspace configDs { env, "config" };
    auto configRoot = configDs.xml().sub_node("monkey-mnemosyne");
    auto conciergeNode = configRoot.sub_node("concierge");
    auto mnemosyneNode = configRoot.sub_node("mnemosyne");

    Status status = Status::SUCCESS;

#define IF_STATUS_NOT_SUCCESS_THEN_RETURN() do { if (status != Status::SUCCESS) return status; } while (0)

    // concierge
    {
        auto ipNode = conciergeNode.sub_node("ip");
        status = config.concierge.ip.set(genodeutils::config::getText(ipNode));
        IF_STATUS_NOT_SUCCESS_THEN_RETURN();

        auto portNode = conciergeNode.sub_node("port");
        config.concierge.port = (adl::uint16_t) genodeutils::config::getText(portNode).toInt64();

    }

    // mnemosyne
    {
        auto ipNode = mnemosyneNode.sub_node("ip");
        status = config.mnemosyne.ip.set(genodeutils::config::getText(ipNode));
        IF_STATUS_NOT_SUCCESS_THEN_RETURN();
        
        auto portNode = mnemosyneNode.sub_node("port");
        config.mnemosyne.port = (adl::uint16_t) genodeutils::config::getText(portNode).toInt64();
        
        auto listenPortNode = mnemosyneNode.sub_node("listen-port");
        config.mnemosyne.listenPort = (adl::uint16_t) genodeutils::config::getText(listenPortNode).toInt64();

        auto keyNode = mnemosyneNode.sub_node("key");
        config.mnemosyne.key = genodeutils::config::getText(keyNode);
    }


#undef IF_STATUS_NOT_SUCCESS_THEN_RETURN

    Genode::log("--- mnemosyne config begin ---");
    Genode::log("> mnemosyne ip    : ", config.mnemosyne.ip.toString().c_str());
    Genode::log("> mnemosyne port  : ", config.mnemosyne.port);
    Genode::log("> mnemosyne listen: ", config.mnemosyne.listenPort);
    Genode::log("> concierge ip    : ", config.concierge.ip.toString().c_str());
    Genode::log("> concierge port  : ", config.concierge.port);
    Genode::log("---  mnemosyne config end  ---");
    
    return status;
}


Status MnemosyneMain::init() {
    initAdlAlloc(heap);
    Status status = Status::SUCCESS;

    try {
        status = loadConfig();
    } 
    catch (...) {
        Genode::error("Exception on loading config. Check your .run file.");
        status = Status::INVALID_PARAMETERS;
    }

    if (status != Status::SUCCESS)
        return status;
    
    return status;
}


Status MnemosyneMain::clockIn() {
    Genode::log("Trying to clock in.");

    net::protocol::Response* response = nullptr;

    net::Protocol2Connection client;
    client.ip = config.concierge.ip;
    client.port = config.concierge.port;
    
    while (client.connect() != Status::SUCCESS) {
        Genode::error("Failed to connect with server.");
        Genode::error("> Retrying...");
    }

    Status status;

    // Open connection using latest protocol.

    if ((status = client.hello(net::protocol::LATEST_VERSION, HelloMode::CLIENT)) != Status::SUCCESS) {
        Genode::error("(Clock In) Failed on [Hello].");
        goto END;
    }
    
    if ((status = client.auth(config.mnemosyne.key)) != Status::SUCCESS) {
        Genode::error("(Clock In) Failed on auth.");
        goto END;
    }
    

    // Clock in.
    
    adl::int64_t id;
    status = client.memoryNodeClockIn(&id, config.mnemosyne.ip, config.mnemosyne.port);
    if (status != Status::SUCCESS) {
        Genode::error("(Clock In) Failed on doing Memory Node Clock In.");
        goto END;
    }
    Genode::log("(Clock In) Node ID is: ", id);
    this->nodeId = id;

    // Get identity keys.

    if ((status = client.sendGetIdentityKeys()) != Status::SUCCESS) 
        goto END;

    if ((status = client.recvResponse(&response)) != Status::SUCCESS) 
        goto END;

    if (response->code != 0) {
        adl::ByteArray bMsg {response->msg, response->msgLen};
        Genode::error(bMsg.toString().c_str());
        goto END;
    }

    
    // Appreciate identity keys.

    {
        struct {
            MnemosyneMain* main;
        } data;

        data.main = this;

        status = client.appreciateGetIdentityKeys(
            *response, 
            &data,
            [] (
                net::Protocol1Connection::ReplyGetIdentityKeysParams::NodeType nodeType, 
                net::Protocol1Connection::ReplyGetIdentityKeysParams::KeyType keyType, 
                const adl::ByteArray& key, 
                adl::int64_t id, 
                void* rawData
            ) {

                const auto AppType = net::Protocol1Connection::ReplyGetIdentityKeysParams::NodeType::App;
                const auto RC4Type = net::Protocol1Connection::ReplyGetIdentityKeysParams::KeyType::RC4;
                
                auto typedData = (decltype(data) *) rawData;
                
                // We don't care about memory nodes' keys. We can't deal algorithms other than RC4.

                if (nodeType != AppType || keyType != RC4Type) {
                    Genode::log(
                        "Key ignored: (", 
                        adl::int8_t(nodeType), 
                        ", ", 
                        adl::int8_t(keyType), 
                        ") ", 
                        key.toString().c_str()
                    );
                    return;
                }
                
                typedData->main->appKeys[id] = key;
                Genode::log("From Concierge: App key: [", id, "] [", key.toString().c_str(), "]");
            }
            
        );

        if (status != Status::SUCCESS) {
            Genode::error("Error on loading identity keys from Concierge.");
            goto END;
        }
    }


    // Cleanup.

END:
    client.close();
    if (response) {
        adl::defaultAllocator.free(response);
        response = nullptr;
    }
    return status;
}



class AppLoungeThread : public Genode::Thread {
public:
    typedef void (*OnExitCall) (AppLounge&, Status);

    AppLounge lounge;
    OnExitCall onExit;

    AppLoungeThread(
        MnemosyneMain& context, 
        net::Protocol1Connection& client,
        OnExitCall onExit
    )
    : 
    Genode::Thread(context.env, "App lounge thread", 16 * 1024),
    lounge(context, client),
    onExit(onExit)
    {}

    virtual void entry() override {
        Status status = lounge.serve();
        onExit(lounge, status);
    }
};


void MnemosyneMain::serveClient(net::Socket4& conn) {
    Genode::log("Client connected: ", conn.ip.toString().c_str(), " [", conn.port, "]");

    Status status;

    net::Protocol1Connection client;
    client.socketFd = conn.socketFd;
    client.ip = conn.ip;
    client.port = conn.port;

    // Force use protocol v1.
    if (client.hello(1, HelloMode::SERVER) != Status::SUCCESS)
        return;
    Genode::log("> Using protocol version 1.");

    // Auth

    if ((status = client.auth(&appKeys, nullptr)) != Status::SUCCESS) {
        Genode::error("Failed on auth. Status: ", adl::int32_t(status));
        return;
    }

    if (client.nodeType != net::Protocol1Connection::NodeType::App) {
        Genode::error("Client is not App node!");
        Genode::error("> Connection between memory nodes is not supported yet.");
        return;
    }

    // Now, we can say client is an authenticated App node.

    // Enter lounge.

    if (client.nodeType == net::Protocol1Connection::NodeType::App)    
    {
        if (loungeThreads.hasKey(client.nodeId)) {
            Genode::error("Node (id: ", client.nodeId, ") already serving. Refuse to serve another.");
            return;
        }

        auto thread = adl::defaultAllocator.allocNoConstruct<AppLoungeThread>(1);
        if (thread == nullptr) {
            Genode::error("Failed to create lounge thread. Out of memory.");
            return;
        }


        Genode::construct_at<AppLoungeThread>(
            thread, 
            
            *this, 
            client, 
            [] (AppLounge& lounge, Status status) {
                Genode::log("App (id: ", lounge.client.nodeId, ") left with status: ", adl::int32_t(status));
                auto thread = lounge.context.loungeThreads[lounge.client.nodeId];
                lounge.context.loungeThreads.removeKey(lounge.client.nodeId);
                lounge.client.close();
                Genode::log("Closed: ", lounge.client.ip.toString().c_str(), " [", lounge.client.port, "]");

                Genode::Mutex::Guard _g {lounge.context.threadRecycleBin.lock};
                lounge.context.threadRecycleBin.bin.append(thread);
            }
        );

        loungeThreads[client.nodeId] = thread;
        thread->start();
    }
   
}


Status MnemosyneMain::runServer() {
    Genode::log("Starting server..");

    net::Socket4 server;
    server.port = config.mnemosyne.listenPort;
    server.ip = INADDR_ANY;

    Status status = server.start();
    if (status != Status::SUCCESS) {
        return status;
    }


    while (true) {
        auto client = server.accept(true);
        serveClient(client);

        {
            Genode::Mutex::Guard _g {threadRecycleBin.lock};
            for (auto it : threadRecycleBin.bin) {
                adl::defaultAllocator.free(it);
            }
            threadRecycleBin.bin.clear();
        }
    }

    server.close();
    return Status::SUCCESS;
} 



Status MnemosyneMain::run() {
    Status status = clockIn();
    if (status != Status::SUCCESS) {
        Genode::error("Something went wrong during clock-in. Status: ", adl::int32_t(status));
        return status;
    }

    status = runServer();

    return status;
}


void MnemosyneMain::cleanup() {
    
}


MnemosyneMain::MnemosyneMain(Genode::Env& env) : env {env} {
    
    Genode::log("Welcome to Monkey Mnemosyne.");

    Libc::with_libc([&] () {

        Status status = init();
        if (status != Status::SUCCESS) {
            Genode::error("Failed on init.");
            cleanup();
            return;
        }

        
        adl::size_t ramAvai = env.pd().avail_ram().value;
        adl::size_t ramTotal = env.pd().ram_quota().value;
        Genode::log(
            "Ram Total: ", memSizeToHumanReadable(ramTotal).c_str(),
            ", Ram Avai: ", memSizeToHumanReadable(ramAvai).c_str()
        );


        status = run();
        if (status != Status::SUCCESS) {
            Genode::warning("Something went wrong on runtime.");
        }


        cleanup();
    });

    Genode::log("Bye :D");
}


void Libc::Component::construct(Libc::Env& env) {
    static MnemosyneMain main {env}; 
}
