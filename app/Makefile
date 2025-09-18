CXX = g++

ifndef os
   os = LINUX
endif

ifndef arch
   arch = IA32
endif

CXXFLAGS = -Wall -D$(os) -I../src -finline-functions -O3

PKG_CONFIG ?= pkg-config
NICE_CFLAGS := $(shell $(PKG_CONFIG) --cflags nice 2>/dev/null)
ifneq ($(findstring glib-2.0,$(NICE_CFLAGS)),glib-2.0)
NICE_CFLAGS := $(shell $(PKG_CONFIG) --cflags nice glib-2.0 2>/dev/null)
endif
NICE_LIBS ?= $(shell $(PKG_CONFIG) --libs nice 2>/dev/null)
CXXFLAGS += $(NICE_CFLAGS)
# For systems without pkg-config, set NICE_CFLAGS and NICE_LIBS manually, e.g.:
# NICE_CFLAGS=-I/usr/include/nice -I/usr/include/glib-2.0 -I/usr/lib/glib-2.0/include
# NICE_LIBS=-lnice -lglib-2.0

ifeq ($(arch), IA32)
   CXXFLAGS += -DIA32 #-mcpu=pentiumpro -march=pentiumpro -mmmx -msse
endif

ifeq ($(arch), POWERPC)
   CXXFLAGS += -mcpu=powerpc
endif

ifeq ($(arch), IA64)
   CXXFLAGS += -DIA64
endif

ifeq ($(arch), SPARC)
   CXXFLAGS += -DSPARC
endif

LIBS = -L../src -ludt -lstdc++ -lpthread -lm $(NICE_LIBS)

ifeq ($(os), UNIX)
   LIBS += -lsocket
endif

ifeq ($(os), SUNOS)
   LIBS += -lrt -lsocket
endif

DIR = $(shell pwd)

APP = appserver appclient sendfile recvfile test appniceserver appniceclient nice_channel_retry_test

all: $(APP)

%.o: %.cpp
	$(CXX) $(CXXFLAGS) $< -c

appserver: appserver.o
	$(CXX) $^ -o $@ $(LIBS)
appclient: appclient.o
	$(CXX) $^ -o $@ $(LIBS)
sendfile: sendfile.o
	$(CXX) $^ -o $@ $(LIBS)
recvfile: recvfile.o
	$(CXX) $^ -o $@ $(LIBS)
test: test.o
	$(CXX) $^ -o $@ $(LIBS)
appniceserver: appniceserver.o
	$(CXX) $^ -o $@ $(LIBS)
appniceclient: appniceclient.o
	$(CXX) $^ -o $@ $(LIBS)
nice_channel_retry_test: nice_channel_retry_test.o
	$(CXX) $^ -o $@ $(LIBS)

clean:
	rm -f *.o $(APP)

install:
	export PATH=$(DIR):$$PATH
