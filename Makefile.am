bin_PROGRAMS = smartmet-server-compare

smartmet_server_compare_CXXFLAGS = \
	$(AM_CXXFLAGS) \
	$(GTKMM_CFLAGS) \
	$(MAGICK_CFLAGS) \
	$(CURL_CFLAGS) \
	-Wall -Wextra -Wno-unused-parameter

smartmet_server_compare_LDADD = \
	$(GTKMM_LIBS) \
	$(MAGICK_LIBS) \
	$(CURL_LIBS) \
	-ltinyxml2 \
	-ljsoncpp

SPEC = smartmet-server-compare.spec

EXTRA_DIST = $(SPEC) autogen.sh m4/ax_cxx_compile_stdcxx.m4

# Source tarball suitable for rpmbuild (name without version, as the spec expects)
RPM_TARBALL = smartmet-server-compare.tar.gz

$(RPM_TARBALL):
	git archive --prefix=smartmet-server-compare/ HEAD | gzip > $@

.PHONY: rpm srpm

rpm: $(RPM_TARBALL)
	rpmbuild -tb $<
	rm -f $<

srpm: $(RPM_TARBALL)
	rpmbuild -ts $<
	rm -f $<

smartmet_server_compare_SOURCES = \
	compare/main.cpp \
	compare/CompareRunner.cpp \
	compare/CompareRunner.h \
	compare/ContentHandler.cpp \
	compare/ContentHandler.h \
	compare/DiffView.cpp \
	compare/DiffView.h \
	compare/EditQueryDialog.cpp \
	compare/EditQueryDialog.h \
	compare/HttpClient.cpp \
	compare/HttpClient.h \
	compare/ImageCompare.cpp \
	compare/ImageCompare.h \
	compare/ImageDiffViewer.cpp \
	compare/ImageDiffViewer.h \
	compare/InputBar.cpp \
	compare/InputBar.h \
	compare/InspectDialog.cpp \
	compare/InspectDialog.h \
	compare/MainWindow.cpp \
	compare/MainWindow.h \
	compare/QueryFetcher.cpp \
	compare/QueryFetcher.h \
	compare/RequestListView.cpp \
	compare/RequestListView.h \
	compare/ResultPanel.cpp \
	compare/ResultPanel.h \
	compare/ResultViewer.h \
	compare/Settings.cpp \
	compare/Settings.h \
	compare/StatusPanel.cpp \
	compare/StatusPanel.h \
	compare/TextDiffViewer.cpp \
	compare/TextDiffViewer.h \
	compare/Types.h \
	compare/UrlUtils.cpp \
	compare/UrlUtils.h
