PROG=netplay_server
CXX=g++
CXXFLAGS=-std=c++11 -O2 -isystem ../asio/asio/include
LDFLAGS=-pthread
SRCS=server.cpp session.cpp connection.cpp
OBJS=$(subst .cpp,.o,$(SRCS))
HEADER=stdafx.h
PCH=$(HEADER).gch
APP_NAME=AQZ NetPlay
GIT_COUNT=$(shell git rev-list HEAD --count)
GIT_COMMIT=$(shell git rev-parse --short HEAD)

$(PROG): $(OBJS)
	$(CXX) $(LDFLAGS) -o $(PROG) $^ $(LDLIBS)

$(OBJS): $(PCH)

$(PCH): $(HEADER)
	$(CXX) $(CXXFLAGS) -c -o $(PCH) $^

version.h: ../.git/index
	echo '#define APP_NAME "$(APP_NAME)"' > version.h
	echo '#define APP_VERSION "$(GIT_COUNT) ($(GIT_COMMIT))"' >> version.h
	echo '#define APP_NAME_AND_VERSION "$(APP_NAME) v$(GIT_COUNT) ($(GIT_COMMIT))"' >> version.h

include .depend

depend: $(SRCS) version.h
	$(CXX) $(CXXFLAGS) -MM $(SRCS) > .depend;

clean:
	rm -f version.h $(PCH) $(OBJS) $(PROG)
