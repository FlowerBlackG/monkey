include $(REP_DIR)/lib/mk/libc.mk

INC_DIR += $(LIBC_DIR)/include/spec/arm_64

vpath % $(REP_DIR)/src/lib/libc/spec/arm_64

CC_CXX_WARN_STRICT =
