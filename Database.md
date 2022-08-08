# Geocoder NLP database format

The geocoder database consists of several files which are expected to be in the
same directory. All locations are described using singe coordinate to keep the
files as small as possible.

The files composing a database are:

1. geonlp-primary.sqlite: SQLite database with location description and coordinate
2. geonlp-normalized.trie: MARISA database with normalized strings
3. geonlp-normalized-id.kch: Kyoto Cabinet database for linking MARISA and primary IDs

## geonlp-primary.sqlite

SQLite database contains location description, their organization into hierarchy
of objects. 

Table `object_primary` keeps location description. In this table, objects are
stored sequentially (in terms of their `id`) according to the positioning in the
object hierarchy with the children stored after parents. Table `hierarchy` has a
record for each item (`id` from `object_primary`) with the children consisting
of parent ID (`prim_id`) and the ID of the last child (`last_subobject`). 

Object types are stored separately in `type` table with the type ID used in
`object_primary`.

Spatial queries are indexed using R-Tree with `box_id` used as a reference in
`object_primary`. Namely, as all objects are stored as points, for storage
efficiency, objects next to each other are set to have the same `box_id` and are
found through `-rtree` tables.

Table `meta` keeps database format version and is used to check version
compatibility.

## geonlp-normalized.trie

All normalized strings are stored in MARISA database
(https://github.com/s-yata/marisa-trie). Normalized strings are formed from
`name` and other similar fields of `object_primary` table in
`geonlp-primary.sqlite`. All strings are pushed into MARISA database that
assigns its internal ID for each of the strings.

## geonlp-normalized-id.kch

Kyoto Cabinet (https://dbmx.net/kyotocabinet/) database for linking MARISA and
primary IDs. Hash database variant is used where `key` is an ID provided by
MARISA for a search string and value is an array of bytes consisting of
`object_primary` IDs stored as `uint32_t` one after another. The array is stored
using `std::string`.