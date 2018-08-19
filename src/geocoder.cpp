#include "geocoder.h"

#include <sstream>
#include <deque>
#include <set>
#include <iostream>
#include <algorithm>

// #include <boost/geometry.hpp>

using namespace GeoNLP;

const int GeoNLP::Geocoder::version{4};
const size_t GeoNLP::Geocoder::num_languages{2}; // 1 (default) + 1 (english)

// typedef boost::geometry::model::point< double, 2, boost::geometry::cs::spherical_equatorial<boost::geometry::degree> > point_t;

////////////////////
// helper functions

static double distance_per_latitude() { return 111e3; }
static double distance_per_longitude(double latitude) { return std::max(1000.0, M_PI/180.0 * 6378137.0 * cos(latitude*M_PI/180.0)); }

////////////////////
// Geocoder class

Geocoder::Geocoder()
{
  update_limits();
}

std::string Geocoder::name_primary(const std::string &dname)
{
  return dname + "/geonlp-primary.sqlite";
}

std::string Geocoder::name_normalized_trie(const std::string &dname)
{
  return dname + "/geonlp-normalized.trie";
}

std::string Geocoder::name_normalized_id(const std::string &dname)
{
  return dname + "/geonlp-normalized-id.kch";
}

Geocoder::Geocoder(const std::string &dbname)
{
  if ( !load(dbname) )
    std::cerr << "Geocoder: error loading " << dbname << std::endl;
}

bool Geocoder::load(const std::string &dbname)
{
  if (dbname == m_database_path && m_database_open) return true;

  // clean before loading anything
  drop();

  bool error = false;
  try
  {
    m_database_path = dbname;
    if ( m_db.connect(name_primary(m_database_path).c_str(),
                      SQLITE_OPEN_READONLY) != SQLITE_OK )
      {
        error = true;
        std::cerr << "Error opening SQLite database\n";
      }

    if ( !error && !check_version() )
      {
        error = true;
      }

    // Limit Kyoto Cabinet caches
    m_database_norm_id.tune_map(32LL*1024LL*1024LL);        // 64MB default
    //m_database_norm_id.tune_page_cache(32LL*1024LL*1024LL); // 64MB default

    if ( !error &&
         !m_database_norm_id.open(name_normalized_id(m_database_path).c_str(),
                                  kyotocabinet::HashDB::OREADER | kyotocabinet::HashDB::ONOLOCK ) )
      {
        error = true;
        std::cerr << "Error opening IDs database\n";
      }

    if ( !error )
      m_trie_norm.load(name_normalized_trie(m_database_path).c_str()); // throws exception on error

    m_database_open = true;
  }
  catch (sqlite3pp::database_error e)
  {
    error = true;
    std::cerr << "Geocoder SQLite exception: " << e.what() << std::endl;
  }
  catch (marisa::Exception e)
  {
    error = true;
    std::cerr << "Geocoder MARISA exception: " << e.what() << std::endl;
  }

  if (error) drop();
  return !error;
}

bool Geocoder::load()
{
  return load( m_database_path );
}

void Geocoder::drop()
{
  m_db.disconnect();
  m_database_norm_id.close();
  m_trie_norm.clear();
  m_database_path = std::string();
  m_database_open = false;
}

bool Geocoder::check_version()
{
  std::ostringstream s;
  s << Geocoder::version;
  return check_version(s.str());
}

bool Geocoder::check_version(const std::string &supported)
{
  // this cannot through exceptions
  try
  {
    sqlite3pp::query qry(m_db, "SELECT value FROM meta WHERE key=\"version\"");

    for (auto v: qry)
      {
        std::string n;
        v.getter() >> n;
        if ( n == supported ) return true;
        else
          {
            std::cerr << "Geocoder: wrong version of the database. Supported: " << supported << " / database version: " << n << std::endl;
            return false;
          }
      }
  }
  catch (sqlite3pp::database_error e)
  {
    std::cerr << "Geocoder exception while checking database version: " << e.what() << std::endl;
    return false;
  }

  return false;
}

void Geocoder::update_limits()
{
  m_max_inter_results = m_max_results + m_max_inter_offset;
}

