#include <osmscout/Database.h>
#include <osmscout/LocationService.h>

#include <sqlite3pp.h>
#include <libpostal/libpostal.h>

#include <map>
#include <deque>

#define DB_VERSION "1"

typedef long long int sqlid; /// type used by IDs in SQLite

///////////////////////////////////////////////////////////
/// Track relationship between objects and fills hierarchy
/// table in the resulting database
class IDTracker
{
public:
  sqlid next() { NextID++; return NextID-1; }

  sqlid get_id(osmscout::FileOffset offset)
  {
    auto indx = m_ids.find(offset);
    if (indx == m_ids.end()) return 0;
    return indx->second;
  }

  void set_id(osmscout::FileOffset fo, sqlid id) { m_ids[fo] = id; }

  void set_parent(sqlid id, sqlid parent, bool track_children = false)
  {
    if ( track_children )
      {
	m_last_child[id] = 0;
	m_parent[id] = parent;
      }

    m_last_child[parent] = id;
    if ( m_parent.find(parent) != m_parent.end() )
      set_parent(id, m_parent[parent], false);
  }

  void set_parent(sqlid id, osmscout::FileOffset foparent, bool track_children = false)
  {
    set_parent(id, get_id(foparent), track_children);
  }

  void write_hierarchy(sqlite3pp::database &db)
  {
    for (auto a: m_last_child)
      {
	sqlid id = a.first;
	sqlid last_id = a.second;
	if (id >= last_id) continue;
	
	sqlite3pp::command cmd(db, "INSERT INTO hierarchy (prim_id, last_subobject) VALUES (?, ?)");
	cmd.binder() << id << last_id;
	cmd.execute();
      }
  }

protected:
  sqlid NextID = 1;
  
  std::map<osmscout::FileOffset, sqlid> m_ids;
  std::map<sqlid, sqlid> m_parent;
  std::map<sqlid, sqlid> m_last_child;  
};

////////////////////////////////////////////////////////////////
/// Global variable tracking IDs and administrative relationship
IDTracker IDs;

//////////////////////////////////////////////////////////////
/// libosmscout helper functions

void GetObjectTypeCoor( const osmscout::DatabaseRef& database,
                        const osmscout::ObjectFileRef& object,
                        std::string &type,
                        osmscout::GeoCoord& coordinates )
{
  if (object.GetType()==osmscout::RefType::refNode)
    {
      osmscout::NodeRef node;

      if (database->GetNodeByOffset(object.GetFileOffset(),
				    node)) {
	type+=node->GetType()->GetName();
	coordinates = node->GetCoords();
      }
    }
  else if (object.GetType()==osmscout::RefType::refArea) {
    osmscout::AreaRef area;

    if (database->GetAreaByOffset(object.GetFileOffset(),
				  area)) {
      type+=area->GetType()->GetName();
      area->GetCenter(coordinates);
    }
  }
  else if (object.GetType()==osmscout::RefType::refWay) {
    osmscout::WayRef way;

    if (database->GetWayByOffset(object.GetFileOffset(),
				 way)) {
      type+=way->GetType()->GetName();
      coordinates = way->GetCoord(way->nodes.size()/2);
    }
  }
}


////////////////////////////////////////////////////////////////////////////
/// SQLite helper functions
void write_type(sqlite3pp::database &db, sqlid id, std::string type)
{
  std::string command = "INSERT INTO object_type_tmp (prim_id, type) VALUES (?, \"" + type + "\")";
  sqlite3pp::command cmd(db, command.c_str());
  cmd.binder() << id;
  if (cmd.execute() != SQLITE_OK)
    std::cerr << "WriteSQLType: error inserting " << id << " " << type << "\n";
}


////////////////////////////////////////////////////////////////////////////
/// Visitors used while going through libosmscout database

//////////////////////////////////////////////////
class AddrVisitor: public osmscout::AddressVisitor
{
public:
  AddrVisitor(osmscout::DatabaseRef &database, sqlite3pp::database &db, sqlid parent):
    m_database(database),
    m_db(db),
    m_parent(parent)
  {}

