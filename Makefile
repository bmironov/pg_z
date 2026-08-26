MAKEFILE_DIR := $(patsubst %/,%,$(dir $(abspath $(lastword $(MAKEFILE_LIST)))))
ifneq ($(wildcard $(MAKEFILE_DIR)/.git),)
    GIT_VERSION := $(shell git describe --tags --abbrev=0 2>/dev/null)
else ifneq ($(wildcard $(MAKEFILE_DIR)/VERSION),)
    GIT_VERSION := $(shell cat $(MAKEFILE_DIR)/VERSION 2>/dev/null | tr -d '\n')
endif

GIT_VERSION := $(if $(GIT_VERSION),$(GIT_VERSION),0.0.1)
export GIT_VERSION

DATA = pg_z--$(GIT_VERSION).sql
export DATA


-include Makefile.port

ACTIVE_ALGOS := $(patsubst -DUSE_%,%,$(COMPRESSION_CFLAGS))

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


.PHONY: all benchmark load_test debug install installcheck clean distclean

all:
	$(MAKE) -C $(SUBDIRS) -f Makefile

debug:
	$(MAKE) -C $(SUBDIRS) -f Makefile DEBUG_BUILD=1

install:
	$(MAKE) -C $(SUBDIRS) -f Makefile install

installcheck:
	$(MAKE) -C $(SUBDIRS) -f Makefile installcheck

benchmark: all
	@build/benchmark.sh

load_test: all
	@build/load_test.sh "$(LOADTEST_ALGOS)"

clean:
	-$(MAKE) -C $(SUBDIRS) -f Makefile clean 2>/dev/null || true
	rm -f pg_z--*.sql

distclean:
	-$(MAKE) -C $(SUBDIRS) -f Makefile distclean 2>/dev/null || true
	rm -rf Makefile.port config.log config.status autom4te.cache
	rm -f $(SUBDIRS)/Makefile $(SUBDIRS)/Makefile.port
