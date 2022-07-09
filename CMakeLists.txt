cmake_minimum_required(VERSION 3.6.0)

project(geocoder-nlp
	VERSION 1.0.0
	DESCRIPTION "Geocoder NLP")

set(CMAKE_INCLUDE_CURRENT_DIR ON)
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED True)

include(FindPkgConfig)
include(FeatureSummary)
include(GNUInstallDirs)

find_package(PkgConfig REQUIRED)
find_package(nlohmann_json 3.2.0 REQUIRED)

pkg_check_modules(MARISA marisa IMPORTED_TARGET)
pkg_check_modules(KYOTOCABINET kyotocabinet IMPORTED_TARGET)
pkg_check_modules(POSTAL libpostal IMPORTED_TARGET)
pkg_check_modules(SQLITE3 sqlite3 IMPORTED_TARGET)
pkg_check_modules(LIBPQXX libpqxx IMPORTED_TARGET)

set(SRC
  src/geocoder.cpp
  src/postal.cpp)

set(HEAD
  src/geocoder.h
  src/postal.h)

# sqlite3pp include
include_directories(thirdparty/sqlite3pp/headeronly_src)
include_directories(src)

# importer
set(IMPSRC
  importer/src/config.h
  importer/src/main.cpp
  importer/src/hierarchy.cpp
  importer/src/hierarchy.h
  importer/src/hierarchyitem.cpp
  importer/src/hierarchyitem.h
  importer/src/normalization.cpp
  importer/src/normalization.h
  importer/src/utils.h
)
add_executable(geocoder-importer ${SRC} ${HEAD} ${IMPSRC})
target_link_libraries(geocoder-importer
  PkgConfig::MARISA 
  PkgConfig::KYOTOCABINET
  PkgConfig::POSTAL
  PkgConfig::SQLITE3
  PkgConfig::LIBPQXX
  nlohmann_json::nlohmann_json)

# demo codes
add_executable(geocoder-nlp
  demo/geocoder-nlp.cpp
  ${SRC}
  ${HEAD})

target_link_libraries(geocoder-nlp
  PkgConfig::MARISA 
  PkgConfig::KYOTOCABINET
  PkgConfig::POSTAL
  PkgConfig::SQLITE3)

add_executable(nearby-line
  demo/nearby-line.cpp
  ${SRC}
  ${HEAD})

target_link_libraries(nearby-line
  PkgConfig::MARISA 
  PkgConfig::KYOTOCABINET
  PkgConfig::POSTAL
  PkgConfig::SQLITE3)

add_executable(nearby-point
  demo/nearby-point.cpp
  ${SRC}
  ${HEAD})

target_link_libraries(nearby-point
  PkgConfig::MARISA 
  PkgConfig::KYOTOCABINET
  PkgConfig::POSTAL
  PkgConfig::SQLITE3)

# summary
feature_summary(WHAT ALL FATAL_ON_MISSING_REQUIRED_PACKAGES)
