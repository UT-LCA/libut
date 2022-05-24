INC     = -I./inc
CFLAGS  = -g -Wall -std=gnu11 -D_GNU_SOURCE $(INC) -march=armv8-a+crc -DTLS_LOCAL_EXEC
CXXFLAGS  = -g -Wall -std=gnu++11 -D_GNU_SOURCE $(INC) -march=armv8-a+crc -D_GLIBC_USE_C11_ABI=1 -DTLS_LOCAL_EXEC
#CFLAGS  = -g -Wall -std=gnu11 -D_GNU_SOURCE $(INC) -mssse3
LDFLAGS = -no-pie
LD	= gcc
CC	= gcc
CXX	= g++
AR	= ar

ifneq ($(DEBUG),)
CFLAGS += -DDEBUG -DCCAN_LIST_DEBUG -rdynamic -O0 -ggdb
LDFLAGS += -rdynamic
else
CFLAGS += -DNDEBUG -O3
endif

ifeq ($(PREFIX),)
PREFIX=/usr/local
endif

# handy for debugging
print-%  : ; @echo $($*)

# libbase.a - the base library
base_src = $(wildcard base/*.c)
base_obj = $(base_src:.c=.o)

# hwallocd - a hardware resource allocator deamon
hwalloc_src = $(wildcard hwalloc/*.c)
hwalloc_obj = $(hwalloc_src:.c=.o)

# runtime - a user-level threading library
runtime_src = $(wildcard runtime/*.c)
runtime_asm = $(wildcard runtime/*.S)
runtime_obj = $(runtime_src:.c=.o) $(runtime_asm:.S=.o)

# rt - a c++ wrapper library for the runtime library
rt_src = bindings/cc/thread.c
rt_obj = $(rt_src:.c=.o)

# test cases
test_src = $(wildcard tests/*.c)
test_obj = $(test_src:.c=.o)
test_targets = $(basename $(test_src))

# must be first
all: libbase.a libruntime.a librt++.a libut.a hwallocd $(test_targets)

libbase.a: $(base_obj)
	$(AR) rcs $@ $^

libruntime.a: $(runtime_obj)
	$(AR) rcs $@ $^

librt++.a: $(rt_obj)
	$(AR) rcs $@ $^

libut.a: $(base_obj) $(runtime_obj) $(rt_obj)
	$(AR) rcs $@ $^

hwallocd: $(hwalloc_obj) libbase.a
	$(LD) $(LDFLAGS) -o $@ $(hwalloc_obj) libbase.a \
	-lpthread -lnuma -ldl

$(test_targets): $(test_obj) libbase.a libruntime.a
	$(LD) $(LDFLAGS) -o $@ $@.o libruntime.a libbase.a -lpthread

install: libbase.a libruntime.a librt++.a libut.a
	mkdir -p $(PREFIX)/lib
	cp $^ -t $(PREFIX)/lib
	mkdir -p $(PREFIX)/include/ut
	cp -r inc/* -t $(PREFIX)/include/ut
	mkdir -p $(PREFIX)/include/ut/cc
	cp bindings/cc/*.h -t $(PREFIX)/include/ut/cc

# general build rules for all targets
src = $(base_src) $(test_src) $(hwalloc_src) $(runtime_src) $(rt_src)
asm = $(runtime_asm)
obj = $(src:.c=.o) $(asm:.S=.o)
dep = $(obj:.o=.d)

ifneq ($(MAKECMDGOALS),clean)
-include $(dep)   # include all dep files in the makefile
endif

# rule to generate a dep file by using the C preprocessor
# (see man cpp for details on the -MM and -MT options)
%.o: %.c
	$(CC) $(CFLAGS) -c $< -o $@
%.d: %.S
	@$(CC) $(CFLAGS) $< -MM -MT $(@:.d=.o) >$@
%.o: %.S
	$(CC) $(CFLAGS) -c $< -o $@
%.o: %.cc
	$(CXX) $(CXXFLAGS) -c $< -o $@

.PHONY: clean
clean:
	rm -f $(obj) $(dep) libbase.a libruntime.a librt++.a libut.a hwallocd $(test_targets)
