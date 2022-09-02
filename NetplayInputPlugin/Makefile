include Makefile.common

PROG = netplay_server

CXX = g++
LD = $(CXX)

CXXFLAGS = -std=c++17 -g -Og -isystem ../asio/asio/include
LDFLAGS = -pthread
SRCS = $(SERVER_SRC)
OBJS = $(subst .cpp,.o,$(SRCS))

.DEFAULT_GOAL := all
.PHONY: all clean depend server
include $(DEPEND)

all: server
server: $(PROG)

$(PROG): $(OBJS)
	$(LD) $(LDFLAGS) -o $(PROG) $^ $(LDLIBS)

$(OBJS): $(PCH)

$(PCH): $(HEADER)
	$(CXX) $(CXXFLAGS) -c -o $(PCH) $^

depend: $(SRCS) $(VERSION)
	$(CXX) $(CXXFLAGS) -MM $(SRCS) > $(DEPEND)

clean:
	rm -f $(VERSION) $(PCH) $(OBJS) $(PROG)
