#!/bin/bash

set -e

OSMSCOUT_IMPORTER=../../../../osmscout-install/bin/Import
OSMSCOUT_MAPSTYLE=../../importer/stylesheet/map.ost
GEOCODER_IMPORTER=../../importer/importer

export LD_LIBRARY_PATH=../../../../osmscout-install/lib:../../../install/lib

if [ $# -lt 5 ] ; then
	echo "Usage: $0 mapfile_pbf base_dir continent country country_2_letter_code"
	exit 1
fi

PBF=$1
BASE_DIR=$2
CONTINENT=$3
COUNTRY=$4
COUNTRY_CODE=$5

IMPDIR="$BASE_DIR/osmscout/$CONTINENT/$COUNTRY"
SQLDIR="$BASE_DIR/geocoder-nlp/$CONTINENT/$COUNTRY"
SQL="$SQLDIR/location.sqlite"

rm -rf "$IMPDIR" "$SQLDIR"
mkdir -p "$IMPDIR"
mkdir -p "$SQLDIR"

"$OSMSCOUT_IMPORTER" --typefile "$OSMSCOUT_MAPSTYLE" --delete-temporary-files true --delete-debugging-files true --delete-analysis-files true --delete-report-files true --destinationDirectory "$IMPDIR" "$PBF"

"$GEOCODER_IMPORTER" "$IMPDIR" "$SQL" "$COUNTRY_CODE"

./pack.sh "$IMPDIR" 10
./pack.sh "$SQLDIR" 1

