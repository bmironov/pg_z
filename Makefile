EXTENSION = pg_z
DATA = pg_z--1.0.sql
MODULE_big = pg_z

BUILD_DIR = tmp
SRC_MODULES = pg_z mem_manager gzip lz4 zstd

REGRESS = gzip deflate lz4 zstd db_params


ifeq ($(filter $(BUILD_DIR),$(notdir $(CURDIR))),)

all:
	@cd $(BUILD_DIR) && $(MAKE) -f ../Makefile VPATH=..

debug:
	@cd $(BUILD_DIR) && $(MAKE) -f ../Makefile VPATH=.. DEBUG=1

install:
	@cd $(BUILD_DIR) && $(MAKE) -f ../Makefile VPATH=.. install

installcheck:
	@cd $(BUILD_DIR) && $(MAKE) -f ../Makefile VPATH=.. installcheck

clean: clean-artifacts
	@cd $(BUILD_DIR) && $(MAKE) -f ../Makefile VPATH=.. clean

clean-artifacts:
	rm -f $(BUILD_DIR)/*.o $(BUILD_DIR)/*.bc $(BUILD_DIR)/*.so

else
OBJS = $(addsuffix .o, $(SRC_MODULES))

ifdef DEBUG
    PG_CFLAGS += -g3 -O0
endif

PG_CONFIG = pg_config
PGXS := $(shell $(PG_CONFIG) --pgxs)
include $(PGXS)


-include ../Makefile.port

# Enforce that configure must be run beforehand
ifndef CONFIGURE_RUN
$(error Please run ./configure before running make)
endif

SHLIB_LINK += $(DYNAMIC_LIBS)
endif
