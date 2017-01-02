#include "postal.h"
#include "geocoder.h"

#include <iostream>
#include <iomanip>

using namespace GeoNLP;

int main(int argc, char *argv[])
{
    Postal postal;
    if (argc < 3)
    {
        std::cout << "Use: " << argv[0] << " sqlite.dbase address\n"
                  << "where\n"
                  << " sqlite.dbase - path to SQLite database\n"
                  << " address - address to be parsed (please enclose it in \" \" to ensure that its a singe argument\n";
        return -1;
    }

    std::vector< Postal::ParseResult > parsed_query;
    Postal::ParseResult nonorm;

    postal.parse(argv[2], parsed_query, nonorm);

    std::cout << "Address parsing before normalization:\n";
    for (auto v: nonorm)
        std::cout << v.first << " " << v.second << "\n";
    std::cout << "\n";

    std::cout << "Normalization:\n";
    for (auto r: parsed_query)
    {
        for (auto v: r)
            std::cout << v.first << " " << v.second << "\n";
        std::cout << "\n";
    }

    Geocoder geo(argv[1]);
    std::vector<Geocoder::GeoResult> result;

    geo.search(parsed_query, result);

    std::cout << std::setprecision(8);
    std::cout << "Search results: \n\n";
    for (const Geocoder::GeoResult &r: result)
    {
        std::cout << r.title << "\n"
                  << r.address << "\n"
                  << r.latitude << ", " << r.longitude << "\n"
                  << r.id << " / " << r.levels_missing << "\n\n";
    }


    return 0;
}
