/*
 * Memory Lounge : Serves Memory Node connection
 * 
 * gongty [at] tongji [dot] edu [dot] cn
 * Created on 2025.1.25 at Xiangzhou, Zhuhai, Guangdong
 */


#include "./MemoryLounge.h"

using namespace monkey;
using namespace net;

using net::protocol::Msg;
using net::protocol::MsgType;


monkey::Status MemoryLounge::processMemoryNodeClockIn(const monkey::net::IP4Addr& ip4Addr, adl::uint16_t port) {
    Status status = Status::SUCCESS;

    if (ip4Addr != client.ip) {
        Genode::warning("memory node's ip doesn't match.");
        Genode::warning(
            "> from protocol: ", 
            ip4Addr.toString().c_str(), 
            "; from socket: ", 
            client.ip.toString().c_str()
        );
    }

    adl::int64_t nodeId = context.genMemoryNodeId();
    
    MemoryNodeInfo info;
    info.id = nodeId;
    info.ip = ip4Addr;
    info.port = port;
    context.memoryNodes[nodeId] = info;
    
    nodeId = adl::htonq(nodeId);
    client.sendResponse(0, sizeof(adl::int64_t), &nodeId);

    return status;
}


monkey::Status MemoryLounge::processGetIdentityKeys() {
    net::Protocol1Connection::ReplyGetIdentityKeysParams params;

    auto appType = Protocol1Connection::ReplyGetIdentityKeysParams::NodeType::App;
    auto memoryType = Protocol1Connection::ReplyGetIdentityKeysParams::NodeType::MemoryNode;
    auto rc4Type = Protocol1Connection::ReplyGetIdentityKeysParams::KeyType::RC4;

    Status status = Status::SUCCESS;

    // memory node keys

    for (auto& key : context.keyrings.memoryNodes) {
        if (status != Status::SUCCESS)
            break;
    
        status = params.addKey(memoryType, rc4Type, key);
    }

    // app keys

    for (const auto& it : context.keyrings.apps) {
        if (status != Status::SUCCESS)
            break;

        status = params.addKey(appType, rc4Type, it.second, it.first);
    }


    // reply

    if (status != Status::SUCCESS) {
        client.sendResponse(1, "Something went wrong on our side.");
        Genode::error("Error replying to GetIdentityKeys. Status: ", adl::int32_t(status));
        return status;
    }
    
    return client.replyGetIdentityKeys(params);
}



monkey::Status MemoryLounge::serve() {
    Genode::log("====== Welcome client (Memory) to Sunflower Lounge ======");
    Status status = Status::SUCCESS;

    while (true) {
        Genode::log("Sunflower - Memory: Waiting for message...");
        Msg* msg = nullptr;
        status = client.recvMsg(&msg);
        if (status != Status::SUCCESS) {
            Genode::warning("Failed to get message. Leaving lounge..");
            return status;
        }

        
        switch ((MsgType) msg->header.type) {

            case MsgType::MemoryNodeClockIn: {
                Genode::log("> Message: Memory Node Clock In");
                net::IP4Addr ip;
                adl::uint8_t ipBuf[16];
                adl::int32_t tcpVer;
                adl::uint16_t port;
                status = client.decodeMemoryNodeClockIn(msg, &tcpVer, &port, ipBuf);
                
                if (status != Status::SUCCESS) {            
                    client.sendResponse(1, "Failed to decode message.");
                    break;
                }

                if (tcpVer != 4) {
                    client.sendResponse(1, "Only IPv4 supported yet.");
                    status = Status::PROTOCOL_ERROR;
                    break;
                }

                ip.i32 = * (adl::int32_t *) ipBuf;
                Genode::log("  TCP ", tcpVer, ", port ", port, ", ip ", ip.toString().c_str());
                status = processMemoryNodeClockIn(ip, port);
                break;
            }

            case MsgType::GetIdentityKeys: {
                Genode::log("> Message: Get Identity Keys");
                status = processGetIdentityKeys();
                break;
            }

            default: {
                Genode::warning("> Message Type NOT SUPPORTED");
                status = Status::PROTOCOL_ERROR;
                client.sendResponse(1, "Msg Type not supported.");
                break;
            }
        }


        adl::defaultAllocator.free(msg);
        if (status != Status::SUCCESS)
            break;
    }

    return status;
}
