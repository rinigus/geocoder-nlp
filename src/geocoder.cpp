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
  if (dbname == m_database_path && m_database_open) return true;

  if (dbname != m_database_path && !m_database_open)
    drop();

  try
    {
      m_database_open = (m_db.connect(dbname.c_str(), SQLITE_OPEN_READONLY) == SQLITE_OK );

      m_database_path = dbname;

      if ( m_database_open && !check_version() )
	drop();
    }
  catch (sqlite3pp::database_error e)
    {
      std::cerr << "Geocoder exception: " << e.what() << std::endl;
      return false;
    }
  
  return m_database_open;
}

bool Geocoder::load()
{
  return load( m_database_path );
}

void Geocoder::drop()
{
  m_db.disconnect();
  m_database_path = std::string();
  m_database_open = false;
}

bool Geocoder::check_version()
{
  return check_version("1");
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
	  if ( n == "1" ) return true;
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

bool Geocoder::search(const std::vector<Postal::ParseResult> &parsed_query, std::vector<Geocoder::GeoResult> &result)
{
  if (!m_database_open)
    return false;
  
  // parse query by libpostal
  std::vector< std::vector<std::string> > parsed_result;
  Postal::result2hierarchy(parsed_query, parsed_result);

  result.clear();
  m_levels_resolved = 0;

#ifdef GEONLP_PRINT_DEBUG
  std::cout << "Search hierarchies:\n";
#endif
#ifdef GEONLP_PRINT_DEBUG_QUERIES
  std::cout << "\n";
#endif

  try { // catch SQLite exceptions
    for (const auto &r: parsed_result)
      {
#ifdef GEONLP_PRINT_DEBUG
	for (auto a: r)
	  std::cout << a << " / ";
	std::cout << "\n";
#endif

	m_query_count = 0;
	search(r, result);
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

bool Geocoder::search(const std::vector<std::string> &parsed, std::vector<Geocoder::GeoResult> &result, size_t level,
                      long long int range0, long long int range1)
{
  if ( level >= parsed.size() || (m_max_queries_per_hierarchy>0 && m_query_count > m_max_queries_per_hierarchy) )
    return false;

  m_query_count++;

  std::string extra;
  if (level > 0)
    extra = "AND nn.prim_id>? AND nn.prim_id<=?";

  std::deque<long long int> ids;
  std::deque<long long int> last_subobj;

  std::string command;
  bool last_level = ( level+1 >= parsed.size() );

  if ( last_level ) // this is the last level we are interested in
    command =
      "SELECT DISTINCT nn.prim_id FROM normalized_name nn WHERE nn.name GLOB \"" + parsed[level] +
      "*\" " + extra + " ORDER BY length(name)";

  else // we would be interested in sub-objects
    {
      command = "SELECT DISTINCT nn.prim_id, h.last_subobject FROM normalized_name nn ";

      if (m_levels_resolved <= level+1) // any result could improve
	// the current best hit
	command += "LEFT ";

      command +=
	"JOIN hierarchy h ON nn.prim_id=h.prim_id "
	"WHERE nn.name GLOB \"" + parsed[level] +
	"*\" " + extra + " ORDER BY length(nn.name)";
    }

#ifdef GEONLP_PRINT_DEBUG_QUERIES
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

      if ( !last_level )
	{
	  long long int r1 = -1;
	  bool null_type = (v.column_type(1) == SQLITE_NULL);
	  if (!null_type) r1 = v.get<long long int>(1);
	  last_subobj.push_back(r1);
	}
    }

  for (size_t id_i = 0; id_i < ids.size(); ++id_i)
    {
      long long int id = ids[id_i];
      if ( last_level ||
	   last_subobj[id_i] <= id ||
	   !search(parsed, result, level+1, id, last_subobj[id_i]) )
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


std::string Geocoder::get_type(long long id)
{
  std::string name;

  sqlite3pp::query qry(m_db, "SELECT t.name FROM object_type o JOIN type t ON t.id=o.type_id WHERE o.prim_id=?");
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
