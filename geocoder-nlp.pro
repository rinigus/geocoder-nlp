QT = 

CONFIG += c++11

TARGET = geocoder-nlp
CONFIG += console
CONFIG -= app_bundle

TEMPLATE = app

SOURCES += src/main.cpp \
    src/postal.cpp \
    src/geocoder.cpp

#DEFINES += GEONLP_PRINT_DEBUG_QUERIES
#DEFINES += GEONLP_PRINT_DEBUG

HEADERS += \
    src/postal.h \
    src/geocoder.h

INCLUDEPATH += /usr/local/libpostal/include
LIBS += -lpostal

INCLUDEPATH += thirdparty/sqlite3pp/headeronly_src
LIBS += -lsqlite3
