/////////////////////////////////////////////////////////////////////
/// WARNING: When adding new languages, increase the language count
/// num_languages in geocoder.c
/////////////////////////////////////////////////////////////////////

#include "geocoder.h"

#include "hierarchy.h"

#include <libpostal/libpostal.h>
#include <nlohmann/json.hpp>
#include <pqxx/pqxx>
#include <sqlite3pp.h>

#include <kchashdb.h>
#include <marisa.h>

#include <algorithm>
#include <cctype>
#include <cstdlib>
#include <deque>
#include <fstream>
#include <iterator>
#include <locale>
#include <map>
#include <set>

#include <boost/tokenizer.hpp>

/// if there are more expansions that specified, this object will be dropped from normalization
/// table
#define MAX_NUMBER_OF_EXPANSIONS 85

/// starting from this length, check wheher the string is suspicious
#define LENGTH_STARTING_SUSP_CHECK 200

#define MAX_COMMAS 10 /// maximal number of commas allowed in a name

#define TEMPORARY "TEMPORARY" // set to empty if need to debug import

#define GEOCODER_IMPORTER_POSTGRES "GEOCODER_IMPORTER_POSTGRES"

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

  // load GeoJSON for surrounding (multi)polygon from stdin
  std::istreambuf_iterator<char> begin(std::cin), end;
  std::string                    border(begin, end);

  if (border.size())
    {
      json j = json::parse(border);
      border = j["geometry"].dump();
      std::cout << "Loaded border GeoJSON. Geometry string length: " << border.size() << "\n";
    }
  else
    {
      std::cout << "No border polygon given\n";
      return 0;
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

  // for (auto r : txn.exec("select hstore_to_json(name) as name from placex where name is not "
  //                        "null limit 100"))
  //   {
  //     json j = json::parse(r["name"].as<std::string>());
  //     for (auto v : j.items())
  //       std::cout << v.key() << " --> " << v.value() << "\n";
  //   }

  // return 0;

  const std::string base_query
      = "select place_id, linked_place_id, parent_place_id, country_code, class, type, "
        "hstore_to_json(name) as name, hstore_to_json(extratags) as extra, "
        "housenumber, postcode, ST_X(centroid) as longitude, ST_Y(centroid) as latitude "
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

  txn.commit(); // finalize transactions

  hierarchy.print(true);
  return 0;
}