#ifdef GEONLP_PRINT_DEBUG
static std::string v2s(const std::vector<std::string> &v)
{
  std::string s = "{";
  for (auto i: v)
    {
      if (s.length() > 1) s += ", ";
      s += i;
    }
  s += "}";
  return s;
}
#endif

bool Geocoder::search(const std::vector<Postal::ParseResult> &parsed_query, std::vector<Geocoder::GeoResult> &result, size_t min_levels)
{
  if (!m_database_open)
    return false;

  // parse query by libpostal
  std::vector< Postal::Hierarchy > parsed_result;
  Postal::result2hierarchy(parsed_query, parsed_result);


  result.clear();
  m_levels_resolved = min_levels;

#ifdef GEONLP_PRINT_DEBUG
  std::cout << "Search hierarchies:\n";
#endif
#ifdef GEONLP_PRINT_DEBUG_QUERIES
  std::cout << "\n";
#endif

  try { // catch and process SQLite and other exceptions
    for (const auto &r: parsed_result)
      {
#ifdef GEONLP_PRINT_DEBUG
        for (auto a: r)
          std::cout << v2s(a) << " / ";
        std::cout << "\n";
#endif

        m_query_count = 0;
        if ( r.size() >= m_levels_resolved ||
             (r.size() == m_levels_resolved && (m_max_results==0 || result.size() < m_max_inter_results)) )
          search(r, result);
#ifdef GEONLP_PRINT_DEBUG_QUERIES
        else
          std::cout << "Skipping hierarchy since search result already has more levels than provided\n";
#endif
#ifdef GEONLP_PRINT_DEBUG_QUERIES
        std::cout << "\n";
#endif
      }

#ifdef GEONLP_PRINT_DEBUG
    std::cout << "\n";
#endif

    // fill the data
    for (GeoResult &r: result)
      {
        get_name(r.id, r.title, r.address, r.admin_levels, m_levels_in_title);
        r.type = get_type(r.id);

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
  catch (sqlite3pp::database_error e)
  {
    std::cerr << "Geocoder exception: " << e.what() << std::endl;
    return false;
  }

  // sort and trim results
  std::sort( result.begin(), result.end() );
  if ( m_max_results > 0 && result.size() >=  m_max_results )
    result.resize(m_max_results);

  return true;
}


bool Geocoder::search(const Postal::Hierarchy &parsed,
                      std::vector<Geocoder::GeoResult> &result, size_t level,
                      long long int range0, long long int range1)
{
  /// Number of allowed queries are scaled by the number of
  /// languages. Otherwise, with the languages added, one can start
  /// missing search results.
  if ( level >= parsed.size() ||
       (m_max_queries_per_hierarchy>0 && m_query_count > m_max_queries_per_hierarchy * num_languages) )
    return false;

  m_query_count++;

  std::set<long long int> ids_explored; /// keeps all ids which have been used to search further at this level

  // help structure keeping marisa-found search string with found ids
  struct IntermediateResult
  {
    std::string txt;
    index_id_value id;
    IntermediateResult(const std::string &t, index_id_value i): txt(t), id(i) {}
    bool operator<(const IntermediateResult &A) const
    { return ( txt.length() < A.txt.length() || (txt.length() == A.txt.length() && txt<A.txt) || (txt==A.txt && id<A.id) ); }
  };

  std::deque<IntermediateResult> search_result;
  for (const std::string s: parsed[level])
    {
      marisa::Agent agent;
      agent.set_query(s.c_str());
      while (m_trie_norm.predictive_search(agent))
        {
          std::string val;
          if ( m_database_norm_id.get( make_id_key( agent.key().id() ), &val) )
            {
              index_id_value *idx, *idx1;
              if ( get_id_range(val, (level==0), range0, range1,
                                &idx, &idx1) )
                {
                  for (; idx < idx1; ++idx)
                    {
                      long long int id = *idx;
                      IntermediateResult r( std::string(agent.key().ptr(), agent.key().length()),
                                            id );
                      search_result.push_back(r);
                    }
                }
            }
          else
            {
              std::cerr << "Internal inconsistency of the databases: TRIE " << agent.key().id() << "\n";
            }
        }
    }

  std::sort(search_result.begin(), search_result.end());

  bool last_level = ( level+1 >= parsed.size() );
  for (const IntermediateResult &branch: search_result)
    {
      long long int id = branch.id;
      long long int last_subobject = id;

      if (ids_explored.count(id) > 0)
        continue; // has been looked into it already

      if (parsed.size() < m_levels_resolved || (parsed.size()==m_levels_resolved && m_max_results > 0 && result.size() >= m_max_inter_results))
        break; // this search cannot add more results

      ids_explored.insert(id);

      // are we interested in this result even if it doesn't have subregions?
      if (!last_level)
        {
          sqlite3pp::query qry(m_db, "SELECT last_subobject FROM hierarchy WHERE prim_id=?");
          qry.bind(1, id);
          for (auto v: qry)
            {
              // only one entry is expected
              v.getter() >> last_subobject;
              break;
            }

          // check if we have results which are better than this one if it
          // does not have any subobjects
          if (m_levels_resolved > level+1 && id >= last_subobject)
            continue; // take the next search_result
        }

      if ( last_level ||
           last_subobject <= id ||
           !search(parsed, result, level+1, id+1, last_subobject) )
        {
          size_t levels_resolved = level+1;
          if ( m_levels_resolved < levels_resolved )
            {
              result.clear();
              m_levels_resolved = levels_resolved;
            }

          if (m_levels_resolved == levels_resolved && (m_max_results == 0 || result.size() < m_max_inter_results))
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
                  r.levels_resolved = levels_resolved;
                  result.push_back(r);
                }
            }
        }
    }

  return !ids_explored.empty();
}


