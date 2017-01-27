#!/bin/bash

set -e

OSMSCOUT_IMPORTER=/usr/local/osmscout/bin/Import
OSMSCOUT_MAPSTYLE=/usr/local/osmscout/share/stylesheets/map.ost
GEOCODER_IMPORTER=../../importer/importer

if [ $# -lt 5 ] ; then
	echo "Usage: $0 mapfile_pbf world_dir continent country country_2_letter_code"
	exit 1
fi

PBF=$1
WORLD_DIR=$2
CONTINENT=$3
COUNTRY=$4
COUNTRY_CODE=$5

FULLDIR=$WORLD_DIR/full
IMPDIR="$FULLDIR/$CONTINENT/$COUNTRY"
SQL="$WORLD_DIR/$CONTINENT/$COUNTRY.sqlite"

mkdir -p "$IMPDIR"

"$OSMSCOUT_IMPORTER" --typefile "$OSMSCOUT_MAPSTYLE" --delete-temporary-files true --delete-debugging-files true --delete-analysis-files true --delete-report-files true --destinationDirectory "$IMPDIR" "$PBF"

"$GEOCODER_IMPORTER" "$IMPDIR" "$SQL" "$COUNTRY_CODE"

# cleanup older version
rm -rf "$IMPDIR.tar.bz2"
rm -rf "$SQL.bz2"

# compress and delete libosmscout dir
(cd "$IMPDIR/.." && tar jcvf "$COUNTRY.tar.bz2" "$COUNTRY")
bzip2 "$SQL"

rm -rf "$IMPDIR"