  virtual bool Visit(const osmscout::AdminRegion &adminRegion, const osmscout::Location &location, const osmscout::Address &address)
  {
    std::string type;
    osmscout::GeoCoord coordinates;
    sqlid id;
    
    GetObjectTypeCoor(m_database, address.object, type, coordinates);
    id = IDs.next();

    sqlite3pp::command cmd(m_db, "INSERT INTO object_primary (id, name, parent, longitude, latitude) VALUES (?, ?, ?, ?, ?)");
    cmd.binder() << id
		 << address.name
		 << m_parent
		 << coordinates.GetLon()
		 << coordinates.GetLat();
    
    if (cmd.execute() != SQLITE_OK)
      std::cerr << "Error inserting " << address.name << "\n";

    IDs.set_parent(id, m_parent);
    write_type(m_db, id, type);

    return true;
  }

protected:
  osmscout::DatabaseRef &m_database;
  sqlite3pp::database &m_db;
  sqlid m_parent;
};

//////////////////////////////////////////////////
class LocVisitor: public osmscout::LocationVisitor
{
public:
  LocVisitor(osmscout::DatabaseRef &database, sqlite3pp::database &db, sqlid parent):
    m_database(database),
    m_db(db),
    m_parent(parent)
  {}

  virtual bool Visit(const osmscout::AdminRegion &adminRegion, const osmscout::POI &poi)
  {
    std::string type;
    osmscout::GeoCoord coordinates;
    sqlid id;
    
    GetObjectTypeCoor(m_database, poi.object, type, coordinates);
    id = IDs.next();

    sqlite3pp::command cmd(m_db, "INSERT INTO object_primary (id, name, parent, longitude, latitude) VALUES (?, ?, ?, ?, ?)");
    cmd.binder() << id
		 << poi.name
		 << m_parent
		 << coordinates.GetLon()
		 << coordinates.GetLat();
    
    if (cmd.execute() != SQLITE_OK)
      std::cerr << "Error inserting " << poi.name << "\n";

    IDs.set_parent(id, m_parent);
    write_type(m_db, id, type);

    return true;
  }
 
  virtual bool Visit(const osmscout::AdminRegion &adminRegion, const osmscout::Location &location)
  {
    std::string type;
    osmscout::GeoCoord coordinates;
    sqlid id;
    sqlid locID;

    if ( location.objects.size() < 1 )
      {
	std::cout << "Location " << location.name << " has no objects, skipping\n";
	return true;
      }
    
    GetObjectTypeCoor(m_database, location.objects[ location.objects.size()/2 ], type, coordinates);
    locID = id = IDs.next();
    IDs.set_id(location.locationOffset, id);
    
    sqlite3pp::command cmd(m_db, "INSERT INTO object_primary (id, name, parent, longitude, latitude) VALUES (?, ?, ?, ?, ?)");
    cmd.binder() << id
		 << location.name
		 << m_parent
		 << coordinates.GetLon()
		 << coordinates.GetLat();

    if (cmd.execute() != SQLITE_OK)
      std::cerr << "Error inserting " << location.name << "\n";

    IDs.set_parent(id, m_parent, true);
    write_type(m_db, id, type);
    
    AddrVisitor addr(m_database, m_db, locID);
    m_database->GetLocationIndex()->VisitLocationAddresses(adminRegion, location, addr);

    return true;
  }

protected:
  osmscout::DatabaseRef &m_database;
  sqlite3pp::database &m_db;
  sqlid m_parent;
};


///////////////////////////////////////////////////////
class AdminVisitor: public osmscout::AdminRegionVisitor
{
public:

  AdminVisitor(osmscout::DatabaseRef &database, sqlite3pp::database &db):
    m_database(database),
    m_db(db)
  {}
  
