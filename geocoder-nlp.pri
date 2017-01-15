#DEFINES += GEONLP_PRINT_DEBUG_QUERIES
#DEFINES += GEONLP_PRINT_DEBUG

INCLUDEPATH += $$PWD/src
INCLUDEPATH += $$PWD/thirdparty/sqlite3pp/headeronly_src

DEPENDPATH += $$PWD/src

SOURCES += \
    $$PWD/postal.cpp \
    $$PWD/geocoder.cpp

HEADERS += \
    $$PWD/postal.h \
    $$PWD/geocoder.h
       
LIBS += -lpostal -lsqlite3