void Geocoder::get_name(long long id, std::string &title, std::string &full, size_t &admin_levels, int levels_in_title)
{
  long long int parent;
  std::string name;
  std::string name_extra;
  std::string name_en;
  std::string toadd;

  sqlite3pp::query qry(m_db, "SELECT name, name_extra, name_en, parent FROM object_primary WHERE id=?");
  qry.bind(1, id);
  for (auto v: qry)
    {
      // only one entry is expected
      v.getter() >> name >> name_extra >> name_en >> parent;

      if (name.empty()) name=" ";

      toadd = std::string();
      if (m_preferred_result_language == "en" && !name_en.empty()) toadd = name_en;
      else if (!name_extra.empty() && name != name_extra) toadd = name_extra + ", " + name;

      if (toadd.empty()) toadd = name;

      if (!full.empty()) full += ", ";
      full += toadd;

      if (levels_in_title > 0)
        {
          if (!title.empty()) title += ", ";
          title += toadd;
        }

      get_name(parent, title, full, admin_levels, levels_in_title-1);
      admin_levels++;
      return;
    }
}


std::string Geocoder::get_type(long long id)
{
  std::string name;

  sqlite3pp::query qry(m_db, "SELECT t.name FROM object_primary o JOIN type t ON t.id=o.type_id WHERE o.id=?");
  qry.bind(1, id);

  for (auto v: qry)
    {
      std::string n;
      v.getter() >> n;

      if (!name.empty()) name += ", ";
      name += n;
    }

  return name;
}


bool Geocoder::get_id_range(std::string &v, bool full_range, index_id_value range0, index_id_value range1,
                            index_id_value* *idx0, index_id_value* *idx1)
{
  int sz = get_id_number_of_values(v);
  index_id_value* v0 = (index_id_value*)v.data();
  if (sz == 0)
    return false;

  if (full_range)
    {
      *idx0 = v0;
      *idx1 = v0 + sz;
      return true;
    }

  *idx0 = std::lower_bound(v0, v0 + sz, range0);
  if (*idx0 - v0 >= sz) return false;

  *idx1 = std::upper_bound(v0, v0 + sz, range1);
  if (*idx1 - v0 >= sz && *(v0) > range1 ) return false;

  return true;
}


