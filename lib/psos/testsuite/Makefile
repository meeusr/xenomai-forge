ifeq ($(DESTDIR),)
XENO_CONFIG=xeno-config
else
XENO_CONFIG=$(DESTDIR)/bin/xeno-config
endif

prefix := $(shell DESTDIR=$(DESTDIR) $(XENO_CONFIG) --prefix)
solibs := $(shell DESTDIR=$(DESTDIR) $(XENO_CONFIG) --libdir)

ifeq ($(prefix),)
$(error Please add <xenomai-install-path>/bin to your PATH variable or specify DESTDIR)
endif

TESTS := \
	task-1 task-2 task-3 task-4 task-5 task-6 \
	tm-1 tm-2 tm-3 tm-4 tm-5 tm-6 tm-7 \
	mq-1 mq-2 mq-3 \
	sem-1 sem-2 \
	pt-1 \
	rn-1

CFLAGS := $(shell DESTDIR=$(DESTDIR) $(XENO_CONFIG) --skin=psos --cflags) -g
LDFLAGS := $(shell DESTDIR=$(DESTDIR) $(XENO_CONFIG) --skin=psos --ldflags)
CCLD = $(shell DESTDIR=$(DESTDIR) $(XENO_CONFIG) --ccld)

all: $(TESTS)

%: %.c
	$(CCLD) -o $@ $< $(CFLAGS) $(LDFLAGS)

clean:
	$(RM) $(TESTS) *~

# Run the test suite. We pin all tests to CPU #0, so that SMP does not
# alter the execution sequence we expect from them.
test: all
	@for t in $(TESTS); do \
		echo -n $$t...; \
		sudo LD_LIBRARY_PATH=$(solibs) ./$$t --cpu-affinity=0 && echo ok || echo BAD; \
	done

.PHONY: clean test
