#include "geocoder.h"

#include <sstream>
#include <deque>
#include <iostream>

using namespace GeoNLP;

Geocoder::Geocoder()
{

}

Geocoder::Geocoder(const std::string &dbname)
{
    if ( !load(dbname) )
        std::cerr << "Geocoder: error loading " << dbname << std::endl;
}

bool Geocoder::load(const std::string &dbname)
{
    return (m_db.connect(dbname.c_str(), SQLITE_OPEN_READONLY) == SQLITE_OK);
}

void Geocoder::drop()
{
    m_db.disconnect();
}

void Geocoder::search(const std::vector<Postal::ParseResult> &parsed_query, std::vector<Geocoder::GeoResult> &result)
{
    // parse query by libpostal
    std::vector< std::vector<std::string> > parsed_result;
    Postal::result2hierarchy(parsed_query, parsed_result);

    result.clear();
    m_min_missing_levels = -1;

    std::cout << "Search hierarchies:\n";
    for (const auto &r: parsed_result)
    {
        for (auto a: r)
            std::cout << a << " / ";
        std::cout << "\n\n";

        m_query_count = 0;
        search(r, result);
    }

    // fill the data
    for (GeoResult &r: result)
    {
        get_name(r.id, r.title, r.address, m_levels_in_title);

        sqlite3pp::query qry(m_db, "SELECT latitude, longitude FROM object_primary WHERE id=?");
        qry.bind(1, r.id);
        for (auto v: qry)
        {
            // only one entry is expected
            v.getter() >> r.latitude >> r.longitude;
            break;
        }
    }
}

bool Geocoder::search(const std::vector<std::string> &parsed, std::vector<Geocoder::GeoResult> &result, size_t level,
                      long long int range0, long long int range1)
{
    if ( level >= parsed.size() || (m_max_queries_per_hierarchy>0 && m_query_count > m_max_queries_per_hierarchy) )
        return false;

    m_query_count++;

    std::string extra;
    if (level > 0)
        extra = "AND prim_id>? AND prim_id<=?";

    std::deque<long long int> ids;

    std::string command =
            "SELECT prim_id FROM normalized_name WHERE name GLOB \"" + parsed[level] +
            "*\" " + extra + " ORDER BY length(name)";

#ifdef GEONLP_PRINT_DEBUG
    std::cout << level << " " << command << "\n";
#endif
    sqlite3pp::query qry(m_db, command.c_str());
    if (level > 0)
    {
        qry.bind(1, range0);
        qry.bind(2, range1);
    }

    for (auto v : qry)
    {
        long long int id;
        v.getter() >> id;
        ids.push_back(id);
    }

    for (long long int id: ids)
    {
        long long int r1 = -1;
        {
            sqlite3pp::query qry(m_db, "SELECT last_subobject FROM hierarchy WHERE prim_id=?");
            qry.bind(1, id);
            for (auto v : qry )
            {
                v.getter() >> r1;
                break; // only one value is expected
            }
        }

        if ( r1 <= id || !search(parsed, result, level+1, id, r1) )
        {
            int missing = parsed.size() - level - 1;
            if ( m_min_missing_levels<0 || missing < m_min_missing_levels )
            {
                result.clear();
                m_min_missing_levels = missing;
            }

            if (m_min_missing_levels == missing)
            {
                bool have_already = false;
                for (const auto &r: result)
                    if (r.id == id)
                    {
                        have_already = true;
                        break;
                    }

                if (!have_already)
                {
                    GeoResult r;
                    r.id = id;
                    r.levels_missing = missing;
                    result.push_back(r);
                }
            }
        }
    }

    return ids.size() > 0;
}

void Geocoder::get_name(long long id, std::string &title, std::string &full, int levels_in_title)
{
    long long int parent;
    std::string name;

    sqlite3pp::query qry(m_db, "SELECT name, parent FROM object_primary WHERE id=?");
    qry.bind(1, id);
    for (auto v: qry)
    {
        // only one entry is expected
        v.getter() >> name >> parent;

        if (!full.empty()) full += ", ";
        full += name;

        if (levels_in_title > 0)
        {
            if (!title.empty()) title += ", ";
            title += name;
        }

        get_name(parent, title, full, levels_in_title-1);
        return;
    }
}
