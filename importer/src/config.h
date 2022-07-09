#ifndef GEOCODER_CONFIG_H
#define GEOCODER_CONFIG_H

#define TEMPORARY "" //"TEMPORARY" // set to empty if need to debug import

/// if there are more expansions that specified, this object will be dropped from normalization
/// table
#define MAX_NUMBER_OF_EXPANSIONS 85

/// starting from this length, check wheher the string is suspicious
#define LENGTH_STARTING_SUSP_CHECK 200

#define MAX_COMMAS 10 /// maximal number of commas allowed in a name

#define GEOCODER_IMPORTER_POSTGRES "GEOCODER_IMPORTER_POSTGRES"

typedef unsigned long hindex;
typedef long long int sqlid; /// type used by IDs in SQLite

#endif