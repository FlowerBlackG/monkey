/*
 * App Lounge : Serves App connection
 * 
 * gongty [at] tongji [dot] edu [dot] cn
 * Created on 2025.1.25 at Xiangzhou, Zhuhai, Guangdong
 */


#include "./AppLounge.h"

using namespace monkey;

using net::protocol::Msg;
using net::protocol::MsgType;

using LocateMemoryNodeNodeInfoEntry = net::Protocol1Connection::LocateMemoryNodeNodeInfoEntry;

Status AppLounge::processLocateMemoryNodes() {
    Status status = Status::SUCCESS;

    adl::ArrayList<LocateMemoryNodeNodeInfoEntry> payload;
    for (const auto& it : context.memoryNodes) {
        LocateMemoryNodeNodeInfoEntry info;
        info.id = adl::htonq(it.second.id);
        info.tcpVersion = adl::htonl(4);
        info.port = adl::htonl(it.second.port);
        info.inet4addr = it.second.ip.ui32;
        payload.append(info);
    }


    status = client.sendResponse(0, payload.size() * sizeof(LocateMemoryNodeNodeInfoEntry), payload.data());

    return status;
}


Status AppLounge::serve() {

    Genode::log("====== Welcome client (APP) to Sunflower Lounge ======");

    Status status = Status::SUCCESS;


    while (true) {
        Msg* msg = nullptr;
        status = client.recvMsg(&msg);
        if (status != Status::SUCCESS)
            return status;


        switch ((MsgType) msg->header.type) {
            case MsgType::LocateMemoryNodes: {
                Genode::log("> Message: Locate Memory Nodes");
                status = processLocateMemoryNodes();
                break;
            }
            default: {
                Genode::warning("> Message Type NOT SUPPORTED");
                status = Status::PROTOCOL_ERROR;
                client.sendResponse(1, "Msg Type not supported.");
                break;
            }
        }


        client.freeMsg(msg);
        if (status != Status::SUCCESS)
            break;
    }

    return status;
}
