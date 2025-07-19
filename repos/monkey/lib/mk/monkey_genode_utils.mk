# Monkey Genode utils.
#
# Created on 2025.1.14
# gongty [at] tongji [dot] edu [dot] cn
#



INC_DIR += $(REP_DIR)/include 
vpath %.c $(REP_DIR)/src/lib/genodeutils
vpath %.cc $(REP_DIR)/src/lib/genodeutils
vpath %.cpp $(REP_DIR)/src/lib/genodeutils

SRC_CC += memory.cc config.cc

LIBS = base adl 

