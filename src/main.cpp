#include "postal.h"
#include "geocoder.h"

#include <iostream>
#include <iomanip>

using namespace GeoNLP;

int main(int argc, char *argv[])
{
    Postal postal;
    postal.set_initialize_every_call(true);

    if (argc < 3)
    {
        std::cout << "Use: " << argv[0] << " sqlite.dbase address\n"
                  << "where\n"
                  << " sqlite.dbase - path to SQLite database\n"
                  << " address - address to be parsed (please enclose it in \" \" to ensure that its a singe argument\n";
        return -1;
    }

    char *query = argv[2];
    std::vector< Postal::ParseResult > parsed_query;
    Postal::ParseResult nonorm;

    postal.parse(query, parsed_query, nonorm);

    std::cout << "\nAddress parsing before full normalization:\n\n";
    for (auto v: nonorm)
        std::cout << v.first << " " << v.second << "\n";
    std::cout << "\n";

    std::cout << "Normalization:\n\n";
    for (auto r: parsed_query)
    {
        for (auto v: r)
            std::cout << v.first << " " << v.second << "\n";
        std::cout << "\n";
    }

    Geocoder geo(argv[1]);
    geo.set_max_queries_per_hierarchy(25);

    std::vector<Geocoder::GeoResult> result;

    geo.search(parsed_query, result);

    std::cout << std::setprecision(8);
    std::cout << "Search results: \n\n";
    size_t counter = 0;
    for (const Geocoder::GeoResult &r: result)
    {
        std::cout << r.title << "\n"
                  << r.address << "\n"
                  << r.latitude << ", " << r.longitude << "\n"
                  << r.id << " / " << r.levels_missing << "\n\n";
        counter++;
    }

    std::cout << "Number of results: " << counter << std::endl;

    return 0;
}
