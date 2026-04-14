include $(shell smartbuildcfg --prefix)/share/smartmet/devel/makefile.inc

PROG = smartmet-server-compare
SPEC = smartmet-server-compare

GTKMM_CLAGS = $(shell pkg-config --cflags gtkmm-3.0)
GTKMM_LDFLAGS = $(shell pkg-config --libs gtkmm-3.0)

MAGICK_CFLAGS = $(shell pkg-config --cflags Magick++)
MAGICK_LDFLAGS = $(shell pkg-config --libs Magick++)

LIBS += \
	$(PREFIX_LDFLAGS) \
	-lsmartmet-spine \
	-lsmartmet-macgyver \
	$(GTKMM_LDFLAGS) \
	$(MAGICK_LDFLAGS) \
	-ltinyxml2 \
	-ljsoncpp \
	$(REQUIRED_LIBS) \
	-lpthread -lrt

CFLAGS += \
	$(PREFIX_CFLAGS) \
	$(GTKMM_CLAGS) \
	$(MAGICK_CFLAGS) \
	$(REQUIRED_CFLAGS) \
	-I. -I.. -I../..

SRC_DIRS := compare
vpath %.cpp $(SRC_DIRS)
SRCS := $(foreach dir,$(SRC_DIRS),$(wildcard $(dir)/*.cpp))
HDRS := $(filter-out $(INTERNAL_HEADERS), $(foreach dir,$(SRC_DIRS),$(wildcard $(dir)/*.h)))
OBJS := $(patsubst %.cpp, obj/%.o, $(notdir $(SRCS)))

.SUFFIXES: $(SUFFIXES) .cpp

.PHONY: all clean rpm

all: smartmet-server-compare

clean:
	rm -rf obj smartmet-server-compare

rpm: clean $(SPEC).spec
	rm -f $(SPEC).tar.gz # Clean a possible leftover from previous attempt
	tar -czvf $(SPEC).tar.gz --exclude test --exclude-vcs --transform "s,^,$(SPEC)/," *
	rpmbuild -tb $(SPEC).tar.gz
	rm -f $(SPEC).tar.gz

smartmet-server-compare: objdir $(OBJS)
	$(CXX) $(CFLAGS) -o $@ $(OBJS) $(LIBS)

objdir:
	mkdir -p $(objdir)

obj/%.o : %.cpp
	@mkdir -p $(dir $@)
	$(CXX) $(CFLAGS) $(INCLUDES) -c -MD -MF $(patsubst obj/%.o, obj/%.d, $@) -MT $@ -o $@ $<

ifneq ($(wildcard obj/*.d),)
-include $(wildcard obj/*.d)
endif
