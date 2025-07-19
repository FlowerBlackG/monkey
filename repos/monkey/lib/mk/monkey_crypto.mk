# Crypto support for monkey.
#
# Created on 2025.1.6
# gongty [at] tongji [dot] edu [dot] cn
#

INC_DIR += $(REP_DIR)/include 
vpath %.c $(REP_DIR)/src/lib/crypto
vpath %.cc $(REP_DIR)/src/lib/crypto
vpath %.cpp $(REP_DIR)/src/lib/crypto

SRC_CC += rc4.cc

LIBS = base adl
