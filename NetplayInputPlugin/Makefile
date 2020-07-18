PROG=netplay_server
CXX=g++
CXXFLAGS=-std=c++17 -O2 -isystem ../asio/asio/include
LDFLAGS=-pthread
SRCS=server.cpp room.cpp user.cpp connection.cpp common.cpp
OBJS=$(subst .cpp,.o,$(SRCS))
HEADER=stdafx.h
PCH=$(HEADER).gch
APP_NAME=AQZ NetPlay
GIT_COUNT=$(shell git rev-list HEAD --count)
GIT_COMMIT=$(shell git rev-parse --short HEAD)
GIT_DIRTY=$(shell git status --short 2>/dev/null | grep -q '\S' && echo '-dirty')

$(PROG): $(OBJS)
	$(CXX) $(LDFLAGS) -o $(PROG) $^ $(LDLIBS)

$(OBJS): $(PCH)

$(PCH): $(HEADER)
	$(CXX) $(CXXFLAGS) -c -o $(PCH) $^

version.h: ../.git/index
	echo '#define APP_NAME "$(APP_NAME)"' > version.h
	echo '#define APP_VERSION "$(GIT_COUNT) ($(GIT_COMMIT)$(GIT_DIRTY))"' >> version.h
	echo '#define APP_NAME_AND_VERSION "$(APP_NAME) v$(GIT_COUNT) ($(GIT_COMMIT)$(GIT_DIRTY))"' >> version.h

include .depend

depend: $(SRCS) version.h
	$(CXX) $(CXXFLAGS) -MM $(SRCS) > .depend;

clean:
	rm -f version.h $(PCH) $(OBJS) $(PROG)
