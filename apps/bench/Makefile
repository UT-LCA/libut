# Makefile for benchmarks

BASEPATH = ../..
CXXPATH = ../../bindings/cc
INC     = -I../../inc -I../../bindings/cc -I./
CXXFLAGS  = -g -Wall -std=gnu++11 -D_GNU_SOURCE $(INC) -march=armv8-a+crc -D_GLIBC_USE_C11_ABI=1 -DTLS_LOCAL_EXEC
LDFLAGS = -no-pie

LD	= g++
CC	= g++
AR	= ar

ifneq ($(DEBUG),)
CXXFLAGS += -DDEBUG -DCCAN_LIST_DEBUG -rdynamic -O0 -ggdb
LDFLAGS += -rdynamic
else
CXXFLAGS += -DNDEBUG -O3
endif

# handy for debugging
print-%  : ; @echo $* = $($*)

tbench_src = tbench.cc
tbench_obj = $(tbench_src:.cc=.o)

tbench_qthread_src = tbench_qthread.cc
tbench_qthread_obj = $(tbench_qthread_src:.cc=.o)

librt_libs = $(CXXPATH)/librt++.a $(BASEPATH)/libruntime.a $(BASEPATH)/libbase.a

# must be first
all: tbench tbench_qthread

tbench: $(tbench_obj) $(librt_libs)
	$(LD) -o $@ $(LDFLAGS) $(tbench_obj) $(librt_libs) -lpthread

#tbench_qthread: $(tbench_qthread_obj)
#	$(LD) -o $@ -I${QTHREAD_INC} -L${QTHREAD_LIB} $< ${QTHREAD_LIB}/libqthread.a -lhwloc -lm -lpthread

tbench_qthread: tbench_qthread.cc
	g++ -o $@ -I${QTHREAD_INC} -L${QTHREAD_LIB} $< ${QTHREAD_LIB}/libqthread.a -lhwloc -lm -lpthread

# general build rules for all targets
src = $(tbench_src) $(tbench_qthread_src)
obj = $(src:.cc=.o)
dep = $(obj:.o=.d)

ifneq ($(MAKECMDGOALS),clean)
-include $(dep)   # include all dep files in the makefile
endif

# rule to generate a dep file by using the C preprocessor
# (see man cpp for details on the -MM and -MT options)
%.d: %.cc
	@$(CC) $(CXXFLAGS) $< -MM -MT $(@:.d=.o) >$@
%.o: %.cc
	$(CC) $(CXXFLAGS) -c $< -o $@

.PHONY: clean
clean:
	rm -f $(obj) $(dep) tbench tbench_qthread
