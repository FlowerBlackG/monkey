/*
 * Monkey Net :: TcpIo
 *
 * Created on 2025.1.21 at Xiangzhou, Zhuhai, Guangdong
 * 
 * 
 * gongty  [at] tongji [dot] edu [dot] cn
 * 
 */


#include <base/log.h>
#include <monkey/net/TcpIo.h>

namespace monkey::net {


void PromisedSocketIo::close() {
    if (valid()) {
        ::close(socketFd);
        socketFd = -1;
    }
}


adl::int64_t PromisedSocketIo::recv(void* buf, adl::size_t len) {
    adl::size_t sum = 0;
    while (sum < len) {
        adl::int64_t curr = read(socketFd, ((char*) buf) + sum, len - sum);

        if (curr <= 0) {
            return -1;
        }

        sum += size_t(curr);
    }
    
    return adl::int64_t(sum);
}

adl::int64_t PromisedSocketIo::send(const void* buf, adl::size_t len) {
    return ::write(socketFd, buf, len);
}


}
