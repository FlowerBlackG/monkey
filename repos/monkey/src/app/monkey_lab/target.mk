TARGET = monkey_lab
SRC_CC = main.cc
LIBS   = libc vfs base adl monkey_net monkey_crypto monkey_genode_utils monkey_tycoon

ifeq ($(KERNEL), hw)
  CC_OPT += -DGENODE_HW
else ifeq ($(KERNEL), nova)
  CC_OPT += -DGENODE_NOVA
else ifeq ($(KERNEL), sel4)
  CC_OPT += -DGENODE_SEL4
else
  CC_OPT += -DGENODE_UNKNOWN
endif

export CC_OPT
