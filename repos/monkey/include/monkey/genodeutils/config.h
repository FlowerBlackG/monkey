/*

    config file utils.

    created on 2025.2.4 at Xiangzhou, Zhuhai, Guangdong

    gongty [at] tongji [dot] edu [dot] cn

*/

#pragma once

#include <base/attached_rom_dataspace.h>

#include <adl/TString.h>

namespace monkey::genodeutils::config {


/**
 * 
 * You should ensure adl allocator has been initialized before using this method.
 *
 * @throw monkey::Status::OUT_OF_RESOURCE  
 */
adl::TString getText(const Genode::Xml_node&);


}