// search next to the reference point
bool Geocoder::search_nearby( const std::vector< std::string > &name_query,
                              const std::vector< std::string > &type_query,
                              double latitude, double longitude,
                              double radius,
                              std::vector<GeoResult> &result,
                              Postal &postal )
{
  if ( (name_query.empty() && type_query.empty()) || radius < 0 )
    return false;

  // rough estimates of distance (meters) per degree
  //
  const double dist_per_degree_lat = distance_per_latitude();
  const double dist_per_degree_lon = distance_per_longitude(latitude);

  try {
    std::ostringstream qtxt;
    qtxt << "SELECT o.id, o.name, o.name_extra, o.name_en, t.name, o.latitude, o.longitude "
         << "FROM object_primary o "
         << "JOIN type t ON o.type_id=t.id "
         << "JOIN object_primary_rtree ON (o.box_id = object_primary_rtree.id) "
         << "WHERE ";
    
    if (!type_query.empty())
      {
        std::string tqfull;
        for (auto tq: type_query)
          {
            if (!tqfull.empty()) tqfull += " OR ";
            else tqfull = "(";
            tqfull += " t.name = '" + tq + "'";
          }
        qtxt << tqfull << ") AND ";
      }
    
    qtxt << "maxLat>=:minLat AND minLat<=:maxLat AND maxLon >= :minLon AND minLon <= :maxLon";

#ifdef GEONLP_PRINT_SQL
    std::cout << qtxt.str() << "\n";
#endif
    sqlite3pp::query qry(m_db, qtxt.str().c_str());

    qry.bind(":minLat", latitude - radius/dist_per_degree_lat);
    qry.bind(":maxLat", latitude + radius/dist_per_degree_lat);
    qry.bind(":minLon", longitude - radius/dist_per_degree_lon);
    qry.bind(":maxLon", longitude + radius/dist_per_degree_lon);

    for (auto v: qry)
      {
        long long id;
        std::string name, name_extra, name_en, type;
        double lat, lon, distance;
        v.getter() >> id >> name >> name_extra >> name_en >> type >> lat >> lon;

        // check if distance is ok. note that the distance is expected
        // to be small (on the scale of the planet)
        {
          double dlat = dist_per_degree_lat * (latitude-lat);
          double dlon = dist_per_degree_lon * (longitude-lon);
          distance = sqrt( dlat*dlat + dlon*dlon );
          if ( distance > radius )
            continue; // skip this result
        }

        // check name query
        if (!name_query.empty())
          {
            bool found = false;
            std::set<std::string> names;
            names.insert(name);
            names.insert(name_extra);
            names.insert(name_en);
            for (auto n = names.begin(); n!=names.end() && !found; ++n)
              {
                if (n->empty()) continue;
                std::vector<std::string> expanded;
                postal.expand_string( *n, expanded );

                for ( auto q = name_query.cbegin(); !found && q != name_query.cend(); ++q )
                  for ( auto e = expanded.begin(); !found && e != expanded.end(); ++e )
                    // search is for whether the name starts with the query or has
                    // the query after space (think of street Dr. Someone and query Someone)
                    found = ( e->compare(0, q->length(), *q) == 0  ||
                              e->find(" " + *q) != std::string::npos );
              }

            if (!found)
              continue; // substring not found
          }

        GeoResult r;
        r.id = id;

        get_name(r.id, r.title, r.address, r.admin_levels, m_levels_in_title);
        r.type = get_type(r.id);

        r.latitude = lat;
        r.longitude = lon;
        r.distance = distance;
        r.levels_resolved = 1; // not used in this search

        result.push_back(r);
      }
  }
  catch (sqlite3pp::database_error e) {
    std::cerr << "Geocoder exception: " << e.what() << std::endl;
    return false;
  }

  if ( m_max_results > 0 && result.size() >=  m_max_results )
    {
      Geocoder::sort_by_distance( result.begin(), result.end() );
      result.resize(m_max_results);
    }

  return true;
}


