include Makefile.common

BUILD_DIR = build/gcc

PROG = $(BUILD_DIR)/netplay_server

CXX = g++
LD = $(CXX)

CXXFLAGS = -std=c++17 -g -Og -isystem ../asio/asio/include
LDFLAGS = -pthread
SRCS = $(SERVER_SRC)
OBJS = $(addprefix $(BUILD_DIR)/,$(subst .cpp,.o,$(SRCS)))
PCH = $(BUILD_DIR)/$(HEADER).gch

.DEFAULT_GOAL := all
.PHONY: all clean depend server
include .gcc.depend

all: server
server: $(PROG)

$(PROG): $(OBJS)
	$(LD) $(LDFLAGS) -o $(PROG) $^ $(LDLIBS)

$(OBJS): $(PCH)

$(PCH): $(HEADER) | $(BUILD_DIR)
	$(CXX) $(CXXFLAGS) -c -o $(PCH) $<
	echo "#error precompiled header missing" > $(BUILD_DIR)/$(HEADER)

$(BUILD_DIR):
	 mkdir -p $(BUILD_DIR)

$(BUILD_DIR)/%.o:
	$(CXX) -include $(BUILD_DIR)/$(HEADER) $(CXXFLAGS) -c -o $@ $<

depend: $(SRCS) $(VERSION)
	$(CXX) $(CXXFLAGS) -MM $(SRCS) | sed "s/^\w/$(subst /,\/,$(BUILD_DIR)/)&/" > .gcc.depend

clean:
	rm -rf $(VERSION) $(BUILD_DIR)
