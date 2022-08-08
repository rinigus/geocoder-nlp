# Import program and scripts.

Import program is using Nominatim database and filters the data using boundary
given in GeoJSON file.

The boundary file can be generated from POLY files as provided by Geofabrik or
given in hierarchy folder of OSM Scout Server (see
https://github.com/rinigus/osmscout-server/tree/master/scripts/import/hierarchy).
As a converter, [poly2geojson](https://github.com/frafra/poly2geojson/) can be
used.

Data can be filtered using data/priority.list and data/skip.list files. Those
list OSM tags in the form where tag type and its value are merged using `_`. Out
of the lists, the priority one gives locations that would be kept in the
database even without names associated with them for reverse geocoding. The
"skip" list allows to specify locations that would be dropped by location type.

To generate larger amount of tags for the priority list, the scripts
under `scripts/tags` (from main directory of the project) can be used.

## Used

- Nominatim Docker https://github.com/mediagis/nominatim-docker/
- For testing: Nominatim-UI https://github.com/osm-search/nominatim-ui
- poly2geojson: https://github.com/frafra/poly2geojson