EXTENSION = pg_z
DATA = pg_z--1.0.sql
MODULE_big = pg_z

BUILD_DIR = tmp
SRC_MODULES = pg_z mem_manager brotli gzip lz4 snappy zstd

REGRESS = brotli gzip deflate lz4 snappy zstd db_params


ifeq ($(filter $(BUILD_DIR),$(notdir $(CURDIR))),)

all:
	$(MAKE) -C $(BUILD_DIR) -f ../Makefile VPATH=..

debug:
	$(MAKE) -C $(BUILD_DIR) -f ../Makefile VPATH=.. DEBUG_BUILD=1

install:
	$(MAKE) -C $(BUILD_DIR) -f ../Makefile VPATH=.. install

installcheck:
	$(MAKE) -C $(BUILD_DIR) -f ../Makefile VPATH=.. installcheck

clean: clean-artifacts
	$(MAKE) -C $(BUILD_DIR) -f ../Makefile VPATH=.. clean

clean-artifacts:
	rm -f $(BUILD_DIR)/*.o $(BUILD_DIR)/*.bc $(BUILD_DIR)/*.so

else
OBJS = $(addsuffix .o, $(SRC_MODULES))

-include ../Makefile.port

# Enforce that configure must be run beforehand
ifndef CONFIGURE_RUN
$(error Please run ./configure before running make)
endif


ifdef DEBUG_BUILD
	PG_CFLAGS += -g3 -O0
	STRIP_CMD = :
else
	# O3 - maximum optimization
	# march=native - to use hardware acceleration where possible
	# For example, Snappy uses HW-accelerated CRC32C
	#
	# lto - Link-Time Optimization
	PG_CFLAGS += -march=native -flto
	SHLIB_LINK += -flto
	STRIP_CMD = strip --strip-unneeded $(shlib)
endif

PG_LDFLAGS += -L/usr/lib/x86_64-linux-gnu
SHLIB_LINK += -Wl,-Bstatic  -Wl,--start-group $(STATIC_LIBS) -Wl,--end-group \
			  -Wl,-Bdynamic $(DYNAMIC_LIBS)

PG_CONFIG = pg_config
PGXS := $(shell $(PG_CONFIG) --pgxs)
include $(PGXS)


# Strip debug info during "make" or "make debug" only.
# "make install" just copies what was produced
ifneq ($(filter install,$(MAKECMDGOALS)),install)
all: $(shlib)
	@$(STRIP_CMD)
endif

endif
