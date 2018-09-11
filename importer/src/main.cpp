/////////////////////////////////////////////////////////////////////
/// WARNING: When adding new languages, increase the language count
/// num_languages in geocoder.c
/////////////////////////////////////////////////////////////////////

#include <osmscout/Database.h>
#include <osmscout/LocationService.h>
#include <osmscout/TypeFeatures.h>
#include <osmscout/FeatureReader.h>
#include <osmscout/LocationDescriptionService.h>

#include "geocoder.h"

#include <sqlite3pp.h>
#include <libpostal/libpostal.h>

#include <marisa.h>
#include <kchashdb.h>

#include <map>
#include <deque>
#include <set>
#include <cctype>
#include <fstream>
#include <algorithm>
#include <locale>

#include <boost/tokenizer.hpp>

#define MAX_NUMBER_OF_EXPANSIONS 85 /// if there are more expansions
                                    /// that specified, this object
                                    /// will be dropped from
                                    /// normalization table

#define LENGTH_STARTING_SUSP_CHECK 200 /// starting from this length,
                                       /// check wheher the string is
                                       /// suspicious

#define MAX_COMMAS 10 /// maximal number of commas allowed in a name

#define TEMPORARY "TEMPORARY" // set to empty if need to debug import

typedef long long int sqlid; /// type used by IDs in SQLite


////////////////////////////////////////////////////////////////
/// White list of POI types to be tracked even without name
std::set< std::string > m_poi_types_whitelist;

////////////////////////////////////////////////////////////////
/// Track locations and POIs to avoid adding them in duplicate
/// via location and POI visitors
std::set< std::string > m_address_poi_inserted;

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
/// Additional postal codes

struct PostalCode {
  std::string code;
  double latitude, longitude;
};

std::map< osmscout::FileOffset, std::vector<PostalCode> > additional_postal_codes;

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
        type=node->GetType()->GetName();
        coordinates = node->GetCoords();
      }
    }
  else if (object.GetType()==osmscout::RefType::refArea) {
    osmscout::AreaRef area;

    if (database->GetAreaByOffset(object.GetFileOffset(),
                                  area)) {
      type=area->GetType()->GetName();
      area->GetCenter(coordinates);
    }
  }
  else if (object.GetType()==osmscout::RefType::refWay) {
    osmscout::WayRef way;

    if (database->GetWayByOffset(object.GetFileOffset(),
                                 way)) {
      type=way->GetType()->GetName();
      coordinates = way->GetCoord(way->nodes.size()/2);
    }
  }
}


/////////////////////////////////////////////////////////////////////
/// WARNING: When adding new languages, increase the language count
/// num_languages in geocoder.c
/////////////////////////////////////////////////////////////////////

typedef osmscout::FeatureValueReader<osmscout::NameAltFeature,osmscout::NameAltFeatureValue> NameAltReader;
typedef osmscout::FeatureValueReader<osmscout::NameFeature,osmscout::NameFeatureValue> NameReader;
typedef osmscout::FeatureValueReader<osmscout::PhoneFeature,osmscout::PhoneFeatureValue> PhoneReader;
typedef osmscout::FeatureValueReader<osmscout::PostalCodeFeature,osmscout::PostalCodeFeatureValue> PostalCodeReader;
typedef osmscout::FeatureValueReader<osmscout::WebsiteFeature,osmscout::WebsiteFeatureValue> WebsiteReader;
NameAltReader *nameAltReader{NULL};
NameReader *nameReader{NULL};
PhoneReader *phoneReader{NULL};
PostalCodeReader *postalCodeReader{NULL};
WebsiteReader *websiteReader{NULL};
void GetFeatures(const osmscout::FeatureValueBuffer &features, std::string &name, std::string &name_en, std::string &phone, std::string &postal_code, std::string &website)
{
  osmscout::NameFeatureValue *nameValue=nameReader->GetValue(features);
  if (nameValue != NULL)
    name = nameValue->GetName();

  osmscout::NameAltFeatureValue *nameAltValue=nameAltReader->GetValue(features);
  if (nameAltValue != NULL)
    name_en = nameAltValue->GetNameAlt();

  osmscout::PhoneFeatureValue *phoneValue=phoneReader->GetValue(features);
  if (phoneValue != NULL)
    phone = phoneValue->GetPhone();

  osmscout::PostalCodeFeatureValue *postalCodeValue=postalCodeReader->GetValue(features);
  if (postalCodeValue != NULL)
    postal_code = GeoNLP::Postal::normalize_postalcode(postalCodeValue->GetPostalCode());

  osmscout::WebsiteFeatureValue *websiteValue=websiteReader->GetValue(features);
  if (websiteValue != NULL)
    website = websiteValue->GetWebsite();
}

