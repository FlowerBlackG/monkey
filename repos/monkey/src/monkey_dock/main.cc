/*
    Monkey Dock

    gongty [at] tongji [dot] edu [dot] cn
    created on 2025.2.24 at Wujing, Minhang

*/


#include <adl/arpa/inet.h>
#include <adl/sys/types.h>
#include <adl/config.h>

#include <sys/socket.h>
#include <netinet/in.h>
#include <unistd.h>

#include <libc/component.h>
#include <base/heap.h>
#include <base/rpc_server.h>
#include <base/attached_ram_dataspace.h>
#include <root/component.h>

#include <monkey/dock/Session.h>

using namespace monkey;


struct DockSessionComponent : Genode::Rpc_object<dock::Session>
{
protected:
    Genode::Env& env;

    Genode::Attached_ram_dataspace bufferDs;


public:
    DockSessionComponent(
        Genode::Env& env
    )
    :
    env(env),
    bufferDs(env.ram(), env.rm(), 0)
    {
        Genode::log("Dock Session created.");
    }


    virtual int socket() override {
        return Libc::with_libc([&] () {
            return ::socket(AF_INET, SOCK_STREAM, 0);
        });
    }


    virtual void close(int socketFd) override {
        Libc::with_libc([&] () {
            ::close(socketFd);
        });
    }


    virtual int connect(int socketFd, net::IP4Addr ip, adl::uint16_t port) override {
        return Libc::with_libc([&] () {

            sockaddr_in addr;
            addr.sin_family = AF_INET;
            addr.sin_port = adl::htons(port);
            addr.sin_addr.s_addr = ip.ui32;

            return ::connect(socketFd, (struct sockaddr*)&addr, sizeof(addr));
        });
    }



    virtual monkey::Status makeBuffer(adl::size_t nPages) override {
        if (nPages == 0)
            return monkey::Status::INVALID_PARAMETERS;

        bufferDs.realloc(&env.ram(), nPages * 4096);
        return monkey::Status::SUCCESS;
    }


    virtual Genode::Ram_dataspace_capability getBuffer() override {
        return bufferDs.cap();
    }

    
    virtual adl::int64_t send(int socketFd, adl::size_t len) override {
        return Libc::with_libc([&] () {
            return ::write(socketFd, bufferDs.local_addr<void>(), len);
        });
    }


    virtual adl::int64_t recv(int socketFd, adl::size_t len) override {
        return Libc::with_libc([&] () {
            return ::read(socketFd, bufferDs.local_addr<char>(), Genode::min(len, bufferDs.size()));
        });
    }


    void cleanup() {

    }


    ~DockSessionComponent() {
        Libc::with_libc([&] () {
            cleanup();
        });
    }
};


class DockRootComponent : public Genode::Root_component<DockSessionComponent> {
protected:
    Genode::Env& env;


    DockSessionComponent* _create_session(const char*) override
    {
        Genode::log("Creating session..");
        
        return new (md_alloc()) DockSessionComponent(env);
    }


    virtual void _destroy_session(DockSessionComponent* session) override
    {
        Genode::Root_component<DockSessionComponent>::_destroy_session(session);
    }


public:
    DockRootComponent(
        Genode::Entrypoint& ep,
        Genode::Allocator& alloc,
        Genode::Env& env
    )
    :
    Genode::Root_component<DockSessionComponent>(ep, alloc),
    env(env)
    {
        Genode::log("Dock root component ready.");
    }
    

};


struct DockMain {
    Genode::Env& env;
    Genode::Heap heap { env.ram(), env.rm() };
    
    DockRootComponent root { env.ep(), heap, env };

    DockMain(Genode::Env& env)
    :
    env(env)
    {
        adl::defaultAllocator.init({
            .alloc = [] (adl::size_t size, void* data) {
                return reinterpret_cast<Genode::Sliced_heap*>(data)->alloc(size);
            },
            
            .free = [] (void* addr, adl::size_t size, void* data) {
                reinterpret_cast<Genode::Sliced_heap*>(data)->free(addr, size);
            },
            
            .data = &heap
        });

        env.parent().announce(env.ep().manage(root));
        Genode::log("Dock server announced.");
    }

};


void Libc::Component::construct(Libc::Env& env) {
    static DockMain main {env};
}
