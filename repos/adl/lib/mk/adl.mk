

INC_DIR += $(REP_DIR)/src/include
vpath %.c $(REP_DIR)/src/lib
vpath %.cc $(REP_DIR)/src/lib
vpath %.cpp $(REP_DIR)/src/lib

SRC_CC += adl/config.cpp adl/string.cpp adl/TString.cc adl/collections/LinkedList.cpp adl/wl_list.cpp