  virtual osmscout::AdminRegionVisitor::Action Visit(const osmscout::AdminRegion &region)
  {
    // insert region first
    std::string type;
    osmscout::GeoCoord coordinates;
    sqlid id;
    sqlid regionID;
    
    GetObjectTypeCoor(m_database, region.object, type, coordinates);
    regionID = id = IDs.next();
    IDs.set_id(region.regionOffset, id);

    sqlite3pp::command cmd(m_db, "INSERT INTO object_primary (id, name, parent, longitude, latitude) VALUES (?, ?, ?, ?, ?)");
    cmd.binder() << id
		 << region.name
		 << IDs.get_id(region.parentRegionOffset)
		 << coordinates.GetLon()
		 << coordinates.GetLat();
    IDs.set_parent(id, region.parentRegionOffset, true);
    
    if (cmd.execute() != SQLITE_OK)
      std::cerr << "Error inserting " << region.name << "\n";

    write_type(m_db, id, type);

    // record region aliases
    std::deque<std::string> saved_names; saved_names.push_back(region.name);
    if (region.aliasName.length() > 0 &&
	find(saved_names.begin(),
	     saved_names.end(),
	     region.aliasName) != saved_names.end() )
      {
	sqlite3pp::command cmd(m_db, "INSERT INTO object_primary (id, name, parent, longitude, latitude) VALUES (?, ?, ?, ?, ?)");
	
	GetObjectTypeCoor(m_database, region.aliasObject, type, coordinates);
	id = IDs.next();
	
	cmd.binder() << id
		     << region.aliasName
		     << IDs.get_id(region.parentRegionOffset)
		     << coordinates.GetLon()
		     << coordinates.GetLat();
	if (cmd.execute() != SQLITE_OK)
	  std::cerr << "Error inserting " << region.aliasName << "\n";
	
	write_type(m_db, id, type);
	IDs.set_parent(id, region.parentRegionOffset);
      }

    for (auto alias: region.aliases)
      {
	if ( alias.name.length() < 1 ||
	     find(saved_names.begin(),
		  saved_names.end(),
		  alias.name) == saved_names.end() )
	  continue; // skip since we saved it already

	saved_names.push_back(alias.name);
	
	sqlite3pp::command cmd(m_db, "INSERT INTO object_primary (id, name, parent, longitude, latitude) VALUES (?, ?, ?, ?, ?)");
	id = IDs.next();
	cmd.binder() << id
		     << alias.name
		     << IDs.get_id(region.parentRegionOffset)
		     << coordinates.GetLon()
		     << coordinates.GetLat();
	if (cmd.execute() != SQLITE_OK)
	  std::cerr << "Error inserting " << alias.name << "\n";
	
	write_type(m_db, id, type);
	IDs.set_parent(id, region.parentRegionOffset);
      }

    LocVisitor loc(m_database, m_db, regionID);
    m_database->GetLocationIndex()->VisitAdminRegionLocations(region, loc, false);
    
    return osmscout::AdminRegionVisitor::visitChildren;
  };

protected:
  osmscout::DatabaseRef &m_database;
  sqlite3pp::database &m_db;
};

////////////////////////////////////////////////////////////////////////////
/// Libpostal normalization with search string expansion
void normalize_libpostal(sqlite3pp::database& db)
{
  struct tonorm
  {
    std::string name;
    sqlid id;
  };

  std::deque<tonorm> data;
  sqlite3pp::query qry(db, "SELECT id, name FROM object_primary");
  for (auto v : qry)
    {
      tonorm d;
      v.getter() >> d.id >> d.name;
      data.push_back(d);
    }

  // make a new table for normalized names
  db.execute("DROP TABLE IF EXISTS normalized_name");
  db.execute("CREATE TABLE normalized_name (prim_id INTEGER, name TEXT NOT NULL, FOREIGN KEY (prim_id) REFERENCES objects_primary(id))");

  // load libpostal
  if (!libpostal_setup() || !libpostal_setup_language_classifier())
    {
      std::cerr << "Failure to load libpostal" << std::endl;
      return;
    }

  // normalize all names
  size_t num_expansions;
  normalize_options_t options = get_libpostal_default_options();
  std::vector<char> charbuff;
  for (tonorm &d: data)
    {
      charbuff.resize(d.name.length() + 1);
      std::copy(d.name.c_str(), d.name.c_str() + d.name.length() + 1, charbuff.begin());
      char **expansions = expand_address(charbuff.data(), options, &num_expansions);

      for (size_t i = 0; i < num_expansions; i++)
	{
	  sqlite3pp::command cmd(db, "INSERT INTO normalized_name (prim_id, name) VALUES (?,?)");
	  std::string s = expansions[i];
	  cmd.binder() << d.id
		       << s;
	  if (cmd.execute() != SQLITE_OK)
	    std::cerr << "Error inserting: " << d.id << " " << s << std::endl;

	  // to cover the street names that have Dr. or the firstname
	  // in the front of the mainly used name, add substrings into
	  // the normalized table as well
	  const size_t max_substrings = 2;
	  size_t pos = 1;
	  for (size_t sbs=0; sbs < max_substrings && pos < s.length(); ++sbs)
	    {
	      bool spacefound = false;
	      for (; pos<s.length(); ++pos)
		{
		  char c = s[pos];
		  if (c == ' ') spacefound = true;
		  if (spacefound && c!=' ')
		    break;
		}
	      
	      if (pos < s.length())
		{
		  sqlite3pp::command cmd(db, "INSERT INTO normalized_name (prim_id, name) VALUES (?,?)");
		  std::string s = expansions[i];
		  cmd.binder() << d.id
			       << s.substr(pos);
		  if (cmd.execute() != SQLITE_OK)
		    std::cerr << "Error inserting: " << d.id << " " << s << std::endl;
		}
	    }
	  
	}

      // Free expansions
      expansion_array_destroy(expansions, num_expansions);
    }

  // Teardown libpostal
  libpostal_teardown();
  libpostal_teardown_language_classifier();
}


