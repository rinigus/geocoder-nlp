cmake_minimum_required(VERSION 3.6.0)

project(geocoder-nlp
	VERSION 1.0.0
	DESCRIPTION "Geocoder NLP")

set(CMAKE_INCLUDE_CURRENT_DIR ON)
set(CMAKE_CXX_STANDARD 11)
set(CMAKE_CXX_STANDARD_REQUIRED True)

include(FindPkgConfig)
include(FeatureSummary)
include(GNUInstallDirs)

find_package(PkgConfig REQUIRED)

pkg_check_modules(MARISA marisa IMPORTED_TARGET)
pkg_check_modules(KYOTOCABINET kyotocabinet IMPORTED_TARGET)
pkg_check_modules(POSTAL postal IMPORTED_TARGET)
pkg_check_modules(SQLITE3 sqlite3 IMPORTED_TARGET)

set(SRC
  src/geocoder.cpp
  src/postal.cpp)

set(HEAD
  src/geocoder.h
  src/postal.h)

# sqlite3pp include
include_directories(thirdparty/sqlite3pp/headeronly_src)
include_directories(src)

# demo codes
add_executable(geocoder-nlp
  demo/geocoder-nlp.cpp
  ${SRC}
  ${HEAD})

target_link_libraries(geocoder-nlp
  -lmarisa -lkyotocabinet -lpostal -lsqlite3)

add_executable(nearby-line
  demo/nearby-line.cpp
  ${SRC}
  ${HEAD})

target_link_libraries(nearby-line
  -lmarisa -lkyotocabinet -lpostal -lsqlite3)

add_executable(nearby-point
  demo/nearby-point.cpp
  ${SRC}
  ${HEAD})

target_link_libraries(nearby-point
  -lmarisa -lkyotocabinet -lpostal -lsqlite3)


feature_summary(WHAT ALL FATAL_ON_MISSING_REQUIRED_PACKAGES)

