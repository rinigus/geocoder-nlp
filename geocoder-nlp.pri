#DEFINES += GEONLP_PRINT_DEBUG_QUERIES
#DEFINES += GEONLP_PRINT_DEBUG

INCLUDEPATH += $$PWD/src
INCLUDEPATH += $$PWD/thirdparty/sqlite3pp/headeronly_src

DEPENDPATH += $$PWD/src

SOURCES += \
    $$PWD/src/postal.cpp \
    $$PWD/src/geocoder.cpp

HEADERS += \
    $$PWD/src/postal.h \
    $$PWD/src/geocoder.h \
    $$PWD/src/version.h
       
LIBS += -lpostal 
