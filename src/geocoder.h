#ifndef GEOCODER_H
#define GEOCODER_H

#include "postal.h"

#include <sqlite3pp.h>

#include <vector>
#include <string>

namespace GeoNLP {

  class Geocoder
  {

  public:
    struct GeoResult {
      long long int id;
      double latitude;
      double longitude;
      std::string title;
      std::string address;
      std::string type;
      size_t levels_resolved;
    };

  public:
    Geocoder();
    Geocoder(const std::string &dbpath);

    virtual ~Geocoder() {}

    bool search(const std::vector< Postal::ParseResult > &parsed_query, std::vector<GeoResult> &result);

    int get_levels_in_title() const { return m_levels_in_title; }
    void set_levels_in_title(int l) { m_levels_in_title = l; }

    size_t get_max_queries_per_hierarchy() const { return m_max_queries_per_hierarchy; }
    void set_max_queries_per_hierarchy(size_t mx) { m_max_queries_per_hierarchy = mx; }

    size_t get_max_results() const { return m_max_results; }
    void set_max_results(size_t mx) { m_max_results = mx; }

    bool load(const std::string &dbpath);
    bool load();
    void drop();

    operator bool() const { return m_database_open; }

  protected:
    bool search(const std::vector<std::string> &parsed, std::vector<GeoResult> &result, size_t level=0,
                long long int range0=0, long long int range1=0);

    void get_name(long long int id, std::string &title, std::string &full, int levels_in_title);

    std::string get_type(long long int id);

    virtual bool check_version();

    bool check_version(const char* supported);
    
  protected:
    sqlite3pp::database m_db;
    std::string m_database_path;
    bool m_database_open = false;
    
    int m_levels_in_title = 2;
    size_t m_max_queries_per_hierarchy = 0;
    size_t m_max_results = 0;

    size_t m_levels_resolved;
    size_t m_query_count;
  };

}
#endif // GEOCODER_H
