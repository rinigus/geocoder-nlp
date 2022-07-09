#ifndef GEOCODER_NORMALIZATION_H
#define GEOCODER_NORMALIZATION_H

#include <sqlite3pp.h>
#include <string>

void normalize_libpostal(sqlite3pp::database &db, std::string address_expansion_dir, bool verbose);

void normalized_to_final(sqlite3pp::database &db, std::string path);

#endif
