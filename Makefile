##
# hermes-broadcast
#
# @file
# @version 0.1

uname_p := $(shell uname -m)

CC=gcc

CPPFLAGS = -D_DEFAULT_SOURCE -D_FILE_OFFSET_BITS=64
CFLAGS   = -O3 -g -std=c99 -Wall -I. -Iraptorq -Ioblas -pthread
CFLAGS  += -funroll-loops -ftree-vectorize -fno-inline -fstack-protector-all
LDFLAGS = -lpthread -lrt

ifeq (${uname_p},aarch64)
	OBLAS_CPPFLAGS="-DOBLAS_NEON"
# aarch64 Raspberry Pi 4 or better
	CFLAGS+=-moutline-atomics -march=armv8-a+crc
# for Pi 5 use:
#	CFLAGS+=-march=armv8.2-a+crypto+fp16+rcpc+dotprod
else
	OBLAS_CPPFLAGS="-DOBLAS_AVX -DOCTMAT_ALIGN=32"
# x86_64 with SSE 4.2 level or better
	CFLAGS+=-march=x86-64-v2
endif

# RaptorQ nanorq implementation
OBJ=\
raptorq/bitmask.o\
raptorq/io.o\
raptorq/params.o\
raptorq/precode.o\
raptorq/rand.o\
raptorq/sched.o\
raptorq/spmat.o\
raptorq/tuple.o\
raptorq/wrkmat.o\
raptorq/nanorq.o

# Common objects for TCP/KISS support
COMMON_OBJ = shm_posix.o ring_buffer_posix.o crc6.o kiss.o tcp_interface.o

all: transmitter receiver raptorq/libnanorq.a

receiver.o: receiver.c tcp_interface.h kiss.h

transmitter.o: transmitter.c tcp_interface.h kiss.h

kiss.o: kiss.c kiss.h

tcp_interface.o: tcp_interface.c tcp_interface.h kiss.h

receiver: receiver.o $(COMMON_OBJ) raptorq/libnanorq.a
	$(CC) receiver.o $(COMMON_OBJ) raptorq/libnanorq.a -o receiver $(LDFLAGS)

transmitter: transmitter.o $(COMMON_OBJ) raptorq/libnanorq.a
	$(CC) transmitter.o $(COMMON_OBJ) raptorq/libnanorq.a -o transmitter $(LDFLAGS)

oblas/liboblas.a:
	$(MAKE) -C oblas CPPFLAGS+=$(OBLAS_CPPFLAGS)

raptorq/libnanorq.a: $(OBJ) oblas/liboblas.a
	$(AR) rcs $@ $(OBJ) oblas/*.o


.PHONY: clean

clean:
	$(RM) transmitter receiver raptorq/*.o raptorq/*.a *.o *.a *.gcda *.gcno *.gcov callgrind.* *.gperf *.prof *.heap perf.data perf.data.old
	$(MAKE) -C oblas clean
