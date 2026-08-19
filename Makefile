EXTENSION = pg_z
DATA = pg_z--1.0.sql
MODULE_big = pg_z

BUILD_DIR = tmp
SRC_MODULES = pg_z mem_manager
ALGO_MODULES = brotli gzip lz4 snappy zstd

-include $(top_srcdir)/Makefile.port Makefile.port ../Makefile.port

REGRESS := core db_params
ACTIVE_ALGOS := $(patsubst -DUSE_%,%,$(COMPRESSION_CFLAGS))
REGRESS += $(ACTIVE_ALGOS)

# Now we can add "deflate" if gzip is present for all tests to run
REGRESS := $(subst gzip,gzip deflate,$(REGRESS))
BENCHMARK_ALGOS := $(subst gzip,gzip deflate,$(ACTIVE_ALGOS))

export ACTIVE_ALGOS
export BENCHMARK_ALGOS
export CONFIGURE_RUN
export DATA

# Extract possible list of algos for load test
ifeq (load_test,$(firstword $(MAKECMDGOALS)))
  LOADTEST_ALGOS := $(wordlist 2,$(words $(MAKECMDGOALS)),$(MAKECMDGOALS))
  $(eval $(LOADTEST_ALGOS):;@:)
endif
LOADTEST_ALGOS ?= $(BENCHMARK_ALGOS)

# ========================================================================
# First part: start from root with redirect to $(BUILD_DIR)
# ========================================================================
ifeq ($(filter $(BUILD_DIR),$(notdir $(CURDIR))),)

.PHONY: all debug install installcheck benchmark load_test clean

all:
	$(MAKE) -C $(BUILD_DIR) -f ../Makefile VPATH=..
	@cp $(BUILD_DIR)/$(DATA) . 2>/dev/null || true

debug:
	$(MAKE) -C $(BUILD_DIR) -f ../Makefile VPATH=.. DEBUG_BUILD=1
	@cp $(BUILD_DIR)/$(DATA) . 2>/dev/null || true

install:
	$(MAKE) -C $(BUILD_DIR) -f ../Makefile VPATH=.. install

installcheck: all
	$(MAKE) -C $(BUILD_DIR) -f ../Makefile VPATH=.. installcheck

benchmark: all
	@build/benchmark.sh

load_test: all
	@build/load_test.sh "$(LOADTEST_ALGOS)"

clean:
	$(MAKE) -C $(BUILD_DIR) -f ../Makefile VPATH=.. clean
	rm -f $(DATA) $(BUILD_DIR)/*.o $(BUILD_DIR)/*.bc $(BUILD_DIR)/*.so

# ========================================================================
# Second part: build inside $(BUILD_DIR) using PGXS
# ========================================================================
else

# Enforce that configure must be run beforehand
ifndef CONFIGURE_RUN
$(error Please run ./configure before running make)
endif

OBJS = $(addsuffix .o, $(SRC_MODULES)) $(addsuffix .o, $(ACTIVE_ALGOS))

$(info OBJS:         $(OBJS))
$(info REGRESS:      $(REGRESS))
$(info ACTIVE_ALGOS: $(ACTIVE_ALGOS))

ifdef DEBUG_BUILD
	PG_CFLAGS += -g3 -O0
	STRIP_CMD = true
else
	# march=native - to use hardware acceleration where possible
	# For example, Snappy uses HW-accelerated CRC32C
	#
	# lto - Link-Time Optimization
	PG_CFLAGS += -march=native -flto
	SHLIB_LINK += -flto
	STRIP_CMD = strip --strip-unneeded $(shlib)
endif

PG_CFLAGS += $(COMPRESSION_CFLAGS)

PG_LDFLAGS += -L/usr/lib/x86_64-linux-gnu
SHLIB_LINK += -Wl,-Bstatic  -Wl,--start-group $(STATIC_LIBS) -Wl,--end-group \
			  -Wl,-Bdynamic $(DYNAMIC_LIBS)

PG_CONFIG = pg_config
PGXS := $(shell $(PG_CONFIG) --pgxs)
include $(PGXS)

ifneq ($(filter install,$(MAKECMDGOALS)),install)
all:
ifndef DEBUG_BUILD
	@echo "=== Stripping out .so if necessary ==="
	@$(STRIP_CMD)
endif
	@echo "=== Generating SQL extension file ==="
	@../build/generate_sql.sh ..
endif

endif
