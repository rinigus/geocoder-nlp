#ifndef GEOCODER_H
#define GEOCODER_H

#include "postal.h"

#include <sqlite3pp.h>
#include <marisa.h>
#include <kchashdb.h>

#include <vector>
#include <string>
#include <cctype>

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

    typedef uint32_t index_id_key;
    typedef uint32_t index_id_value;

  public:
    Geocoder();
    Geocoder(const std::string &dbpath);

    virtual ~Geocoder() {}

    /// \brief Search for any objects matching the normalized query
    ///
    bool search(const std::vector< Postal::ParseResult > &parsed_query, std::vector<GeoResult> &result, size_t min_levels=0);

    /// \brief Search for objects within given radius from specified point and matching the query
    ///
    /// Here, radius is given in meters and the reference point is
    /// given by latitude and longitude (WGS 84)
    bool search_nearby( const std::vector< std::string > &name_query,
                        const std::string &type_query,
                        double latitude, double longitude,
                        double radius,
                        std::vector<GeoResult> &result,
                        Postal &postal );
    
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

  public:
    static std::string name_primary(const std::string &dname);
    static std::string name_normalized_trie(const std::string &dname);
    static std::string name_normalized_id(const std::string &dname);

    // interaction with key/value database
    static std::string make_id_key(index_id_key key)
    {
      return std::string( (char*)&key, sizeof(key) );
    }
    
    static std::string make_id_value(const std::vector<index_id_value> v)
    {
      return std::string( (char*)v.data(), sizeof(index_id_value) * v.size() );
    }

    static index_id_key get_id_key(const std::string &v)
    {
      return *( (index_id_key*)v.data() );
    }

    static index_id_value get_id_value(const std::string &v, size_t index)
    {
      index_id_value *p = (index_id_value*)v.data();
      return p[index];
    }

    static size_t get_id_number_of_values(const std::string &v)
    {
      return v.size() / sizeof(index_id_value);
    }

    static bool get_id_range(std::string &v, bool full_range, index_id_value range0, index_id_value range1,
                             index_id_value* *idx0, index_id_value* *idx1);

  protected:
    bool search(const Postal::Hierarchy &parsed, std::vector<GeoResult> &result, size_t level=0,
                long long int range0=0, long long int range1=0);

    void get_name(long long int id, std::string &title, std::string &full, int levels_in_title);

    std::string get_type(long long int id);

    virtual bool check_version();

    bool check_version(const char* supported);
    
  protected:
    sqlite3pp::database m_db;
    std::string m_database_path;
    bool m_database_open = false;

    kyotocabinet::HashDB m_database_norm_id;
    marisa::Trie m_trie_norm;
    
    int m_levels_in_title = 2;
    size_t m_max_queries_per_hierarchy = 0;
    size_t m_max_results = 0;

    size_t m_levels_resolved;
    size_t m_query_count;
  };

}
#endif // GEOCODER_H
