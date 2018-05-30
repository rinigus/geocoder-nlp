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

#######################################################
# list the modules required to generate location index
#
# modules were found by logging the full import and checking
# which files provide and require the needed files
for module in 1 2 3 4 5 6 7 8 9 10 11 12 13 14 17 22; do
    "$PROGPATH"/libosmscout/install/bin/Import -s $module -e $module --typefile "$PROGPATH"/stylesheet/map.ost --destinationDirectory "$D"/tmp "$1" 
done

"$PROGPATH"/importer "$D"/tmp "$D"

echo Removing temporary files
rm -rf "$D"/tmp
