/////////////////////////////////////////////////////////////////////
/// WARNING: When adding new languages, increase the language count
/// num_languages in geocoder.c
/////////////////////////////////////////////////////////////////////

#include "config.h"
#include "geocoder.h"
#include "hierarchy.h"
#include "normalization.h"

#include <algorithm>
#include <boost/tokenizer.hpp>
#include <cctype>
#include <cstdlib>
#include <deque>
#include <fstream>
#include <iterator>
#include <libpostal/libpostal.h>
#include <locale>
#include <map>
#include <nlohmann/json.hpp>
#include <pqxx/pqxx>
#include <set>
#include <sqlite3pp.h>

using json = nlohmann::json;

////////////////////////////////////////////////////////////////////////////
// MAIN

int main(int argc, char *argv[])
{
  if (argc == 2)
    {
      std::string option = argv[1];
      if (option == "--version")
        {
          std::cout << GeoNLP::Geocoder::version << "\n";
          return 0;
        }
    }

  if (argc < 3)
    {
      std::cerr << "importer <poly.json> <geocoder-nlp database directory> "
                   "[postal_country_parser_code] [address_parser_directory] [verbose]\n";
      std::cerr
          << "When using optional parameters, you have to specify all of the perceiving ones\n";
      return 1;
    }

  std::string polyjson      = argv[1];
  std::string database_path = argv[2];
  std::string postal_country_parser;
  std::string postal_address_parser_dir;
  bool        verbose_address_expansion = false;

  if (argc > 3)
    postal_country_parser = argv[3];
  if (argc > 4)
    postal_address_parser_dir = argv[4];
  if (argc > 5 && strcmp("verbose", argv[5]) == 0)
    verbose_address_expansion = true;

  // load GeoJSON for surrounding (multi)polygon from poly.json
  std::string border;
  {
    std::ifstream                  fin(polyjson);
    std::istreambuf_iterator<char> begin(fin), end;
    std::string                    b(begin, end);
    border = b;
  }

  if (border.size())
    {
      json j = json::parse(border);
      border = j["geometry"].dump();
      std::cout << "Loaded border GeoJSON. Geometry string length: " << border.size() << "\n";
    }

  Hierarchy hierarchy;

  std::string postgres_dblink;
  const char *env = std::getenv(GEOCODER_IMPORTER_POSTGRES);
  if (env)
    postgres_dblink = env;
  else
    {
      std::cout << "Please specify PostgreSQL connection string using environment variable "
                << GEOCODER_IMPORTER_POSTGRES << "\n";
      return 0;
    }

  std::cout << "Postgres connection: " << postgres_dblink << std::endl;

  pqxx::connection pgc{ postgres_dblink };
  pqxx::work       txn{ pgc };

  const std::string base_query
      = "select place_id, linked_place_id, parent_place_id, country_code, class, type, "
        "hstore_to_json(name) as name, hstore_to_json(extratags) as extra, "
        "COALESCE(address->'housenumber',housenumber) AS housenumber, postcode, ST_X(centroid) as "
        "longitude, ST_Y(centroid) as latitude "
        "from placex ";

  // load primary hierarchy
  {
    pqxx::result r = txn.exec_params(
        base_query
            + "where linked_place_id IS NULL and ST_Intersects(ST_GeomFromGeoJSON($1), "
              "geometry) order by admin_level",
        border);
    size_t count = 0;
    for (const pqxx::row &row : r)
      {
        ++count;
        std::shared_ptr<HierarchyItem> item = std::make_shared<HierarchyItem>(row);
        hierarchy.add_item(item);
        if (count % 10000 == 0)
          std::cout << "Imported records: " << count
                    << "; Root elements: " << hierarchy.get_root_count()
                    << "; Missing parents: " << hierarchy.get_missing_count() << std::endl;
      }
  }

  // load all linked places and merge with the primary ones
  {
    pqxx::result r = txn.exec_params(
        base_query
            + "where linked_place_id IS NOT NULL and ST_Intersects(ST_GeomFromGeoJSON($1), "
              "geometry) order by admin_level",
        border);
    size_t count = 0;
    for (const pqxx::row &row : r)
      {
        ++count;
        std::shared_ptr<HierarchyItem> item = std::make_shared<HierarchyItem>(row);
        hierarchy.add_linked_item(item);
        if (count % 10000 == 0)
          std::cout << "Imported linked records: " << count
                    << "; Root elements: " << hierarchy.get_root_count()
                    << "; Missing parents: " << hierarchy.get_missing_count() << std::endl;
      }
  }

  std::cout << "Fill missing hierarchies. Root size: " << hierarchy.get_root_count() << "\n";
  for (hindex parent = hierarchy.get_next_nonzero_root_parent(); parent;)
    {
      pqxx::result r = txn.exec_params(base_query + "where place_id=$1", parent);
      for (auto row : r)
        {
          std::shared_ptr<HierarchyItem> item = std::make_shared<HierarchyItem>(row);
          hierarchy.add_item(item);
        }

      parent = hierarchy.get_next_nonzero_root_parent();
    }

  std::cout << "Try to fill missing parents through countries. Root size: "
            << hierarchy.get_root_count() << "\n";

  // find missing countries and move root nodes under them if possible
  for (std::string country : hierarchy.get_root_countries())
    {
      for (auto row : txn.exec_params(
               base_query + "where rank_address = 4 and country_code = $1 limit 1", country))
        hierarchy.set_country(country, row["place_id"].as<hindex>(0));
    }

  hierarchy.finalize();

  txn.commit(); // finalize postgres transactions

  hierarchy.print(false);

  // Saving data into SQLite
  sqlite3pp::database db(GeoNLP::Geocoder::name_primary(database_path).c_str());

  db.execute("PRAGMA journal_mode = OFF");
  db.execute("PRAGMA synchronous = OFF");
  db.execute("PRAGMA cache_size = 2000000");
  db.execute("PRAGMA temp_store = 2");
  db.execute("BEGIN TRANSACTION");
  db.execute("DROP TABLE IF EXISTS type");
  db.execute("DROP TABLE IF EXISTS object_primary");
  db.execute("DROP TABLE IF EXISTS object_primary_tmp");
  db.execute("DROP TABLE IF EXISTS object_primary_tmp2");
  db.execute("DROP TABLE IF EXISTS boxids");
  db.execute("DROP TABLE IF EXISTS object_type");
  db.execute("DROP TABLE IF EXISTS object_type_tmp");
  db.execute("DROP TABLE IF EXISTS hierarchy");
  db.execute("DROP TABLE IF EXISTS object_primary_rtree");

  db.execute("CREATE " TEMPORARY " TABLE object_primary_tmp ("
             "id INTEGER PRIMARY KEY AUTOINCREMENT, postgres_id INTEGER, name TEXT, name_extra "
             "TEXT, name_en TEXT, phone TEXT, postal_code TEXT, website TEXT, parent INTEGER, "
             "latitude REAL, longitude REAL)");
  db.execute("CREATE " TEMPORARY " TABLE object_type_tmp (prim_id INTEGER, type TEXT NOT NULL, "
             "FOREIGN KEY (prim_id) REFERENCES objects_primary_tmp(id))");
  db.execute("CREATE TABLE hierarchy (prim_id INTEGER PRIMARY KEY, last_subobject INTEGER, "
             "FOREIGN KEY (prim_id) REFERENCES objects_primary(id), FOREIGN KEY (last_subobject) "
             "REFERENCES objects_primary(id))");

  std::cout << "Preliminary filling of the database" << std::endl;
  hierarchy.write(db);

  // cleanup from duplicated names
  db.execute("UPDATE object_primary_tmp SET name_extra='' WHERE name=name_extra");
  db.execute("UPDATE object_primary_tmp SET name_en='' WHERE name=name_en");

  std::cout << "Reorganizing database tables" << std::endl;

  db.execute("CREATE TABLE type (id INTEGER PRIMARY KEY AUTOINCREMENT, name TEXT)");
  db.execute("INSERT INTO type (name) SELECT DISTINCT type FROM object_type_tmp");
  db.execute("CREATE " TEMPORARY
             " TABLE object_primary_tmp2 (id INTEGER PRIMARY KEY AUTOINCREMENT, "
             "name TEXT, name_extra TEXT, name_en TEXT, phone TEXT, postal_code TEXT, website "
             "TEXT, parent INTEGER, type_id INTEGER, latitude REAL, longitude REAL, boxstr TEXT, "
             "FOREIGN KEY (type_id) REFERENCES type(id))");

  db.execute("INSERT INTO object_primary_tmp2 (id, name, name_extra, name_en, phone, postal_code, "
             "website, parent, type_id, latitude, longitude, boxstr) "
             "SELECT p.id, p.name, p.name_extra, p.name_en, p.phone, p.postal_code, p.website, "
             "p.parent, type.id, p.latitude, p.longitude, "
             // LINE BELOW DETERMINES ROUNDING USED FOR BOXES
             "CAST(CAST(p.latitude*100 AS INTEGER) AS TEXT) || ',' || CAST(CAST(p.longitude*100 AS "
             "INTEGER) AS TEXT) "
             "FROM object_primary_tmp p JOIN object_type_tmp tt ON p.id=tt.prim_id "
             "JOIN type ON tt.type=type.name");

  db.execute("CREATE " TEMPORARY " TABLE boxids (id INTEGER PRIMARY KEY AUTOINCREMENT, boxstr "
             "TEXT, CONSTRAINT struni UNIQUE (boxstr))");
  db.execute("INSERT INTO boxids (boxstr) SELECT DISTINCT boxstr FROM object_primary_tmp2");

  db.execute("CREATE TABLE object_primary (id INTEGER PRIMARY KEY AUTOINCREMENT, name TEXT, "
             "name_extra TEXT, name_en TEXT, phone TEXT, postal_code TEXT, website TEXT, "
             "parent INTEGER, type_id INTEGER, latitude REAL, longitude REAL, box_id INTEGER, "
             "FOREIGN KEY (type_id) REFERENCES type(id))");
  db.execute(
      "INSERT INTO object_primary (id, name, name_extra, name_en, phone, postal_code, website, "
      "parent, type_id, latitude, longitude, box_id) "
      "SELECT o.id, name, name_extra, name_en, phone, postal_code, website, parent, type_id, "
      "latitude, longitude, b.id FROM object_primary_tmp2 o JOIN boxids b ON o.boxstr=b.boxstr");

  db.execute("DROP INDEX IF EXISTS idx_object_primary_box");
  db.execute("CREATE INDEX idx_object_primary_box ON object_primary (box_id)");

  db.execute("DROP INDEX IF EXISTS idx_object_primary_postal_code");
  db.execute("CREATE INDEX idx_object_primary_postal_code ON object_primary (postal_code)");

  std::cout << "Normalize using libpostal" << std::endl;

  normalize_libpostal(db, postal_address_parser_dir, verbose_address_expansion);
  normalized_to_final(db, database_path);

  // Create R*Tree for nearest neighbor search
  std::cout << "Populating R*Tree" << std::endl;
  db.execute(
      "CREATE VIRTUAL TABLE object_primary_rtree USING rtree(id, minLat, maxLat, minLon, maxLon)");
  db.execute("INSERT INTO object_primary_rtree (id, minLat, maxLat, minLon, maxLon) "
             "SELECT box_id, min(latitude), max(latitude), min(longitude), max(longitude) from "
             "object_primary group by box_id");

  // Recording version
  db.execute("DROP TABLE IF EXISTS meta");
  db.execute("CREATE TABLE meta (key TEXT, value TEXT)");
  {
    sqlite3pp::command cmd(db, "INSERT INTO meta (key, value) VALUES (?, ?)");
    std::ostringstream ss;
    ss << GeoNLP::Geocoder::version;
    cmd.binder() << "version" << ss.str().c_str();
    if (cmd.execute() != SQLITE_OK)
      std::cerr << "WriteSQL: error inserting version information\n";
  }

  if (!postal_country_parser.empty())
    {
      std::cout << "Recording postal parser country preference: " << postal_country_parser << "\n";
      std::string cmd = "INSERT INTO meta (key, value) VALUES (\"postal:country:parser\", \""
                        + postal_country_parser + "\")";
      db.execute(cmd.c_str());
    }

  // finalize
  db.execute("END TRANSACTION");
  db.execute("VACUUM");
  db.execute("ANALYZE");

  std::cout << "Done\n";

  return 0;
}
