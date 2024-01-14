#include "geocoder.h"
#include "postal.h"
#include "version.h"

#include <boost/program_options.hpp>
#include <iomanip>
#include <iostream>

using namespace GeoNLP;
namespace po = boost::program_options;

int main(int argc, char *argv[])
{
  Postal postal;
  // postal.set_initialize_every_call(true);

  std::string query;
  std::string postal_data_global;
  std::string postal_data_country;
  std::string geocoder_data;
  int         max_results = 10;
  double      ref_latitude;
  double      ref_longitude;
  int         ref_zoom       = 16;
  double      ref_importance = 0.75;

  Geocoder::GeoReference reference;

  {
    po::options_description generic("Geocoder NLP demo options");
    generic.add_options()("help,h", "Help message");
    generic.add_options()("version,v", "Version");
    generic.add_options()("geocoder-data", po::value<std::string>(&geocoder_data),
                          "GeocoderNLP database directory path");

    generic.add_options()(
        "postal-country", po::value<std::string>(&postal_data_country),
        "libpostal country database. Keep empty to use global libpostal parser data.");
    generic.add_options()(
        "postal-global", po::value<std::string>(&postal_data_global),
        "libpostal global database. Keep empty to use global libpostal parser data.");

    generic.add_options()("max-results", po::value<int>(&max_results), "Maximal number of results");

    generic.add_options()("ref-latitude", po::value<double>(&ref_latitude),
                          "Reference for location bias; latitude");
    generic.add_options()("ref-longitude", po::value<double>(&ref_longitude),
                          "Reference for location bias; longitude");
    generic.add_options()(
        "ref-zoom", po::value<int>(&ref_zoom),
        "Reference for location bias; zoom level for calculating reference radius");
    generic.add_options()("ref-importance", po::value<double>(&ref_importance),
                          "Reference for location bias; importance from 0 to 1 of location bias");

    po::options_description hidden("Hidden options");
    hidden.add_options()("query", po::value<std::string>(&query), "Search query");

    po::positional_options_description p;
    p.add("query", 1);

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
        std::cout << "Geocoder NLP demo:\n\n"
                  << "Call as\n\n " << argv[0] << " <options> query\n"
                  << "\nwhere query is a string.\n\n"
                  << generic << "\n";
        return 0;
      }

    if (vm.count("version"))
      {
        std::cout << "Geocoder NLP version: " << GEOCODERNLP_VERSION_STRING << "\n";
        return 0;
      }

    if (!vm.count("geocoder-data"))
      {
        std::cerr << "GeocoderNLP database directory path is missing\n";
        return -1;
      }

    if (vm.count("ref-latitude") && vm.count("ref-longitude"))
      reference.set(ref_latitude, ref_longitude, ref_zoom, ref_importance);
  }

  std::vector<Postal::ParseResult> parsed_query;
  Postal::ParseResult              nonorm;

  postal.set_postal_datadir(postal_data_global, postal_data_country);
  // postal.add_language("da");
  // postal.add_language("et");
  // postal.add_language("en");
  postal.set_initialize_every_call(true);
  postal.set_use_primitive(false);

  postal.parse(query, parsed_query, nonorm);

  std::cout << "\nAddress parsing before full normalization:\n\n";
  for (auto v : nonorm)
    {
      std::cout << v.first << " ";
      for (auto k : v.second)
        std::cout << k << " ";
      std::cout << "\n";
    }
  std::cout << "\n";

  std::cout << "Normalization:\n\n";
  for (auto r : parsed_query)
    {
      for (auto v : r)
        {
          std::cout << v.first << " ";
          for (auto k : v.second)
            std::cout << k << " ";
          std::cout << "\n";
        }
      std::cout << "\n";
    }

  std::cout << std::endl;

  Geocoder geo(geocoder_data);
  geo.set_max_queries_per_hierarchy(30);
  geo.set_max_results(max_results);
  // geo.set_result_language("en");

  std::vector<Geocoder::GeoResult> result;

  std::cout << "Geocoder loaded" << std::endl;

  geo.search(parsed_query, result, 0, reference);

  std::cout << std::setprecision(8);
  std::cout << "Search results: \n\n";
  size_t counter = 0;
  for (const Geocoder::GeoResult &r : result)
    {
      std::cout << r.title << "\n"
                << r.address << "\n"
                << "Postal code: " << r.postal_code << "\n"
                << "Phone: " << r.phone << " / URL: " << r.website << "\n"
                << r.latitude << ", " << r.longitude << " / distance=" << r.distance << "\n"
                << r.type << " / " << r.id << " / " << r.search_rank << " / " << r.levels_resolved
                << "\n\n";
      counter++;
    }

  std::cout << "Number of results: " << counter << std::endl;

  return 0;
}
