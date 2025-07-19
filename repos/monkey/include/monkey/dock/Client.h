/*
    Monkey Dock

    gongty [at] tongji [dot] edu [dot] cn
    created on 2025.2.25 at Wujing, Minhang

*/

#pragma once

#include <adl/sys/types.h>
#include <base/rpc_client.h>
#include <base/log.h>
#include <base/heap.h>
#include <monkey/dock/Session.h>

#include <adl/config.h>
#include <adl/string.h>

namespace monkey::dock {

struct Client : Genode::Rpc_client<Session>
{
protected:
    Genode::Env& env;
    Genode::Heap heap { env.ram(), env.rm() };

    struct {
        Genode::Ram_dataspace_capability ds;
        bool mapped = false;
        adl::uintptr_t vaddr = 0;
        adl::size_t size = 0;
    } buffer;

public:

    Client(
        Genode::Capability<Session> cap,
        Genode::Env& env
    )
    :
    Genode::Rpc_client<Session>(cap),
    env(env)
    {
        if (adl::defaultAllocator.notReady()) {
            adl::defaultAllocator.init({

                .alloc = [] (adl::size_t size, void* data) {
                    return reinterpret_cast<Genode::Sliced_heap*>(data)->alloc(size);
                },
                
                .free = [] (void* addr, adl::size_t size, void* data) {
                    reinterpret_cast<Genode::Sliced_heap*>(data)->free(addr, size);
                },
                
                .data = &heap
            });

        }
    }


    ~Client() {
        if (buffer.mapped) {
            env.rm().detach(buffer.vaddr);
            buffer.mapped = false;
        }
    }


    inline virtual int socket() override {
        return call<Rpc_socket>();
    }


    inline virtual void close(int socketFd) override {
        call<Rpc_close>(socketFd);
    }


    inline virtual int connect(int socketFd, net::IP4Addr ip, adl::uint16_t port) override {
        return call<Rpc_connect>(socketFd, ip, port);
    }



    inline virtual monkey::Status makeBuffer(adl::size_t nPages) override {
        Status status = call<Rpc_makeBuffer>(nPages);
        if (status == Status::SUCCESS) {
            buffer.size = nPages * 4096;
        }
        return status;
    }


    inline virtual Genode::Ram_dataspace_capability getBuffer() override {
        buffer.ds = call<Rpc_getBuffer>();
        return buffer.ds;
    }


    inline virtual adl::int64_t send(int socketFd, adl::size_t len) override {
        return -1;
    }


    inline adl::int64_t send(int socketFd, const void* data, adl::size_t len) {
        auto pData = (const char*) data;
        adl::size_t sum = 0;
        
        adl::size_t onceMax = len;
        if (onceMax > buffer.size) {
            onceMax = buffer.size;
        }

        while (sum < len) {
            if (onceMax > len - sum) {
                onceMax = len - sum;
            }

            adl::memcpy((void*) buffer.vaddr, pData + sum, onceMax);
            adl::int64_t sent = call<Rpc_send>(socketFd, onceMax);
            if (sent < 0) {
                return adl::int64_t(sum);
            }
            sum += adl::size_t(sent);
        }

        return adl::int64_t(sum);
    }


    inline virtual adl::int64_t recv(int socketFd, adl::size_t len) override {
        return -1;
    }


    inline adl::int64_t recv(int socketFd, void* data, adl::size_t len) {
        auto pData = (char*) data;

        adl::size_t sum = 0;
        
        adl::size_t onceMax = len;
        if (onceMax > buffer.size) {
            onceMax = buffer.size;
        }

        while (sum < len) {
            if (onceMax > len - sum) {
                onceMax = len - sum;
            }

            adl::int64_t received = call<Rpc_recv>(socketFd, onceMax);
            if (received < 0) {
                return adl::int64_t(sum);
            }

            adl::memcpy(pData + sum, (void*) buffer.vaddr, adl::size_t(received));
            sum += adl::size_t(received);
        }

        return adl::int64_t(sum);
    }


    inline monkey::Status mapBuffer(adl::uintptr_t vaddr, adl::size_t size) {
        if (size < buffer.size) {
            return Status::INVALID_PARAMETERS;
        }
        else if (size > buffer.size) {
            size = buffer.size;
        }

        if (buffer.mapped) {
            env.rm().detach(buffer.vaddr);
            buffer.mapped = false;
        }

        buffer.vaddr = vaddr;
        env.rm().attach_at(buffer.ds, vaddr);
        buffer.mapped = true;
        return Status::SUCCESS;
    }


};

}
