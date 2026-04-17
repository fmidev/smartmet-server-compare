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

smartmet_server_compare_SOURCES = \
	compare/main.cpp \
	compare/CompareRunner.cpp \
	compare/CompareRunner.h \
	compare/ContentHandler.cpp \
	compare/ContentHandler.h \
	compare/DiffView.cpp \
	compare/DiffView.h \
	compare/HttpClient.cpp \
	compare/HttpClient.h \
	compare/ImageCompare.cpp \
	compare/ImageCompare.h \
	compare/ImageDiffViewer.cpp \
	compare/ImageDiffViewer.h \
	compare/InputBar.cpp \
	compare/InputBar.h \
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
