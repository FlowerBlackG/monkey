/*
 * Monkey Concierge : Remembers where all nodes are.
 *
 * Created on 2025.1.6 at Minhang, Shanghai
 * 
 * 
 * gongty  [at] tongji [dot] edu [dot] cn
 * feng.yt [at]  sjtu  [dot] edu [dot] cn
 * 
 */


#include "./main.h"
#include "./AppLounge.h"
#include "./MemoryLounge.h"


#include <adl/stdint.h>
#include <adl/config.h>
#include <adl/collections/HashMap.hpp>
#include <base/component.h>
#include <libc/component.h>
#include <base/heap.h>
#include <base/attached_rom_dataspace.h>

#include <monkey/Status.h>

#include <monkey/net/TcpIo.h>
#include <monkey/net/protocol.h>
#include <monkey/net/IP4Addr.h>
#include <unistd.h>
#include <sys/socket.h>
#include <sys/types.h>
#include <arpa/inet.h>
#include <sys/stat.h>
#include <sys/select.h>
#include <netinet/in.h>


using namespace monkey;

using HelloMode = net::ProtocolConnection::HelloMode;


void ConciergeMain::initAdlAlloc() {
    adl::defaultAllocator.init({
        .alloc = [] (adl::size_t size, void* data) {
            return reinterpret_cast<Genode::Heap*>(data)->alloc(size);
        },
        
        .free = [] (void* addr, adl::size_t size, void* data) {
            reinterpret_cast<Genode::Heap*>(data)->free(addr, size);
        },
        
        .data = &heap
    });
}


Status ConciergeMain::loadConfig() {
    Genode::Attached_rom_dataspace configDs { env, "config" };
    auto config = configDs.xml();
    auto conciergeNode = config.sub_node("monkey-concierge");
    auto serverNode = conciergeNode.sub_node("server");
    auto portNode = serverNode.sub_node("port");
    {
        adl::ByteArray portByteArr;

        if (!portByteArr.resize(portNode.content_size())) {
            return Status::OUT_OF_RESOURCE;
        }
        
        portNode.decoded_content((char*) portByteArr.data(), portByteArr.size());
        port = 0;
        
        for (auto ch : portByteArr) {
            
            if (ch >= '0' && ch <= '9') {
                port *= 10;
                port += uint16_t(ch - '0');
            }
            else {
                
                return Status::INVALID_PARAMETERS;
            }
        }
    }
    

    // load memory nodes' keys

    conciergeNode.for_each_sub_node("memory-node", [&] (const Genode::Xml_node& node) {
        auto keyNode = node.sub_node("key");
        adl::ByteArray arr;

        keyNode.with_raw_content([&] (const char* content, size_t contentSize) {
            if (!arr.resize(contentSize)) {
                throw Status::OUT_OF_RESOURCE;  // bad way.. but.. anyway..
            }

            adl::memcpy(arr.data(), content, contentSize);
        });
        
        this->keyrings.memoryNodes.append(arr);
    });
    

    // load apps' keys

    conciergeNode.for_each_sub_node("app", [&] (const Genode::Xml_node& node) {
        auto keyNode = node.sub_node("key");
        auto idNode = node.sub_node("id");
        adl::ByteArray key;
        adl::TString idStr;
        
        keyNode.with_raw_content([&] (const char* content, size_t contentSize) {
            if (!key.resize(contentSize)) {
                throw Status::OUT_OF_RESOURCE;  // bad way.. but.. anyway..
            }

            adl::memcpy(key.data(), content, contentSize);
        });

        idNode.with_raw_content([&] (const char* content, size_t contentSize) {
            for (adl::size_t idx = 0; idx < contentSize; idx++) {
                idStr += content[idx];
            }
        });

        this->keyrings.apps[idStr.toInt64()] = key;
    });

    return Status::SUCCESS;
}


Status ConciergeMain::init() {
    initAdlAlloc();
    Status status = Status::SUCCESS;
    
    try {
        status = loadConfig();
        if (status == Status::SUCCESS) {
            Genode::log("Config loaded.");
            Genode::log("> port: ", port);
            for (auto& it : keyrings.memoryNodes) {
                Genode::log("> memory node key: ", it.toString().c_str());
            }

            for (const auto& it : keyrings.apps)
                Genode::log("> client  app key: ", it.first, " -> ", it.second.toString().c_str());
        }
    }
    catch (...) {
        Genode::error("Failed on loading config. Check your .run file.");
        status = Status::INVALID_PARAMETERS;
    }

    return status;
}


void ConciergeMain::serveClient(net::Socket4& conn) {
    Genode::log("Client connected: ", conn.ip.toString().c_str(), " [", conn.port, "]");

    net::Protocol2Connection client;
    client.socketFd = conn.socketFd;
    client.port = conn.port;
    client.ip = conn.ip;


    // Determine protocol version. Allow only latest version for convenience.

    if (client.hello(net::protocol::LATEST_VERSION, HelloMode::SERVER) != Status::SUCCESS) {
        return;
    }
    Genode::log("> Using protocol Version: ", net::protocol::LATEST_VERSION);


    // Auth
    if (client.auth(keyrings.apps, keyrings.memoryNodes) != Status::SUCCESS) {
        return;
    }
    adl::TString nodeTypeStr;
    switch (client.nodeType) {
        case net::Protocol1Connection::NodeType::MemoryNode:
            nodeTypeStr = "MemoryNode";
            break;
        case net::Protocol1Connection::NodeType::Other:
            nodeTypeStr = "Other";
            break;
        case net::Protocol1Connection::NodeType::App:
            nodeTypeStr = "App";
            break;
        default:
            nodeTypeStr = "Unknown";
            break;
    }
    Genode::log("> Client Type: ", nodeTypeStr.c_str());
    
    if (client.nodeType == net::Protocol1Connection::NodeType::App) {
        AppLounge lounge {*this, client};
        Status status = lounge.serve();
        Genode::log("App left with status: ", adl::int32_t(status));
    }
    else if (client.nodeType == net::Protocol1Connection::NodeType::MemoryNode) {
        MemoryLounge lounge {*this, client};
        Status status = lounge.serve();
        Genode::log("Memory left with status: ", adl::int32_t(status));
    }
}


Status ConciergeMain::run() {
    server.port = this->port;
    Status status = server.start();
    if (status != Status::SUCCESS) {
        return status;
    }

    Genode::log("Server started on port: ", port);

    while (true) {
        auto client = server.accept(true);
        serveClient(client);
        client.close();
        Genode::log("Closed: ", client.ip.toString().c_str(), " [", client.port, "]");
    }

    server.close();
    return status;
}


void ConciergeMain::cleanup() {

}


ConciergeMain::ConciergeMain(Genode::Env& env) : env {env} {

    Libc::with_libc([&] () {

        Genode::log("Hello :D This is Monkey Concierge");
        
        Status status = init();
        if (status != monkey::Status::SUCCESS) {
            Genode::log("Something went wrong on init.. (", adl::int32_t(status), ")");
            goto END;
        }

        status = run();
        if (status != monkey::Status::SUCCESS) {
            Genode::log("Something went wrong when running.. (", adl::int32_t(status), ")");
        }
        
END:
        cleanup();        
        Genode::log("Bye :D");
    });
}



void Libc::Component::construct(Libc::Env& env) {
    static ConciergeMain main {env};
}