// search next to the reference linestring
bool Geocoder::search_nearby(const std::vector< std::string > &name_query,
                             const std::vector< std::string > &type_query,
                             const std::vector<double> &latitude, const std::vector<double> &longitude,
                             double radius,
                             std::vector<GeoResult> &result,
                             Postal &postal,
                             size_t skip_points)
{
  if ( (name_query.empty() && type_query.empty()) || radius < 0 || latitude.size() < 2 || latitude.size() != longitude.size())
    return false;

  //const double earth_radius = 6378137;
  const double radius2 = radius*radius;
  
  // // fill linestring
  // boost::geometry::model::linestring<point_t> ls;
  // for (size_t i=0; i < latitude.size(); ++i)
  //   boost::geometry::append(ls, point_t(longitude[i],latitude[i]));
  
  try {

    std::set<long long> processed_boxes;
    double line_distance = 0;
    for (size_t LineI = skip_points; LineI < longitude.size()-1 && (m_max_results == 0 || result.size() <=  m_max_results); ++LineI)
      {

        // rough estimates of distance (meters) per degree
        const double dist_per_degree_lat = distance_per_latitude();
        const double dist_per_degree_lon = distance_per_longitude(latitude[LineI]);
        {
          double x1 = latitude[LineI]*dist_per_degree_lat;
          double y1 = longitude[LineI]*dist_per_degree_lon;
          double x2 = latitude[LineI+1]*dist_per_degree_lat;
          double y2 = longitude[LineI+1]*dist_per_degree_lon;
          double dx = x2-x1;
          double dy = y2-y1;
          line_distance += sqrt(dx*dx+dy*dy);
        }
        
        // step 1: get boxes that are near the line segment
        std::deque<long long> newboxes;
        {
          sqlite3pp::query qry(m_db,
                               "SELECT id, minLat, maxLat, minLon, maxLon "
                               "FROM object_primary_rtree "
                               "WHERE maxLat>=:minLat AND minLat<=:maxLat AND maxLon >= :minLon AND minLon <= :maxLon" );
          
          auto bb_lat = std::minmax(latitude[LineI], latitude[LineI+1]);
          auto bb_lon = std::minmax(longitude[LineI], longitude[LineI+1]);
          
          qry.bind(":minLat", bb_lat.first - radius/dist_per_degree_lat);
          qry.bind(":maxLat", bb_lat.second + radius/dist_per_degree_lat);
          qry.bind(":minLon", bb_lon.first - radius/dist_per_degree_lon);
          qry.bind(":maxLon", bb_lon.second + radius/dist_per_degree_lon);
          
          for (auto v: qry)
            {
              long long id;
              double minLat, maxLat, minLon, maxLon;
              v.getter() >> id >> minLat >> maxLat >> minLon >> maxLon;
              
              if (processed_boxes.count(id))
                continue;
              processed_boxes.insert(id);
              newboxes.push_back(id);
            }
        }

        // check if anything is new
        if (newboxes.empty()) continue;

        // step 2: search for objects from new boxes
        {
          std::ostringstream qtxt;
          qtxt << "SELECT o.id, o.name, o.name_extra, o.name_en, t.name, "
               << "o.latitude, o.longitude "
               << "FROM object_primary o "
               << "JOIN type t ON o.type_id=t.id "
               << "WHERE ";

          if (!type_query.empty())
            {
              std::string tqfull;
              for (auto tq: type_query)
                {
                  if (!tqfull.empty()) tqfull += " OR ";
                  else tqfull = "(";
                  tqfull += " t.name = '" + tq + "'";
                }
              qtxt << tqfull << ") AND ";
            }

          qtxt << " o.box_id IN (";
          for (size_t i=0; i < newboxes.size(); ++i)
            {
              if (i) qtxt << ", ";
              qtxt << newboxes[i];
            }
          qtxt << ")";
#ifdef GEONLP_PRINT_SQL
          std::cout << qtxt.str() << "\n";
#endif
          sqlite3pp::query qry(m_db, qtxt.str().c_str());

          for (auto v: qry)
            {
              long long id;
              std::string name, name_extra, name_en, type;
              double lat, lon, distance;
              v.getter() >> id >> name >> name_extra >> name_en >> type >> lat >> lon;

              // check if distance is ok using earth as a plane approximation around the line
              {
                double distance2 = -1;
                const double xp = lat*dist_per_degree_lat;
                const double yp = lon*dist_per_degree_lon;
                double u=0, nrm2=0; // cache the last result for distance
                for (size_t i=LineI; i < longitude.size()-1 && (distance2<0 || distance2>radius2); ++i)
                  {
                    // constants used for distance calculations, drop when
                    // moving back to boost geometry
                    const double x1 = latitude[i]*dist_per_degree_lat;
                    const double y1 = longitude[i]*dist_per_degree_lon;
                    const double x2 = latitude[i+1]*dist_per_degree_lat;
                    const double y2 = longitude[i+1]*dist_per_degree_lon;
                    const double px = x2-x1;
                    const double py = y2-y1;
                    nrm2 = px*px + py*py;
                    
                    u = ((xp - x1) * px + (yp - y1) * py) / nrm2;
                    u = std::max(0.0, std::min(1.0, u));

                    double dx = (x1 + u * px) - xp;
                    double dy = (y1 + u * py) - yp;
                    double dd = dx*dx + dy*dy;

                    if (distance2 < 0 || distance2 > dd) distance2 = dd;
                  }
                
                if ( distance2 > radius2 )
                  continue;

                distance = line_distance - (1-u) * sqrt(nrm2);
              }
              // // distance check using boost
              // if ( boost::geometry::distance(point_t(lon, lat), ls)*earth_radius > radius )
              //   continue; // skip this result

              // check name query
              if (!name_query.empty())
                {
                  bool found = false;
                  std::set<std::string> names;
                  names.insert(name);
                  names.insert(name_extra);
                  names.insert(name_en);
                  for (auto n = names.begin(); n!=names.end() && !found; ++n)
                    {
                      if (n->empty()) continue;
                      std::vector<std::string> expanded;
                      postal.expand_string( *n, expanded );
        
                      for ( auto q = name_query.cbegin(); !found && q != name_query.cend(); ++q )
                        for ( auto e = expanded.begin(); !found && e != expanded.end(); ++e )
                          // search is for whether the name starts with the query or has
                          // the query after space (think of street Dr. Someone and query Someone)
                          found = ( e->compare(0, q->length(), *q) == 0  ||
                                    e->find(" " + *q) != std::string::npos );
                    }
                  
                  if (!found)
                    continue; // substring not found
                }
        
              GeoResult r;
              r.id = id;
        
              get_name(r.id, r.title, r.address, r.admin_levels, m_levels_in_title);
              r.type = get_type(r.id);
        
              r.latitude = lat;
              r.longitude = lon;
              r.distance = distance;
              r.levels_resolved = 1; // not used in this search
        
              result.push_back(r);
            }
        }
      }
  }
  catch (sqlite3pp::database_error e) {
    std::cerr << "Geocoder exception: " << e.what() << std::endl;
    return false;
  }

  return true;
}


