GEN_CORE_DIR := $(BASE_DIR)/src/core
LIBS         := syscall-linux
BOARD        ?= unknown

SRC_CC       := main.cc \
                platform.cc \
                platform_thread.cc \
                platform_services.cc \
                pd_session_component.cc \
                ram_dataspace_support.cc \
                rom_session_component.cc \
                cpu_session_component.cc \
                cpu_session_support.cc \
                cpu_thread_component.cc \
                pd_session_support.cc \
                dataspace_component.cc \
                native_pd_component.cc \
                native_cpu_component.cc \
                capability_space.cc \
                ram_dataspace_factory.cc \
                core_rpc_cap_alloc.cc \
                io_mem_session_component.cc \
                irq_session_component.cc \
                signal_source_component.cc \
                signal_transmitter_proxy.cc \
                trace_session_component.cc \
                thread_linux.cc \
                stack_area.cc \
                core_log.cc \
                core_log_out.cc \
                default_log.cc \
                heartbeat.cc \
                thread.cc \
                thread_myself.cc

INC_DIR      += $(REP_DIR)/src/core/include \
                $(GEN_CORE_DIR)/include \
                $(REP_DIR)/src/platform \
                $(REP_DIR)/src/include \
                $(BASE_DIR)/src/include

vpath main.cc                      $(GEN_CORE_DIR)
vpath pd_session_component.cc      $(GEN_CORE_DIR)
vpath core_log.cc                  $(GEN_CORE_DIR)
vpath cpu_session_component.cc     $(GEN_CORE_DIR)
vpath cpu_session_support.cc       $(GEN_CORE_DIR)
vpath cpu_thread_component.cc      $(GEN_CORE_DIR)
vpath pd_upgrade_ram_quota.cc      $(GEN_CORE_DIR)
vpath pd_session_support.cc        $(GEN_CORE_DIR)
vpath capability_space.cc          $(GEN_CORE_DIR)
vpath ram_dataspace_factory.cc     $(GEN_CORE_DIR)
vpath signal_source_component.cc   $(GEN_CORE_DIR)
vpath signal_transmitter_proxy.cc  $(GEN_CORE_DIR)
vpath trace_session_component.cc   $(GEN_CORE_DIR)
vpath default_log.cc               $(GEN_CORE_DIR)
vpath heartbeat.cc                 $(GEN_CORE_DIR)
vpath thread.cc                    $(BASE_DIR)/src/lib/base
vpath thread_myself.cc             $(BASE_DIR)/src/lib/base
vpath trace.cc                     $(BASE_DIR)/src/lib/base
vpath dataspace_component.cc       $(REP_DIR)/src/core/spec/$(BOARD)
vpath io_mem_session_component.cc  $(REP_DIR)/src/core/spec/$(BOARD)
vpath irq_session_component.cc     $(REP_DIR)/src/core/spec/$(BOARD)
vpath platform_services.cc         $(REP_DIR)/src/core/spec/$(BOARD)
vpath %.cc                         $(REP_DIR)/src/core
