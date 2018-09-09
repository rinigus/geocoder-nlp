#include "postal.h"
#include "geocoder.h"

#include <iostream>
#include <iomanip>

using namespace GeoNLP;

int main(int argc, char *argv[])
{
  Postal postal;
  //postal.set_initialize_every_call(true);

  if (argc < 5)
    {
      std::cout << "Use: " << argv[0] << " dbase postal_global postal_country address\n"
		<< "where\n"
		<< " dbase - path to Geocoder-NLP database folder\n"
		<< " postal_global - path to libpostal database with language classifier\n"
		<< " postal_country - path to libpostal  database covering country\n"
		<< " address - address to be parsed (please enclose it in \" \" to ensure that its a singe argument)\n";
      return -1;
    }

  char *query = argv[4];
  std::vector< Postal::ParseResult > parsed_query;
  Postal::ParseResult nonorm;

  postal.set_postal_datadir(argv[2], argv[3]);
  // postal.add_language("da");
  // postal.add_language("et");
  // postal.add_language("en");
  postal.set_initialize_every_call(true);
  
  postal.parse(query, parsed_query, nonorm);

  std::cout << "\nAddress parsing before full normalization:\n\n";
  for (auto v: nonorm)
    {
      std::cout << v.first << " ";
      for (auto k: v.second) std::cout << k << " ";
      std::cout << "\n";
    }
  std::cout << "\n";

  std::cout << "Normalization:\n\n";
  for (auto r: parsed_query)
    {
      for (auto v: r)
        {
          std::cout << v.first << " ";
          for (auto k: v.second) std::cout << k << " ";
          std::cout << "\n";
        }
      std::cout << "\n";
    }

  std::cout << std::endl;
  
  Geocoder geo(argv[1]);
  geo.set_max_queries_per_hierarchy(30);
  geo.set_max_results(25);
  //geo.set_result_language("en");

  std::vector<Geocoder::GeoResult> result;

  std::cout << "Geocoder loaded" << std::endl;
  
  geo.search(parsed_query, result);

  std::cout << std::setprecision(8);
  std::cout << "Search results: \n\n";
  size_t counter = 0;
  for (const Geocoder::GeoResult &r: result)
    {
      std::cout << r.title << "\n"
		<< r.address << "\n"
                << "Postal code: " << r.postal_code << "\n"
                << "Phone: " << r.phone << " / URL: " << r.website << "\n" 
                << r.latitude << ", " << r.longitude << "\n"
		<< r.type << " / " << r.id << " / " << r.admin_levels << " / " << r.levels_resolved << "\n\n";
      counter++;
    }

  std::cout << "Number of results: " << counter << std::endl;

  return 0;
}