int Geocoder::closest_segment(const std::vector<double> &latitude, const std::vector<double> &longitude,
                              double reference_latitude, double reference_longitude)
{
  // make all checks first
  if ( latitude.size() < 2 || latitude.size() != longitude.size())
    return -1;

  // rough estimates of distance (meters) per degree
  const double dist_per_degree_lat = distance_per_latitude();
  const double dist_per_degree_lon = distance_per_longitude(reference_latitude);

  const double xp = reference_latitude*dist_per_degree_lat;
  const double yp = reference_longitude*dist_per_degree_lon;

  size_t currI = 0;
  double currD2 = -1;
  for (size_t i = 0; i < longitude.size()-1; ++i)
    {
      const double x1 = latitude[i]*dist_per_degree_lat;
      const double y1 = longitude[i]*dist_per_degree_lon;
      const double x2 = latitude[i+1]*dist_per_degree_lat;
      const double y2 = longitude[i+1]*dist_per_degree_lon;
      const double px = x2-x1;
      const double py = y2-y1;
      const double nrm2 = px*px + py*py;
                
      double u = ((xp - x1) * px + (yp - y1) * py) / nrm2;
      u = std::max(0.0, std::min(1.0, u));
      double dx = (x1 + u * px) - xp;
      double dy = (y1 + u * py) - yp;
      double dd = dx*dx + dy*dy;

      if (currD2 < 0 || currD2 > dd)
        {
          currI = i;
          currD2 = dd;
        }
    }

  return currI;
}
