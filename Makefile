ifdef TEST
TARGET_APP ?= bzrpc
else
TARGET_LIB ?= libbzrpc.a
endif

TOP_DIR = $(shell pwd)

#external definition
CJSON_INC_DIR ?= $(TOP_DIR)/thirdparty

#BUILD_THIRDPARTY flag
#BUILD_THIRDPARTY +=

#Extra flags
LOCAL_C_FLAGS += $(EXTRA_C_FLAGS)
LOCAL_LD_FLAGS += $(EXTRA_LD_FLAGS)
LOCAL_LD_LIBS += $(EXTRA_LD_LIBS)

#default is $PWD/build
ifdef BUILD_DIR
LOCAL_BUILD_DIR := $(BUILD_DIR)
endif

#default is $LOCAL_BUILD_DIR/obj
ifdef BUILD_OBJ_DIR
LOCAL_BUILD_OBJ_DIR := $(BUILD_OBJ_DIR)
endif

 #default is $LOCAL_BUILD_DIR/lib
ifdef INSTALL_LIB_DIR
LOCAL_INSTALL_LIB_DIR := $(INSTALL_LIB_DIR)
endif

#default is $LOCAL_BUILD_DIR/bin
ifdef INSTALL_BIN_DIR
LOCAL_INSTALL_BIN_DIR := $(INSTALL_BIN_DIR)
endif


ifdef TEST
BUILD_THIRDPARTY ?= yes
LOCAL_C_FLAGS += -g
ifdef SERVER
LOCAL_C_FLAGS += -Dtest_bzrpc_server=main
LOCAL_BUILD_DIR := build/server
else
ifdef CLIENT
LOCAL_C_FLAGS += -Dtest_bzrpc_client=main
LOCAL_BUILD_DIR := build/client
endif
endif
endif

# it's not neccessary to define LOCAL_SRC_FILES, default is $PWD/*.c
#LOCAL_SRC_FILES += 
LOCAL_C_FLAGS += -I$(CJSON_INC_DIR)
LOCAL_LD_FLAGS += -lpthread

ifdef BUILD_THIRDPARTY
LOCAL_EXTRA_SRC_FILES += $(wildcard thirdparty/*.c)
endif

include build.mk

