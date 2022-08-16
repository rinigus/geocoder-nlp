#include "geocoder.h"
#include "postal.h"

#include <fstream>
#include <iomanip>
#include <iostream>
#include <stdlib.h>

using namespace GeoNLP;

int main(int argc, char *argv[])
{
  Postal postal;

  if (argc < 8)
    {
      std::cout << "Use: " << argv[0] << " dbase postal_global postal_country address\n"
                << "where\n"
                << " dbase - path to Geocoder-NLP database folder\n"
                << " postal_global - path to libpostal database with language classifier\n"
                << " postal_country - path to libpostal  database covering country\n"
                << " point - text file containing lat lon coordinate, the first pair is read only\n"
                << " radius - distance in meters\n"
                << " type - type of POI\n"
                << " name - name of POI\n";
      return -1;
    }

  double radius = atof(argv[5]);
  std::cout << "Search radius: " << radius << " meters\n";

  std::vector<double> latitude, longitude;
  {
    std::ifstream fin(argv[4]);
    double        lat, lon;
    while (fin >> lat >> lon)
      {
        latitude.push_back(lat);
        longitude.push_back(lon);
        break;
      }
  }
  std::cout << "Loaded " << latitude.size() << " coordinates from " << argv[4] << "\n";

  char                    *name_query = argv[7];
  std::vector<std::string> parsed_name;

  if (strlen(name_query) > 0)
    {
      postal.set_postal_datadir(argv[2], argv[3]);
      postal.set_initialize_every_call(true);
      postal.expand_string(name_query, parsed_name);

      std::cout << "Normalization of name:\n\n";
      for (auto r : parsed_name)
        std::cout << r << "\n";
    }

  std::cout << std::endl;

  std::vector<std::string> type_query;
  if (strlen(argv[6]) > 0)
    {
      type_query.push_back(argv[6]);
      std::cout << "Searching for type: " << argv[6] << "\n";
    }

  Geocoder geo(argv[1]);
  geo.set_max_queries_per_hierarchy(30);
  geo.set_max_results(25);

  std::vector<Geocoder::GeoResult> result;

  std::cout << "Geocoder loaded" << std::endl;

  if (!geo.search_nearby(parsed_name, type_query, latitude[0], longitude[0], radius, result,
                         postal))
    std::cout << "Failed to search\n";
  else
    {
      std::cout << std::setprecision(8);
      std::cout << "Search results: \n\n";
      size_t counter = 0;
      for (const Geocoder::GeoResult &r : result)
        {
          std::cout << r.title << "\n"
                    << r.address << "\n"
                    << r.latitude << ", " << r.longitude << ": distance - " << r.distance << "\n"
                    << r.type << " / " << r.id << " / " << r.admin_levels << " / "
                    << r.levels_resolved << "\n\n";
          counter++;
        }

      std::cout << "Number of results: " << counter << std::endl;
    }

  return 0;
}
