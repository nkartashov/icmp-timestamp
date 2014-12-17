CC = clang++
CXXFLAGS = -Wall -O2 -ggdb -Wextra -pedantic -Werror -std=c++11

LIBS := -lboost_system -lpthread

OS := $(shell uname -s)

ifeq ($(OS), Darwin)
BOOST_SEARCH_PATHS = -I/usr/local/include -L/usr/local/lib
CXXFLAGS += -stdlib=libc++
endif

SOURCEDIR = src
UTILDIR = $(SOURCEDIR)/utils

SOURCES = $(wildcard $(SOURCEDIR)/*.cpp)
UTIL_SOURCES = $(wildcard $(UTILDIR)/*.cpp)

TARGETS = $(SOURCES:src/%.cpp=%)

all: $(TARGETS)

$(TARGETS): $(SOURCES) $(UTIL_SOURCES)
	@echo -n 'Linking $@ ...'
	$(CC) $(CXXFLAGS) -o $@ $(UTIL_SOURCES) $(SOURCEDIR)/$@.cpp $(BOOST_SEARCH_PATHS) $(LIBS)
	@echo ' OK'
	@rm -rf *.dSYM

clean:
	@rm -rf $(TARGETS)

.PHONY: clean
