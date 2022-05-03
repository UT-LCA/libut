INC     = -I./inc
CFLAGS  = -g -Wall -std=gnu11 -D_GNU_SOURCE $(INC) -march=armv8-a+crc -DTLS_LOCAL_EXEC
#CFLAGS  = -g -Wall -std=gnu11 -D_GNU_SOURCE $(INC) -mssse3
LDFLAGS = -T base/base.ld -no-pie
LD	= gcc
CC	= gcc
AR	= ar

ifneq ($(DEBUG),)
CFLAGS += -DDEBUG -DCCAN_LIST_DEBUG -rdynamic -O0 -ggdb
LDFLAGS += -rdynamic
else
CFLAGS += -DNDEBUG -O3
endif

# handy for debugging
print-%  : ; @echo $* = $($*)

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

# test cases
test_src = $(wildcard tests/*.c)
test_obj = $(test_src:.c=.o)
test_targets = $(basename $(test_src))

# must be first
all: libbase.a $(test_targets)

libbase.a: $(base_obj)
	$(AR) rcs $@ $^

libruntime.a: $(runtime_obj)
	$(AR) rcs $@ $^

hwallocd: $(hwalloc_obj) libbase.a base/base.ld
	$(LD) $(LDFLAGS) -o $@ $(hwalloc_obj) libbase.a \
	-lpthread -lnuma -ldl

$(test_targets): $(test_obj) libbase.a libruntime.a base/base.ld
	$(LD) $(LDFLAGS) -o $@ $@.o libruntime.a libbase.a -lpthread

# general build rules for all targets
src = $(base_src) $(test_src) $(hwalloc_src) $(runtime_src)
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

.PHONY: clean
clean:
	rm -f $(obj) $(dep) libbase.a hwallocd $(test_targets)
