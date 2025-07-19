# Application-layer network support for monkey.
#
# Created on 2025.1.5
# gongty [at] tongji [dot] edu [dot] cn
#

INC_DIR += $(REP_DIR)/include 
vpath %.c $(REP_DIR)/src/lib/net
vpath %.cc $(REP_DIR)/src/lib/net
vpath %.cpp $(REP_DIR)/src/lib/net

SRC_CC += protocol/defines.cc IP4Addr.cc TcpIo.cc Socket4.cc

SRC_CC += protocol/ProtocolConnection.cc 
SRC_CC += protocol/Protocol1Connection.cc 
SRC_CC += protocol/Protocol2Connection.cc 

LIBS = base adl monkey_crypto libc vfs
