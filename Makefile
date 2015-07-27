ifeq ($(RTE_SDK),)
$(error "Please define RTE_SDK environment variable")
endif

# Default target, can be overriden by command line or environment
RTE_TARGET ?= x86_64-native-linuxapp-gcc

include $(RTE_SDK)/mk/rte.vars.mk

# binary name
APP = test

# all source are stored in SRCS-y
SRCS-y := main.c socket.c db.c config.c

CFLAGS += -O3 $(USER_FLAGS)
CFLAGS += $(WERROR_FLAGS) -g -I/usr/include/mysql `mysql_config --cflags --libs`
#-L/usr/lib/mysql -lmysqlclient

# workaround for a gcc bug with noreturn attribute
# http://gcc.gnu.org/bugzilla/show_bug.cgi?id=12603
ifeq ($(CONFIG_RTE_TOOLCHAIN_GCC),y)
CFLAGS_main.o += -Wno-return-type
endif

include $(RTE_SDK)/mk/rte.extapp.mk
