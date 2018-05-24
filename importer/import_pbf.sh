#!/bin/bash

set -e

if [ "$#" -eq 1 ]; then
    D=${1%-latest.osm.pbf}
    D=${D%.osm.pbf}
    D=${D%.pbf} 
    D=${D%.osm}    
    if [ "$1" == "$D" ] 
    then
        D=$1-imported
    fi

elif [ "$#" -eq 2 ]; then
    D=${2}
else
    echo "Usage: ./import_pbf.sh openstreetmapfilename [imported-dir-name]"
    exit 0    
fi

PROGPATH=$(dirname "$0")

echo Make directory: $D
mkdir -p "$D"/tmp

export LD_LIBRARY_PATH="$PROGPATH"/libosmscout/install/lib
"$PROGPATH"/libosmscout/install/bin/Import --typefile "$PROGPATH"/stylesheet/map.ost --delete-temporary-files true --delete-debugging-files true --delete-analysis-files true --delete-report-files true --destinationDirectory "$D"/tmp "$1"

"$PROGPATH"/importer "$D"/tmp "$D"

echo Removing temporary files
rm -rf "$D"/tmp
