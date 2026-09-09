ifneq ($(wildcard VERSION),)
    GIT_VERSION := $(shell cat VERSION 2>/dev/null | tr -d '\n')
else ifneq ($(wildcard .git),)
    GIT_VERSION := $(shell git -c safe.directory='*' describe --tags --abbrev=0 2>/dev/null)
endif

GIT_VERSION := $(if $(GIT_VERSION),$(GIT_VERSION),0.0.1)
export GIT_VERSION

DATA = pg_z--$(GIT_VERSION).sql
export DATA


-include Makefile.port

ACTIVE_ALGOS := $(patsubst -DUSE_%,%,$(filter -DUSE_%,$(COMPRESSION_CFLAGS)))

empty :=
space := $(empty) $(empty)
BENCHMARK_ALGOS := $(subst gzip$(space),gzip deflate$(space),$(ACTIVE_ALGOS))
BENCHMARK_ALGOS := $(subst gzip_ng$(space),gzip_ng deflate_ng$(space),$(BENCHMARK_ALGOS))

ifeq (load_test,$(firstword $(MAKECMDGOALS)))
  LOADTEST_ALGOS := $(wordlist 2,$(words $(MAKECMDGOALS)),$(MAKECMDGOALS))
  $(eval $(LOADTEST_ALGOS):;@:)
endif
LOADTEST_ALGOS ?= $(BENCHMARK_ALGOS)

export ACTIVE_ALGOS
export CONFIGURE_RUN
export LOADTEST_ALGOS
export BENCHMARK_ALGOS


SUBDIRS = tmp


.PHONY: all benchmark load_test debug install installcheck clean distclean generate-sql

all:
	$(MAKE) -C $(SUBDIRS) -f Makefile
	$(MAKE) generate-sql

debug:
	$(MAKE) -C $(SUBDIRS) -f Makefile DEBUG_BUILD=1
	$(MAKE) generate-sql
	@if [ -f $(SUBDIRS)/pg_z.so ]; then \
		echo "=== Saving debug build to pg_z.so.debug ==="; \
		cp -f $(SUBDIRS)/pg_z.so $(SUBDIRS)/pg_z.so.debug; \
	fi

install:
	$(MAKE) -C $(SUBDIRS) -f Makefile install
	@if [ -f $(SUBDIRS)/pg_z.so.debug ]; then \
		echo "=== [DEBUG] Restoring debug symbols over installed library ==="; \
		TARGET_DIR="$(DESTDIR)`pg_config --pkglibdir`"; \
		echo "=== Target directory: $$TARGET_DIR ==="; \
		mkdir -p "$$TARGET_DIR"; \
		cp -f $(SUBDIRS)/pg_z.so.debug "$$TARGET_DIR/pg_z.so"; \
	fi

installcheck: show-details
	$(MAKE) -C $(SUBDIRS) -f Makefile installcheck

benchmark: all
	@build/benchmark.sh

load_test: all
	@build/load_test.sh "$(LOADTEST_ALGOS)"

clean:
	-$(MAKE) -C $(SUBDIRS) -f Makefile clean 2>/dev/null || true
	rm -f pg_z--*.sql $(SUBDIRS)/pg_z.so.debug

distclean:
	-$(MAKE) -C $(SUBDIRS) -f Makefile distclean 2>/dev/null || true
	rm -rf Makefile.port config.log config.status autom4te.cache
	rm -f $(SUBDIRS)/Makefile $(SUBDIRS)/Makefile.port $(SUBDIRS)/pg_z.so.debug

show-details:
	psql -c " \
		DROP EXTENSION IF EXISTS pg_z; \
		CREATE EXTENSION pg_z; \
		SELECT * FROM pg_z_version(); \
		SELECT * FROM pg_z_details(); \
	" || true

strip-debug-info:
	echo "=== Stripping out debug info from .so ==="
	@if [ -f $(SUBDIRS)/pg_z.so ]; then \
		strip --strip-unneeded $(SUBDIRS)/pg_z.so; \
	fi

generate-sql:
	@echo "=== Generating SQL extension file ==="
	@build/generate_sql.sh .
