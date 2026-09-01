##
# hermes-broadcast
#
# @file
# @version 0.1

uname_p := $(shell uname -m)

CC=gcc

CPPFLAGS = -D_DEFAULT_SOURCE -D_FILE_OFFSET_BITS=64
CFLAGS   = -O3 -g -std=c11 -Wall -I. -Iraptorq/include -Iraptorq/deps -Iraptorq/src -pthread
CFLAGS  += -Wno-error=incompatible-pointer-types
CFLAGS  += -funroll-loops -ftree-vectorize -fno-inline -fstack-protector-all -Wno-unused -Wno-sequence-point
LDFLAGS = -lpthread -lrt

ifeq (${uname_p},aarch64)
# aarch64 Raspberry Pi 4 or better
        CFLAGS+=-moutline-atomics -march=armv8-a+crc
# for Pi 5 use:
#       CFLAGS+=-march=armv8.2-a+crypto+fp16+rcpc+dotprod
else
# x86_64 with SSE 4.2 level or better
        CFLAGS+=-march=x86-64-v2
endif

# RaptorQ nanorq implementation
OBJ=\
raptorq/lib/chooser.o\
raptorq/lib/io.o\
raptorq/lib/nanorq.o\
raptorq/lib/nanorq_core.o\
raptorq/lib/ops.o\
raptorq/lib/params.o\
raptorq/lib/partition.o\
raptorq/lib/precode.o\
raptorq/lib/rand.o\
raptorq/lib/sopi.o\
raptorq/lib/tuple.o\
raptorq/lib/uvec.o\
raptorq/deps/obl/oblas_lite.o\
raptorq/deps/obl/oblas_common.o

# Common objects for TCP/KISS support
COMMON_OBJ = crc6.o kiss.o tcp_interface.o

all: transmitter receiver broadcast_daemon raptorq/libnanorq.a

# Regression test for the nanorq re-vendor.  The vendored copy carries three
# HERMES-local helpers (reduced OTI/tag) that upstream does not; this drives the
# real wire layout through encode -> loss -> decode and requires a byte-identical
# file.  Run it after any nanorq update.
#   make test        round-trip only
#   make test-e2e    also the real transmitter/receiver over a loopback relay
rq_roundtrip_test: rq_roundtrip_test.c raptorq/libnanorq.a
	$(CC) $(CFLAGS) $(CPPFLAGS) -o $@ rq_roundtrip_test.c raptorq/libnanorq.a $(LDFLAGS) -lm

.PHONY: test test-e2e
test: rq_roundtrip_test
	./rq_roundtrip_test

test-e2e: all
	./bcast_loopback_test.sh 0 20000 18201
	./bcast_loopback_test.sh 1 20000 18202
	./bcast_loopback_test.sh 9 60000 18203
	./bcast_loopback_test.sh 10 60000 18204


receiver.o: receiver.c tcp_interface.h kiss.h

transmitter.o: transmitter.c tcp_interface.h kiss.h

daemon.o: daemon.c tcp_interface.h kiss.h mercury_modes.h

kiss.o: kiss.c kiss.h

tcp_interface.o: tcp_interface.c tcp_interface.h kiss.h

receiver: receiver.o $(COMMON_OBJ) raptorq/libnanorq.a
	$(CC) receiver.o $(COMMON_OBJ) raptorq/libnanorq.a -o receiver $(LDFLAGS)

transmitter: transmitter.o $(COMMON_OBJ) raptorq/libnanorq.a
	$(CC) transmitter.o $(COMMON_OBJ) raptorq/libnanorq.a -o transmitter $(LDFLAGS)

broadcast_daemon: daemon.o $(COMMON_OBJ) raptorq/libnanorq.a
	$(CC) daemon.o $(COMMON_OBJ) raptorq/libnanorq.a -o broadcast_daemon $(LDFLAGS)

raptorq/libnanorq.a: $(OBJ)
	$(AR) rcs $@ $(OBJ)


.PHONY: clean

clean:
	$(RM) transmitter receiver broadcast_daemon raptorq/*.o raptorq/*.a raptorq/lib/*.o raptorq/deps/obl/*.o *.o *.a *.gcda *.gcno *.gcov callgrind.* *.gperf *.prof *.heap perf.data perf.data.old
