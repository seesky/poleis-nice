CXX = g++

ifndef os
   os = LINUX
endif

ifndef arch
   arch = IA32
endif

CXXFLAGS = -fPIC -Wall -Wextra -D$(os) -finline-functions -O3 -fno-strict-aliasing -fvisibility=hidden

PKG_CONFIG ?= pkg-config
NICE_CFLAGS ?= $(shell $(PKG_CONFIG) --cflags nice 2>/dev/null)
NICE_LIBS ?= $(shell $(PKG_CONFIG) --libs nice 2>/dev/null)
CXXFLAGS += $(NICE_CFLAGS)
LIBS += $(NICE_LIBS)
# For systems without pkg-config, set NICE_CFLAGS and NICE_LIBS manually, e.g.:
# NICE_CFLAGS=-I/usr/include/nice -I/usr/include/glib-2.0 -I/usr/lib/glib-2.0/include
# NICE_LIBS=-lnice -lglib-2.0

ifeq ($(arch), IA32)
   CXXFLAGS += -DIA32
endif

ifeq ($(arch), POWERPC)
   CXXFLAGS += -mcpu=powerpc
endif

ifeq ($(arch), SPARC)
   CXXFLAGS += -DSPARC
endif

ifeq ($(arch), IA64)
   CXXFLAGS += -DIA64
endif

ifeq ($(arch), AMD64)
   CXXFLAGS += -DAMD64
endif

OBJS = api.o buffer.o cache.o ccc.o channel.o common.o core.o epoll.o list.o md5.o packet.o queue.o window.o
DIR = $(shell pwd)

all: libudt.so libudt.a udt

%.o: %.cpp %.h udt.h
	$(CXX) $(CXXFLAGS) $< -c

libudt.so: $(OBJS)
ifneq ($(os), OSX)
	$(CXX) -shared -o $@ $^ $(LIBS)
else
	$(CXX) -dynamiclib -o libudt.dylib -lstdc++ -lpthread -lm $^ $(LIBS)
endif

libudt.a: $(OBJS)
	ar -rcs $@ $^

udt:
	cp udt.h udt

clean:
	rm -f *.o *.so *.dylib *.a udt

install:
	export LD_LIBRARY_PATH=$(DIR):$$LD_LIBRARY_PATH

