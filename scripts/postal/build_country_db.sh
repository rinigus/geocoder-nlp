#!/bin/bash

set -e

if [ $# -ne 3 ]; then
    echo "
This script generates a country specific databases for use with
libpostal routines.

Usage:

$0 addrdata_dir output_root_dir country_code

where 

addrdata_dir: path to the directory containing files required for
address parser training (formatted_addresses_tagged.random.tsv
formatted_places_tagged.random.tsv)

output_root_dir: path to the directory where country specific
subdirectory will be created

country_code: ISO 3166-1 alpha-2 country code (2 letters,
https://en.wikipedia.org/wiki/ISO_3166-1)

The country code can be specified in any case. It will be converted to
uppercase when making subdirectory under output_root_dir. Note that a
temporary files will be created under output_root_dir while this
script is running. This temporary directory will be removed at the end
of the script.

The script uses build_geodb and address_parser_train from
libpostal. Either ensure that these executables are in the path or
point the variable POSTAL_SRC_DIR in the script to a directory
containing these executables.

"
    exit -1
fi

#################################################################
### PATH TO LIBPOSTAL SRC DIRECTORY WITH COMPILED EXECUTABLES ###

POSTAL_SRC_DIR=libpostal/src

#################################################################

ADDRDATA=$1
OUTPUT=$2
COUNTRY=$3

TMPDATA="$OUTPUT/tmp-$COUNTRY-`cat /dev/urandom | tr -dc 'a-zA-Z0-9' | fold -w 8 | head -n 1`"

COUNTRY_LOWER="${COUNTRY,,}"
COUNTRY_UPPER="${COUNTRY^^}"

COUNTRY_DIR_BASE=$COUNTRY_UPPER
COUNTRY_DIR="$OUTPUT/$COUNTRY_DIR_BASE"

PATH="$POSTAL_SRC_DIR:$PATH"

OUTPUT_ADDRESS="$TMPDATA/address.tsv"

rm -rf "$COUNTRY_DIR"

mkdir -p "$TMPDATA"
mkdir -p "$COUNTRY_DIR/address_parser"

# Addresses
cp /dev/null "$OUTPUT_ADDRESS"
clow=$(echo $COUNTRY_LOWER | tr "-" "\n")
for C in $clow; do 
    for file in formatted_addresses_tagged.random.tsv formatted_places_tagged.random.tsv formatted_ways_tagged.random.tsv geoplanet_formatted_addresses_tagged.random.tsv openaddresses_formatted_addresses_tagged.random.tsv uk_openaddresses_formatted_addresses_tagged.random.tsv; do
	echo "Address data preparation: $file / $C"
	DSPLIT="$ADDRDATA/$file-split"
	if [ -d $DSPLIT ]; then
	    echo "Using pre-split" $file
	    cat "$ADDRDATA/$file-split/$file-$C" >> "$OUTPUT_ADDRESS" || true
	else
	    grep $'\t'$C$'\t' "$ADDRDATA/$file" >> "$OUTPUT_ADDRESS" || true
	fi
    done
done

echo "Randomize addresses"
shuf -o "$OUTPUT_ADDRESS.shuf" "$OUTPUT_ADDRESS"
mv "$OUTPUT_ADDRESS.shuf" "$OUTPUT_ADDRESS"

########################################################################

echo "Address training"
tlines=`cat "$OUTPUT_ADDRESS" | wc -l`
reflines=15000000
refiters=35
if (( tlines*5 > reflines )); then
    iters=5
    minup=5
else
    iters=$((reflines / tlines + 1))
    minup=3
    if (( iters > refiters )); then
        iters=$refiters
        minup=1
    fi
fi

echo 'Number of training lines:' $tlines
echo 'Iterations:' $iters
echo 'Minimal updates:' $minup

time address_parser_train --iterations $iters --min-updates $minup "$OUTPUT_ADDRESS" "$COUNTRY_DIR/address_parser"

echo "Removing temporary directory"
rm -rf "$TMPDATA"

# echo "Compressing country database"
# mkdir -p "$OUTPUT/compressed"
# (cd "$OUTPUT" && tar jcvf "compressed/$COUNTRY_DIR_BASE.tar.bz2" "$COUNTRY_DIR_BASE")
