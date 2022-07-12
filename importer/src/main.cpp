/////////////////////////////////////////////////////////////////////
/// WARNING: When adding new languages, increase the language count
/// num_languages in geocoder.c
/////////////////////////////////////////////////////////////////////

#include "config.h"
#include "geocoder.h"
#include "hierarchy.h"
#include "normalization.h"

#include <algorithm>
#include <boost/program_options.hpp>
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

using json   = nlohmann::json;
namespace po = boost::program_options;

////////////////////////////////////////////////////////////////////////////
// MAIN

int main(int argc, char *argv[])
{
  std::string polyjson;
  std::string database_path;
  std::string postal_country_parser;
  std::string postal_address_parser_dir;
  std::string type_priority_list;
  std::string type_skip_list;
  bool        verbose_address_expansion = false;

  {
    po::options_description generic("Geocoder NLP importer options");
    generic.add_options()("help,h", "Help message")("version,v", "Data format version");
    generic.add_options()("poly,p", po::value<std::string>(&polyjson),
                          "Boundary of the imported region in GeoJSON format");
    generic.add_options()("postal-country", po::value<std::string>(&postal_country_parser),
                          "libpostal country preference for this database");
    generic.add_options()(
        "postal-address", po::value<std::string>(&postal_address_parser_dir),
        "libpostal address parser directory. If not specified, global libpostal parser directory "
        "preference is used.");
    generic.add_options()(
        "priority", po::value<std::string>(&type_priority_list),
        "File with OSM tags that are kept even if there is no name associated with the location");
    generic.add_options()(
        "skip", po::value<std::string>(&type_skip_list),
        "File with OSM tags for locations that should be dropped even if there is a name "
        "associated with the location");
    generic.add_options()("verbose", "Verbose address expansion");

    po::options_description hidden("Hidden options");
    hidden.add_options()("output-directory", po::value<std::string>(&database_path),
                         "Output directory for imported database");

    po::positional_options_description p;
    p.add("output-directory", 1);

    po::options_description cmdline_options;
    cmdline_options.add(generic).add(hidden);

    po::variables_map vm;
    try
      {
        po::store(po::command_line_parser(argc, argv).options(cmdline_options).positional(p).run(),
                  vm);
        po::notify(vm);
      }
    catch (std::exception &e)
      {
        std::cerr << "Error while parsing options: " << e.what() << "\n\n";
        std::cerr << generic << "\n";
      }

    if (vm.count("help"))
      {
        std::cout << "Geocoder NLP importer:\n\n"
                  << "Call as\n\n " << argv[0] << " <options> output-directory\n"
                  << "\nwhere output-directory is a directory for imported database.\n\n"
                  << generic << "\n";
        return 0;
      }

    if (vm.count(("version")))
      {
        std::cout << GeoNLP::Geocoder::version << "\n";
        return 0;
      }

    if (vm.count("verbose"))
      verbose_address_expansion = true;

    if (!vm.count("poly"))
      {
        std::cerr << "Boundary of the imported region in GeoJSON format is missing\n";
        return -1;
      }
  }

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

  HierarchyItem::load_priority_list(type_priority_list);
  HierarchyItem::load_skip_list(type_skip_list);

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

  // find missing parents for root nodes
  std::cout << "Fill missing hierarchies. Root size: " << hierarchy.get_root_count() << "\n";
  for (hindex parent = hierarchy.get_next_nonzero_root_parent(); parent;)
    {
      pqxx::result r     = txn.exec_params(base_query + "where place_id=$1", parent);
      bool         found = false;
      for (auto row : r)
        {
          std::shared_ptr<HierarchyItem> item = std::make_shared<HierarchyItem>(row);
          hierarchy.add_item(item);
          found = true;
        }

      if (!found)
        {
          std::cerr << "Missing parent with ID " << parent << ". Stopping import\n";
          return -1;
        }

      parent = hierarchy.get_next_nonzero_root_parent();
    }

  // remove all items from hierarchy that are not supposed to be there
  std::cout << "Cleanup hierarchy\n";
  hierarchy.cleanup();

  // find missing countries and move root nodes under them if possible
  std::cout << "Try to fill missing parents through countries. Root size: "
            << hierarchy.get_root_count() << "\n";
  for (std::string country : hierarchy.get_root_countries())
    {
      for (auto row : txn.exec_params(
               base_query + "where rank_address = 4 and country_code = $1 limit 1", country))
        hierarchy.set_country(country, row["place_id"].as<hindex>(0));
    }

  hierarchy.finalize();

  txn.commit(); // finalize postgres transactions

  // hierarchy.print(false);

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

  // Stats view
  db.execute("DROP VIEW IF EXISTS type_stats");
  db.execute(
      "CREATE VIEW type_stats AS SELECT t.name as type_name, COUNT(*) AS cnt FROM object_primary o "
      "JOIN \"type\" t ON t.id = o.type_id GROUP BY t.name ORDER BY cnt desc");
  {
    std::cout << "List of most popular imported types\n";
    sqlite3pp::query qry(db, "SELECT type_name, cnt FROM type_stats ORDER BY cnt DESC LIMIT 25");
    for (auto v : qry)
      {
        std::string name;
        int         cnt;
        v.getter() >> name >> cnt;
        std::cout << " " << name << "\t" << cnt << "\n";
      }
  }
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
