PROG = smartmet-server-compare
SPEC = smartmet-server-compare

CXX ?= g++
CXXSTD ?= c++17

GTKMM_CFLAGS = $(shell pkg-config --cflags gtkmm-3.0)
GTKMM_LDFLAGS = $(shell pkg-config --libs gtkmm-3.0)

MAGICK_CFLAGS = $(shell pkg-config --cflags Magick++)
MAGICK_LDFLAGS = $(shell pkg-config --libs Magick++)

CURL_CFLAGS = $(shell pkg-config --cflags libcurl)
CURL_LDFLAGS = $(shell pkg-config --libs libcurl)

CFLAGS = -std=$(CXXSTD) -fPIC -Wall -Wextra -Wno-unused-parameter \
	-O2 -g \
	$(GTKMM_CFLAGS) \
	$(MAGICK_CFLAGS) \
	$(CURL_CFLAGS) \
	$(EXTRA_CFLAGS)

LIBS = \
	$(GTKMM_LDFLAGS) \
	$(MAGICK_LDFLAGS) \
	$(CURL_LDFLAGS) \
	-ltinyxml2 \
	-ljsoncpp \
	-lpthread

SRC_DIRS := compare
vpath %.cpp $(SRC_DIRS)
SRCS := $(foreach dir,$(SRC_DIRS),$(wildcard $(dir)/*.cpp))
HDRS := $(foreach dir,$(SRC_DIRS),$(wildcard $(dir)/*.h))
OBJS := $(patsubst %.cpp, obj/%.o, $(notdir $(SRCS)))

.SUFFIXES: $(SUFFIXES) .cpp

.PHONY: all clean rpm

all: $(PROG)

clean:
	rm -rf obj $(PROG)

rpm: clean $(SPEC).spec
	rm -f $(SPEC).tar.gz
	tar -czvf $(SPEC).tar.gz --exclude test --exclude-vcs --transform "s,^,$(SPEC)/," *
	rpmbuild -tb $(SPEC).tar.gz
	rm -f $(SPEC).tar.gz

$(PROG): $(OBJS)
	$(CXX) $(CFLAGS) -o $@ $(OBJS) $(LIBS)

obj/%.o : %.cpp
	@mkdir -p $(dir $@)
	$(CXX) $(CFLAGS) -c -MD -MF $(patsubst obj/%.o, obj/%.d, $@) -MT $@ -o $@ $<

ifneq ($(wildcard obj/*.d),)
-include $(wildcard obj/*.d)
endif
