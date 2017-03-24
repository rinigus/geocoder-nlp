#include "geocoder.h"

#include <sstream>
#include <deque>
#include <set>
#include <iostream>
#include <algorithm>

using namespace GeoNLP;

Geocoder::Geocoder()
{
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
  return dname + "/geonlp-normalized-id.kct";
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
      
      if ( !error &&
           !m_database_norm_id.open(name_normalized_id(m_database_path).c_str(),
                                    kyotocabinet::PolyDB::OREADER | kyotocabinet::PolyDB::ONOLOCK ) )
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
  return check_version("2");
}
  
bool Geocoder::check_version(const char *supported)
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

bool Geocoder::search(const std::vector<Postal::ParseResult> &parsed_query, std::vector<Geocoder::GeoResult> &result)
{
  if (!m_database_open)
    return false;
  
  // parse query by libpostal
  std::vector< Postal::Hierarchy > parsed_result;
  Postal::result2hierarchy(parsed_query, parsed_result);

  result.clear();
  m_levels_resolved = 0;

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
             (r.size() == m_levels_resolved && result.size() < m_max_results) )
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
	get_name(r.id, r.title, r.address, m_levels_in_title);
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
  

  return true;
}


bool Geocoder::search(const Postal::Hierarchy &parsed,
                      std::vector<Geocoder::GeoResult> &result, size_t level,
                      long long int range0, long long int range1)
{
  if ( level >= parsed.size() || (m_max_queries_per_hierarchy>0 && m_query_count > m_max_queries_per_hierarchy) )
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
              if ( level == 0 )
                {
                }
              if ( !get_id_range(val, range0, range1,
                                &idx, &idx1) )
                {
                  for (; idx < idx1; ++idx)
                    {
                      long long int id = *idx;
                      if ( level == 0 || ( id > range0 && id <= range1 ) )
                        {
                          IntermediateResult r( std::string(agent.key().ptr(), agent.key().length()),
                                                id );
                          search_result.push_back(r);
                        }
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
	   !search(parsed, result, level+1, id, last_subobject) )
        {
	  size_t levels_resolved = level+1;
	  if ( m_levels_resolved < levels_resolved )
            {
	      result.clear();
	      m_levels_resolved = levels_resolved;
            }

	  if (m_levels_resolved == levels_resolved && (m_max_results==0 || result.size() < m_max_results))
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


bool Geocoder::get_id_range(std::string &v, index_id_value range0, index_id_value range1,
                            index_id_value* *idx0, index_id_value* *idx1)
{
  size_t sz = get_id_number_of_values(v);
  index_id_value* v0 = (index_id_value*)v.data();
  if (sz == 0)
    return false;

  *idx0 = std::lower_bound(v0, v0 + sz, range0);
  if (*idx0 - v0 >= sz) return false;
  
  *idx1 = std::upper_bound(v0, v0 + sz, range1);
  if (*idx1 - v0 >= sz && *(v0) > range1 ) return false;

  return true;
}