////////////////////////////////////////////////////////////////////////////
// MAIN

int main(int argc, char* argv[])
{
  if (argc<3)
    {
      std::cerr << "importer <map directory> <sqlite database name>\n";
      return 1;
    }

  std::string map = argv[1];
  std::string sqlite_filepath = argv[2];

  osmscout::DatabaseParameter databaseParameter;
  osmscout::DatabaseRef database(new osmscout::Database(databaseParameter));

  if (!database->Open(map.c_str()))
    {
      std::cerr << "Cannot open libosmscout database: " << map << std::endl;
      return 1;
    }

  sqlite3pp::database db(sqlite_filepath.c_str());
			 
  db.execute( "PRAGMA journal_mode = OFF" );
  db.execute( "PRAGMA synchronous = OFF" );
  db.execute( "DROP TABLE IF EXISTS type" );
  db.execute( "DROP TABLE IF EXISTS object_primary" );
  // db.execute( "DROP TABLE IF EXISTS object_alias" );
  db.execute( "DROP TABLE IF EXISTS object_type" );
  db.execute( "DROP TABLE IF EXISTS object_type_tmp" );
  db.execute( "DROP TABLE IF EXISTS hierarchy" );
  db.execute( "CREATE TABLE object_primary (id INTEGER PRIMARY KEY AUTOINCREMENT, name TEXT NOT NULL, parent INTEGER, latitude REAL, longitude REAL)");
  // db.execute( "CREATE TABLE object_alias (prim_id INTEGER, name TEXT NOT NULL, FOREIGN KEY (prim_id) REFERENCES objects_primary(id))");
  db.execute( "CREATE TABLE object_type_tmp (prim_id INTEGER, type TEXT NOT NULL, FOREIGN KEY (prim_id) REFERENCES objects_primary(id))" );
  db.execute( "CREATE TABLE hierarchy (prim_id INTEGER PRIMARY KEY, last_subobject INTEGER, "
	      "FOREIGN KEY (prim_id) REFERENCES objects_primary(id), FOREIGN KEY (last_subobject) REFERENCES objects_primary(id))" );

  std::cout << "Preliminary filling of the database" << std::endl; 
  
  osmscout::LocationIndexRef locationIndex = database->GetLocationIndex();
  AdminVisitor vis_admin(database, db);
  locationIndex->VisitAdminRegions(vis_admin);
  IDs.write_hierarchy(db);

  std::cout << "Reorganizing database tables" << std::endl; 

  db.execute( "CREATE TABLE type (id INTEGER PRIMARY KEY AUTOINCREMENT, name TEXT)" );
  db.execute( "INSERT INTO type (name) SELECT DISTINCT type FROM object_type_tmp" );
  db.execute( "CREATE TABLE object_type (prim_id INTEGER PRIMARY KEY, type_id INTEGER, "
	      "FOREIGN KEY (prim_id) REFERENCES object_primary(id), FOREIGN KEY (type_id) REFERENCES type(id))" );
  db.execute( "INSERT INTO object_type (prim_id, type_id) "
	      "SELECT object_type_tmp.prim_id, type.id FROM object_type_tmp JOIN type ON object_type_tmp.type=type.name" );
  db.execute( "DROP TABLE IF EXISTS object_type_tmp" );

  std::cout << "Normalize using libpostal" << std::endl;
  
  normalize_libpostal(db);

  db.execute ( "DROP INDEX IF EXISTS idx_norm_name" );
  db.execute ( "CREATE INDEX idx_norm_name ON normalized_name (name)" );

  db.execute( "DROP TABLE IF EXISTS meta" );
  db.execute( "CREATE TABLE meta (key TEXT, value TEXT)" );
  db.execute( "INSERT INTO meta (key, value) VALUES (\"version\", \"" DB_VERSION "\")" );

  db.execute( "VACUUM" );

  std::cout << "Done\n";
  
  return 0;
}