void GetObjectFeatures( const osmscout::DatabaseRef& database,
                        const osmscout::ObjectFileRef& object,
                        std::string &name,
                        std::string &name_en,
                        std::string &phone,
                        std::string &postal_code,
                        std::string &website,
                        bool reset = false )
{
  if (reset)
    {
      name = std::string();
      name_en = std::string();
      phone = std::string();
      postal_code = std::string();
      website = std::string();
    }

  if (object.GetType()==osmscout::RefType::refNode)
    {
      osmscout::NodeRef node;

      if (database->GetNodeByOffset(object.GetFileOffset(),
                                    node))
        GetFeatures(node->GetFeatureValueBuffer(), name, name_en, phone, postal_code, website);
    }
  else if (object.GetType()==osmscout::RefType::refArea)
    {
      osmscout::AreaRef area;

      if (database->GetAreaByOffset(object.GetFileOffset(),
                                    area))
        GetFeatures(area->GetFeatureValueBuffer(), name, name_en, phone, postal_code, website);
    }
  else if (object.GetType()==osmscout::RefType::refWay)
    {
      osmscout::WayRef way;

      if (database->GetWayByOffset(object.GetFileOffset(),
                                   way))
        GetFeatures(way->GetFeatureValueBuffer(), name, name_en, phone, postal_code, website);
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

  virtual bool Visit(const osmscout::AdminRegion &adminRegion, const osmscout::PostalArea &postalArea, const osmscout::Location &location, const osmscout::Address &address)
  {
    std::string type;
    std::string name;
    std::string name_en;
    std::string phone;
    std::string postal_code = GeoNLP::Postal::normalize_postalcode(postalArea.name);
    std::string website;
    osmscout::GeoCoord coordinates;
    std::string scoutid = address.object.GetName();
    sqlid id;

    // check if we have this object inserted already
    if (m_address_poi_inserted.count(scoutid) > 0)
      {
        // GetObjectFeatures(m_database, address.object, name, name_en, phone, website);
        // std::cout << "AddrVisitor: " << address.name << " " << name << " " << scoutid << " inserted already\n";
        return true;
      }

    // new object, insert into set
    m_address_poi_inserted.insert(scoutid);

    GetObjectTypeCoor(m_database, address.object, type, coordinates);
    id = IDs.next();

    GetObjectFeatures(m_database, address.object, name, name_en, phone, postal_code, website);

    sqlite3pp::command cmd(m_db, "INSERT INTO object_primary_tmp (id, scoutid, name, name_extra, name_en, phone, postal_code, website, parent, longitude, latitude) VALUES (?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?)");
    cmd.binder() << id
                 << scoutid
                 << address.name
                 << name
                 << name_en
                 << phone
                 << postal_code
                 << website
                 << m_parent
                 << coordinates.GetLon()
                 << coordinates.GetLat();

    if (cmd.execute() != SQLITE_OK)
      std::cerr << "Error inserting addr " << address.name << "\n";

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
class PoiVisitor: public osmscout::POIVisitor
{
public:
  PoiVisitor(osmscout::DatabaseRef &database, sqlite3pp::database &db, sqlid parent):
    m_database(database),
    m_db(db),
    m_parent(parent)
  {}

  virtual bool Visit(const osmscout::AdminRegion &adminRegion, const osmscout::POI &poi)
  {
    std::string type;
    std::string name;
    std::string name_en;
    std::string phone;
    std::string postal_code;
    std::string website;
    osmscout::GeoCoord coordinates;
    std::string scoutid = poi.object.GetName();
    sqlid id;

    // check if we have this object inserted already
    if (m_address_poi_inserted.count(scoutid) > 0)
      {
        // GetObjectFeatures(m_database, poi.object, name, name_en, phone, website);
        // std::cout << "POIVisitor: " << poi.name << " " << name << " " << scoutid << " inserted already\n";
        return true;
      }

    // allow POIs without name only if they are of white-listed types
    GetObjectTypeCoor(m_database, poi.object, type, coordinates);
    GetObjectFeatures(m_database, poi.object, name, name_en, phone, postal_code, website);

    if (name.empty() && m_poi_types_whitelist.count(type) == 0)
      return true;

    // new object, insert into set
    m_address_poi_inserted.insert(scoutid);

    id = IDs.next();

    sqlite3pp::command cmd(m_db, "INSERT INTO object_primary_tmp (id, scoutid, name, name_extra, name_en, phone, postal_code, website, parent, longitude, latitude) VALUES (?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?)");
    cmd.binder() << id
                 << scoutid
                 << poi.name
                 << name
                 << name_en
                 << phone
                 << postal_code
                 << website
                 << m_parent
                 << coordinates.GetLon()
                 << coordinates.GetLat();

    if (cmd.execute() != SQLITE_OK)
      std::cerr << "Error inserting POI " << poi.name << "\n";

    IDs.set_parent(id, m_parent);
    write_type(m_db, id, type);

    return true;
  }

protected:
  osmscout::DatabaseRef &m_database;
  sqlite3pp::database &m_db;
  sqlid m_parent;
};

///////////////////////////////////////////////////////
class LocVisitor: public osmscout::LocationVisitor
{
public:
  LocVisitor(osmscout::DatabaseRef &database, sqlite3pp::database &db, sqlid parent):
    m_database(database),
    m_db(db),
    m_parent(parent)
  {}

  virtual bool Visit(const osmscout::AdminRegion &adminRegion, const osmscout::PostalArea &postalArea, const osmscout::Location &location)
  {
    std::string type;
    std::string name;
    std::string name_en;
    std::string phone;
    std::string postal_code = GeoNLP::Postal::normalize_postalcode(postalArea.name);
    std::string website;
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

    GetObjectFeatures(m_database, location.objects[ location.objects.size()/2 ], name, name_en, phone, postal_code, website);

    sqlite3pp::command cmd(m_db, "INSERT INTO object_primary_tmp (id, name, name_extra, name_en, phone, postal_code, website, parent, longitude, latitude) VALUES (?, ?, ?, ?, ?, ?, ?, ?, ?, ?)");
    cmd.binder() << id
                 << location.name
                 << name
                 << name_en
                 << phone
                 << postal_code
                 << website
                 << m_parent
                 << coordinates.GetLon()
                 << coordinates.GetLat();

    if (cmd.execute() != SQLITE_OK)
      std::cerr << "Error inserting location " << location.name << "\n";

    IDs.set_parent(id, m_parent, true);
    write_type(m_db, id, type);

    AddrVisitor addr(m_database, m_db, locID);
    m_database->GetLocationIndex()->VisitAddresses(adminRegion, postalArea, location, addr);

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
    std::string name;
    std::string name_en;
    std::string phone;
    std::string postal_code;
    std::string website;
    osmscout::GeoCoord coordinates;
    sqlid id;
    sqlid regionID;

    GetObjectTypeCoor(m_database, region.object, type, coordinates);
    regionID = id = IDs.next();
    IDs.set_id(region.regionOffset, id);

    GetObjectFeatures(m_database, region.object, name, name_en, phone, postal_code, website);

    sqlite3pp::command cmd(m_db, "INSERT INTO object_primary_tmp (id, name, name_extra, name_en, phone, postal_code, website, parent, longitude, latitude) VALUES (?, ?, ?, ?, ?, ?, ?, ?, ?, ?)");
    cmd.binder() << id
                 << region.name
                 << name
                 << name_en
                 << phone
                 << postal_code
                 << website
                 << IDs.get_id(region.parentRegionOffset)
                 << coordinates.GetLon()
                 << coordinates.GetLat();
    IDs.set_parent(id, region.parentRegionOffset, true);

    if (cmd.execute() != SQLITE_OK)
      std::cerr << "Error inserting region " << region.name << "\n";

    write_type(m_db, id, type);

    // record region aliases
    std::deque<std::string> saved_names; saved_names.push_back(region.name);
    if (region.aliasName.length() > 0 &&
        find(saved_names.begin(),
             saved_names.end(),
             region.aliasName) != saved_names.end() )
      {
        sqlite3pp::command cmd(m_db, "INSERT INTO object_primary_tmp (id, name, name_extra, name_en, phone, postal_code, website, parent, longitude, latitude) VALUES (?, ?, ?, ?, ?, ?, ?, ?, ?, ?)");

        GetObjectTypeCoor(m_database, region.aliasObject, type, coordinates);
        id = IDs.next();

        GetObjectFeatures(m_database, region.aliasObject, name, name_en, phone, postal_code, website, true);

        cmd.binder() << id
                     << region.aliasName
                     << name
                     << name_en
                     << phone
                     << postal_code
                     << website
                     << IDs.get_id(region.parentRegionOffset)
                     << coordinates.GetLon()
                     << coordinates.GetLat();
        if (cmd.execute() != SQLITE_OK)
          std::cerr << "Error inserting region alias " << region.aliasName << "\n";

        write_type(m_db, id, type);
        IDs.set_parent(id, region.parentRegionOffset);
      }

    for (auto alias: region.aliases)
      {
        sqlite3pp::command cmd(m_db, "INSERT INTO object_primary_tmp (id, name, name_extra, name_en, phone, postal_code, website, parent, longitude, latitude) VALUES (?, ?, ?, ?, ?, ?, ?, ?, ?, ?)");

        osmscout::ObjectFileRef object(alias.objectOffset, osmscout::refNode);
        GetObjectTypeCoor(m_database, object, type, coordinates);
        id = IDs.next();

        GetObjectFeatures(m_database, object, name, name_en, phone, postal_code, website, true);

        cmd.binder() << id
                     << alias.name
                     << name
                     << name_en
                     << phone
                     << postal_code
                     << website
                     << regionID
                     << coordinates.GetLon()
                     << coordinates.GetLat();
        if (cmd.execute() != SQLITE_OK)
          std::cerr << "Error inserting region alias 2 " << alias.name << "\n";

        write_type(m_db, id, type);
        IDs.set_parent(id, regionID);
      }

    LocVisitor loc(m_database, m_db, regionID);
    for (const osmscout::PostalArea &parea: region.postalAreas)
      {
        m_database->GetLocationIndex()->VisitLocations(region, parea, loc, false);
      }

    PoiVisitor poi(m_database, m_db, regionID);
    m_database->GetLocationIndex()->VisitPOIs(region, poi, false);

    // Add extra postal codes
    osmscout::FileOffset rid = region.object.GetFileOffset();
    if (additional_postal_codes.count(rid) > 0)
      {
        const auto &v = additional_postal_codes.at(rid);
        for (auto c: v)
          {
            sqlite3pp::command cmd(m_db, "INSERT INTO object_primary_tmp (id, postal_code, longitude, latitude) VALUES (?, ?, ?, ?)");
            id = IDs.next();
            cmd.binder() << id
                         << c.code
                         << c.longitude
                         << c.latitude;
            if (cmd.execute() != SQLITE_OK)
              std::cerr << "Error inserting additional postal code " << c.code << "\n";

            write_type(m_db, id, "postal code");
            IDs.set_parent(id, regionID);
          }
      }

    return osmscout::AdminRegionVisitor::visitChildren;
  };

protected:
  osmscout::DatabaseRef &m_database;
  sqlite3pp::database &m_db;
};

////////////////////////////////////////////////////////////////////////////
/// Libpostal normalization with search string expansion
void normalize_libpostal(sqlite3pp::database& db, std::string address_expansion_dir, bool verbose)
{
  struct tonorm
  {
    std::string name;
    sqlid id;
  };

  std::deque<tonorm> data;
  sqlite3pp::query qry(db, "SELECT id, name, name_extra, name_en FROM object_primary_tmp");
  for (auto v : qry)
    {
      tonorm d;
      sqlid id;
      char const *name, *name_extra, *name_en;
      v.getter() >> id >> name >> name_extra >> name_en;

      if (name == nullptr)
        continue; // no need to add empty name into search index

      d.id = id;

      d.name = name; data.push_back(d);
      if (name_extra) { d.name = name_extra; data.push_back(d); }
      if (name_en) { d.name = name_en; data.push_back(d); }
    }

  // make a new table for normalized names
  db.execute("DROP TABLE IF EXISTS normalized_name");
  db.execute("CREATE " TEMPORARY " TABLE normalized_name (prim_id INTEGER, name TEXT NOT NULL, PRIMARY KEY (name, prim_id))");

  // load libpostal
  if (!libpostal_setup() || !libpostal_setup_language_classifier())
    {
      std::cerr << "Failure to load libpostal" << std::endl;
      return;
    }

  std::vector<char> aed(address_expansion_dir.begin(), address_expansion_dir.end());
  aed.push_back(0);
  if ( (address_expansion_dir.empty() && !libpostal_setup_parser() ) ||
       (!address_expansion_dir.empty() && !libpostal_setup_parser_datadir(aed.data())) )
    {
      std::cerr << "Failure to load libpostal parser" << std::endl;
      return;
    }

  // normalize all names
  size_t num_expansions;
  size_t num_doubles_dropped = 0;
  libpostal_normalize_options_t options = libpostal_get_default_options();
  std::vector<char> charbuff;
  for (tonorm &d: data)
    {
      charbuff.resize(d.name.length() + 1);
      std::copy(d.name.c_str(), d.name.c_str() + d.name.length() + 1, charbuff.begin());

      if (verbose) std::cout << d.name << ": " << std::flush;

      // check for sanity before we proceed with expansion
      if ( d.name.length() > LENGTH_STARTING_SUSP_CHECK )
        {
          size_t digits_space = 0;
          for (size_t i=0; i < d.name.length(); ++i)
            if ( std::isdigit( charbuff[i] ) || std::isspace( charbuff[i] ) )
              digits_space++;

          if ( (digits_space*1.0) / d.name.length() > 0.5 )
            {
              std::cout << "Warning: dropping suspicious name: " << d.name << "\n";
              continue;
            }
        }

      // check if there are too many commas
      if ( std::count(d.name.begin(), d.name.end(), ',') > MAX_COMMAS )
        {
          std::cout << "Warning: dropping suspicious name - too many commas: " << d.name << "\n";
          continue;
        }

      char **expansions = libpostal_expand_address(charbuff.data(), options, &num_expansions);

      if ( num_expansions > MAX_NUMBER_OF_EXPANSIONS )
        {
          std::cout << "Warning: large number [" << num_expansions << "] of normalization expansions of " << d.name
                    << " - dropping it from the table [" << d.id << "]\n";
          // for (size_t i=0; i < 10 && i < num_expansions; i++)
          //   std::cout << "   example expansion: " << expansions[i] << "\n";
          // std::cout << "\n";

          continue; // don't insert it, its probably wrong anyway
        }

      for (size_t i = 0; i < num_expansions; i++)
        {
          sqlite3pp::command cmd(db, "INSERT INTO normalized_name (prim_id, name) VALUES (?,?)");
          std::string s = expansions[i];
          cmd.binder() << d.id
                       << s;
          if (cmd.execute() != SQLITE_OK)
            {
              // std::cerr << "Error inserting: " << d.id << " " << s << std::endl;
              num_doubles_dropped++;
            }

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
                  try {
                    sqlite3pp::command cmd(db, "INSERT INTO normalized_name (prim_id, name) VALUES (?,?)");
                    std::string s = expansions[i];
                    cmd.binder() << d.id
                                 << s.substr(pos);
                    if (cmd.execute() != SQLITE_OK)
                      {
                        //std::cerr << "Error inserting: " << d.id << " " << s << std::endl;
                        num_doubles_dropped++;
                      }
                  }
                  catch (sqlite3pp::database_error e)
                    {
                      num_doubles_dropped++;
                    }

                }
            }

        }

      // Free expansions
      libpostal_expansion_array_destroy(expansions, num_expansions);

      if (verbose) std::cout << "done" << std::endl;
    }

  std::cout << "Redundant records skipped: " << num_doubles_dropped << "\n";

  // Teardown libpostal
  libpostal_teardown_parser();
  libpostal_teardown();
  libpostal_teardown_language_classifier();
}

////////////////////////////////////////////////////////////////////////////
/// Libpostal normalization with search string expansion
void normalized_to_final(sqlite3pp::database& db, std::string path)
{
  std::cout << "Inserting normalized data into MARISA trie" << std::endl;

  marisa::Keyset keyset;

  {
    sqlite3pp::query qry(db, "SELECT name FROM normalized_name");
    for (auto v : qry)
      {
        std::string name;
        v.getter() >> name;
        keyset.push_back(name.c_str());
      }
  }

  marisa::Trie trie;
  trie.build(keyset);
  trie.save(GeoNLP::Geocoder::name_normalized_trie(path).c_str());

  struct norm
  {
    std::string name;
    sqlid prim_id;
  };

  std::deque<norm> data;
  {
    sqlite3pp::query qry(db, "SELECT name, prim_id FROM normalized_name");
    for (auto v : qry)
      {
        norm d;
        v.getter() >> d.name >> d.prim_id;
        data.push_back(d);
      }
  }

  std::map< GeoNLP::Geocoder::index_id_key, std::vector<GeoNLP::Geocoder::index_id_value> > bdata;
  for (auto d: data)
    {
      marisa::Agent agent;
      agent.set_query(d.name.c_str());
      if (trie.lookup(agent))
        {
          GeoNLP::Geocoder::index_id_key k = agent.key().id();
          if ( bdata.count(k) == 0 ) bdata[k] = std::vector<GeoNLP::Geocoder::index_id_value>();
          bdata[k].push_back( d.prim_id );
        }
      else
        {
          std::cerr << "Error: cannot find in MARISA trie: " <<  d.name << std::endl;
        }
    }

  {
    // create the database object
    kyotocabinet::HashDB db;

    db.tune_options(kyotocabinet::HashDB::TSMALL | kyotocabinet::HashDB::TLINEAR);
    db.tune_alignment(0);
    db.tune_defrag(8);

    // open the database
    if (!db.open(GeoNLP::Geocoder::name_normalized_id(path).c_str(),
                 kyotocabinet::HashDB::OWRITER | kyotocabinet::HashDB::OCREATE))
      {
        std::cerr << "open error: " << db.error().name() << std::endl;
        return;
      }

    std::vector<std::string> keys;
    for (auto a: bdata)
      keys.push_back( GeoNLP::Geocoder::make_id_key(a.first) );

    std::sort( keys.begin(), keys.end() );

    for (auto key: keys)
      {
        std::vector<GeoNLP::Geocoder::index_id_value> &d = bdata[GeoNLP::Geocoder::get_id_key(key)];
        std::sort(d.begin(), d.end());
        std::string value = GeoNLP::Geocoder::make_id_value( d );
        if (!db.set(key, value))
          {
            std::cerr << "set error: " << db.error().name() << std::endl;
            return;
          }
      }

    std::cout << "Number of records in normalized id database: " << db.count() << "\n";

    db.close();
  }

  db.execute("DROP TABLE IF EXISTS normalized_name");
}


// trim from start (in place)
static inline void ltrim(std::string &s) {
    s.erase(s.begin(), std::find_if(s.begin(), s.end(), [](int ch) {
        return !std::isspace(ch);
    }));
}

// trim from end (in place)
static inline void rtrim(std::string &s) {
    s.erase(std::find_if(s.rbegin(), s.rend(), [](int ch) {
        return !std::isspace(ch);
    }).base(), s.end());
}

// trim from both ends (in place)
static inline void trim(std::string &s) {
    ltrim(s);
    rtrim(s);
}


////////////////////////////////////////////////////////////////////////////
// MAIN

int main(int argc, char* argv[])
{
  if (argc == 2)
    {
      std::string option = argv[1];
      if ( option == "--version" )
        std::cout << GeoNLP::Geocoder::version << "\n";
      return 0;
    }

  if (argc<4)
    {
      std::cerr << "importer <libosmscout map directory> <geocoder-nlp database directory> <whitelist file> [postal_codes.csv] [<postal_country_parser_code>] [address_parser_directory] [verbose]\n";
      std::cerr << "When using optional parameters, you have to specify all of the perceiving ones\n";
      return 1;
    }

  std::string map = argv[1];
  std::string database_path = argv[2];
  std::string whitelist_file = argv[3];
  std::string postcodes_fname;
  std::string postal_country_parser;
  std::string postal_address_parser_dir;
  bool verbose_address_expansion = false;

  if (argc > 4) postcodes_fname = argv[4];
  if (argc > 5) postal_country_parser = argv[5];
  if (argc > 6) postal_address_parser_dir = argv[6];
  if (argc > 7 && strcmp("verbose", argv[7])==0 ) verbose_address_expansion = true;

  std::cout << "Starting import: " << map << " -> " << database_path << "\n";

  // fill white list
  {
    std::ifstream f(whitelist_file);
    std::string line;
    if (!f)
      {
        std::cerr << "Failed to open whitelist file: " << whitelist_file << std::endl;
        return -1;
      }

    while (std::getline(f, line))
      {
        trim(line);
        if (!line.empty())
          m_poi_types_whitelist.insert(line);
        std::cout << "Whitelisted: " << line << "\n";
      }
  }

  // load the database and proceed with import
  osmscout::DatabaseParameter databaseParameter;
  osmscout::DatabaseRef database(new osmscout::Database(databaseParameter));

  if (!database->Open(map.c_str()))
    {
      std::cerr << "Cannot open libosmscout database: " << map << std::endl;
      return 1;
    }

  nameReader = new NameReader(*database->GetTypeConfig());
  nameAltReader = new NameAltReader(*database->GetTypeConfig());
  phoneReader = new PhoneReader(*database->GetTypeConfig());
  postalCodeReader = new PostalCodeReader(*database->GetTypeConfig());
  websiteReader = new WebsiteReader(*database->GetTypeConfig());

  // reverse geocode all postal codes submitted as a separate file
  if (!postcodes_fname.empty())
    {
      std::ifstream fin(postcodes_fname.c_str());
      osmscout::LocationDescriptionService locationService(database);
      std::string line;
      size_t postal_counter=0;
      while (getline(fin,line))
        {
          boost::tokenizer< boost::escaped_list_separator<char> > tok(line);
          std::vector<std::string> cells;
          cells.assign(tok.begin(),tok.end());
          if (cells.size() != 4 || cells[0] == "id")
            continue;
          std::string code = cells[1];
          double latitude = std::stod(cells[2]);
          double longitude = std::stod(cells[3]);
          osmscout::GeoCoord coordinates(latitude, longitude);
          std::list<osmscout::LocationDescriptionService::ReverseLookupResult> results;
          {
            static int o = 0;
            if (o % 10000 == 0) {
              std::cout << "Postal codes parsed: " << postal_counter << std::endl;
              o = 0;
            }
            ++o;
            ++postal_counter;
          }
          if (locationService.ReverseLookupRegion(coordinates, results))
            if (results.size() > 0)
              {
                const auto r = results.back();
                if (r.adminRegion)
                  {
                    osmscout::FileOffset admin = r.adminRegion->object.GetFileOffset();
                    PostalCode c;
                    c.code = GeoNLP::Postal::normalize_postalcode(code);
                    c.latitude = latitude;
                    c.longitude = longitude;
                    additional_postal_codes[admin].push_back(c);
                    // std::cout << code << " " << r.adminRegion->name << " "
                    //           << r.adminRegion->object.GetFileOffset() << " " << r.adminRegion->parentRegionOffset << " "
                    //           << results.size() << "\n";
                  }
              }
        }
    }
  //return -1;

  // reverse geocoding: done


  sqlite3pp::database db(GeoNLP::Geocoder::name_primary(database_path).c_str());

  db.execute( "PRAGMA journal_mode = OFF" );
  db.execute( "PRAGMA synchronous = OFF" );
  db.execute( "PRAGMA cache_size = 2000000" );
  db.execute( "PRAGMA temp_store = 2" );
  db.execute( "BEGIN TRANSACTION" );
  db.execute( "DROP TABLE IF EXISTS type" );
  db.execute( "DROP TABLE IF EXISTS object_primary" );
  db.execute( "DROP TABLE IF EXISTS object_primary_tmp" );
  db.execute( "DROP TABLE IF EXISTS object_primary_tmp2" );
  db.execute( "DROP TABLE IF EXISTS boxids" );
  db.execute( "DROP TABLE IF EXISTS object_type" );
  db.execute( "DROP TABLE IF EXISTS object_type_tmp" );
  db.execute( "DROP TABLE IF EXISTS hierarchy" );
  db.execute( "DROP TABLE IF EXISTS object_primary_rtree" );

  db.execute( "CREATE " TEMPORARY " TABLE object_primary_tmp (id INTEGER PRIMARY KEY AUTOINCREMENT, scoutid TEXT, name TEXT, name_extra TEXT, name_en TEXT, phone TEXT, postal_code TEXT, website TEXT, parent INTEGER, latitude REAL, longitude REAL)");
  db.execute( "CREATE " TEMPORARY " TABLE object_type_tmp (prim_id INTEGER, type TEXT NOT NULL, FOREIGN KEY (prim_id) REFERENCES objects_primary_tmp(id))" );
  db.execute( "CREATE TABLE hierarchy (prim_id INTEGER PRIMARY KEY, last_subobject INTEGER, "
              "FOREIGN KEY (prim_id) REFERENCES objects_primary(id), FOREIGN KEY (last_subobject) REFERENCES objects_primary(id))" );

  std::cout << "Preliminary filling of the database" << std::endl;

  osmscout::LocationIndexRef locationIndex = database->GetLocationIndex();
  AdminVisitor vis_admin(database, db);
  locationIndex->VisitAdminRegions(vis_admin);
  IDs.write_hierarchy(db);

  // cleanup from duplicated names
  // db.execute( "UPDATE object_primary_tmp SET name_extra=NULL WHERE name_extra='' OR name=name_extra" );
  // db.execute( "UPDATE object_primary_tmp SET name_en=NULL WHERE name_en='' OR name=name_en" );
  db.execute( "UPDATE object_primary_tmp SET name_extra='' WHERE name=name_extra" );
  db.execute( "UPDATE object_primary_tmp SET name_en='' WHERE name=name_en" );

  std::cout << "Reorganizing database tables" << std::endl;

  db.execute( "CREATE TABLE type (id INTEGER PRIMARY KEY AUTOINCREMENT, name TEXT)" );
  db.execute( "INSERT INTO type (name) SELECT DISTINCT type FROM object_type_tmp" );
  db.execute( "CREATE " TEMPORARY " TABLE object_primary_tmp2 (id INTEGER PRIMARY KEY AUTOINCREMENT, "
              "name TEXT, name_extra TEXT, name_en TEXT, phone TEXT, postal_code TEXT, website TEXT, parent INTEGER, type_id INTEGER, latitude REAL, longitude REAL, boxstr TEXT, "
              "FOREIGN KEY (type_id) REFERENCES type(id))");

  db.execute( "INSERT INTO object_primary_tmp2 (id, name, name_extra, name_en, phone, postal_code, website, parent, type_id, latitude, longitude, boxstr) "
              "SELECT p.id, p.name, p.name_extra, p.name_en, p.phone, p.postal_code, p.website, p.parent, type.id, p.latitude, p.longitude, "
              // LINE BELOW DETERMINES ROUNDING USED FOR BOXES
              "CAST(CAST(p.latitude*100 AS INTEGER) AS TEXT) || ',' || CAST(CAST(p.longitude*100 AS INTEGER) AS TEXT) "
              "FROM object_primary_tmp p JOIN object_type_tmp tt ON p.id=tt.prim_id "
              "JOIN type ON tt.type=type.name" );

  db.execute( "CREATE " TEMPORARY " TABLE boxids (id INTEGER PRIMARY KEY AUTOINCREMENT, boxstr TEXT, CONSTRAINT struni UNIQUE (boxstr))" );
  db.execute( "INSERT INTO boxids (boxstr) SELECT DISTINCT boxstr FROM object_primary_tmp2" );

  db.execute( "CREATE TABLE object_primary (id INTEGER PRIMARY KEY AUTOINCREMENT, name TEXT, name_extra TEXT, name_en TEXT, phone TEXT, postal_code TEXT, website TEXT, "
              "parent INTEGER, type_id INTEGER, latitude REAL, longitude REAL, box_id INTEGER, "
              "FOREIGN KEY (type_id) REFERENCES type(id))" );
  db.execute( "INSERT INTO object_primary (id, name, name_extra, name_en, phone, postal_code, website, parent, type_id, latitude, longitude, box_id) "
              "SELECT o.id, name, name_extra, name_en, phone, postal_code, website, parent, type_id, latitude, longitude, b.id FROM object_primary_tmp2 o JOIN boxids b ON o.boxstr=b.boxstr" );

  db.execute( "DROP INDEX IF EXISTS idx_object_primary_box" );
  db.execute( "CREATE INDEX idx_object_primary_box ON object_primary (box_id)" );

  db.execute( "DROP INDEX IF EXISTS idx_object_primary_postal_code" );
  db.execute( "CREATE INDEX idx_object_primary_postal_code ON object_primary (postal_code)" );

  std::cout << "Normalize using libpostal" << std::endl;

  normalize_libpostal(db, postal_address_parser_dir, verbose_address_expansion);
  normalized_to_final(db, database_path);

  // Create R*Tree for nearest neighbor search
  std::cout << "Populating R*Tree" << std::endl;
  db.execute( "CREATE VIRTUAL TABLE object_primary_rtree USING rtree(id, minLat, maxLat, minLon, maxLon)" );
  db.execute( "INSERT INTO object_primary_rtree (id, minLat, maxLat, minLon, maxLon) "
              "SELECT box_id, min(latitude), max(latitude), min(longitude), max(longitude) from object_primary group by box_id" );

  // Recording version
  db.execute( "DROP TABLE IF EXISTS meta" );
  db.execute( "CREATE TABLE meta (key TEXT, value TEXT)" );
  {
    sqlite3pp::command cmd(db,"INSERT INTO meta (key, value) VALUES (?, ?)");
    std::ostringstream ss; ss << GeoNLP::Geocoder::version;
    cmd.binder() << "version"
                 << ss.str().c_str();
    if (cmd.execute() != SQLITE_OK)
      std::cerr << "WriteSQL: error inserting version information\n";
  }

  if ( !postal_country_parser.empty() )
    {
      std::cout << "Recording postal parser country preference: " <<  postal_country_parser << "\n";
      std::string cmd = "INSERT INTO meta (key, value) VALUES (\"postal:country:parser\", \"" + postal_country_parser + "\")";
      db.execute( cmd.c_str() );
    }

  db.execute( "END TRANSACTION" );
  db.execute( "VACUUM" );
  db.execute( "ANALYZE" );

  std::cout << "Done\n";

  return 0;
}
