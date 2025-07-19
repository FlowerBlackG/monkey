/*

    config file utils.

    created on 2025.2.4 at Xiangzhou, Zhuhai, Guangdong

    gongty [at] tongji [dot] edu [dot] cn

*/


#include <monkey/genodeutils/config.h>
#include <monkey/Status.h>
#include <adl/collections/ArrayList.hpp>

namespace monkey::genodeutils::config {

adl::TString getText(const Genode::Xml_node& node) {
    adl::ByteArray buf;
    node.with_raw_content([&] (const char* raw, adl::size_t size) {
        if (!buf.resize(size))
            throw Status::OUT_OF_RESOURCE;
        adl::memcpy(buf.data(), raw, size);
    });

    return buf.toString();
}


}
