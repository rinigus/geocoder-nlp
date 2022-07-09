#include "normalization.h"
#include "config.h"
#include "geocoder.h"

#include <kchashdb.h>
#include <libpostal/libpostal.h>
#include <marisa.h>

////////////////////////////////////////////////////////////////////////////
/// Libpostal normalization with search string expansion
void normalize_libpostal(sqlite3pp::database &db, std::string address_expansion_dir, bool verbose)
{
  struct tonorm
  {
    std::string name;
    sqlid       id;
  };

  std::deque<tonorm> data;
  sqlite3pp::query   qry(db, "SELECT id, name, name_extra, name_en FROM object_primary_tmp");
  for (auto v : qry)
    {
      tonorm      d;
      sqlid       id;
      char const *name, *name_extra, *name_en;
      v.getter() >> id >> name >> name_extra >> name_en;

      if (name == nullptr)
        continue; // no need to add empty name into search index

      d.id = id;

      d.name = name;
      data.push_back(d);
      if (name_extra)
        {
          d.name = name_extra;
          data.push_back(d);
        }
      if (name_en)
        {
          d.name = name_en;
          data.push_back(d);
        }
    }

  // make a new table for normalized names
  db.execute("DROP TABLE IF EXISTS normalized_name");
  db.execute(
      "CREATE " TEMPORARY
      " TABLE normalized_name (prim_id INTEGER, name TEXT NOT NULL, PRIMARY KEY (name, prim_id))");

  // load libpostal
  if (!libpostal_setup() || !libpostal_setup_language_classifier())
    {
      std::cerr << "Failure to load libpostal" << std::endl;
      return;
    }

  std::vector<char> aed(address_expansion_dir.begin(), address_expansion_dir.end());
  aed.push_back(0);
  if ((address_expansion_dir.empty() && !libpostal_setup_parser())
      || (!address_expansion_dir.empty() && !libpostal_setup_parser_datadir(aed.data())))
    {
      std::cerr << "Failure to load libpostal parser" << std::endl;
      return;
    }

  // normalize all names
  size_t                        num_expansions;
  size_t                        num_doubles_dropped = 0;
  libpostal_normalize_options_t options             = libpostal_get_default_options();
  std::vector<char>             charbuff;
  for (tonorm &d : data)
    {
      charbuff.resize(d.name.length() + 1);
      std::copy(d.name.c_str(), d.name.c_str() + d.name.length() + 1, charbuff.begin());

      if (verbose)
        std::cout << d.name << ": " << std::flush;

      // check for sanity before we proceed with expansion
      if (d.name.length() > LENGTH_STARTING_SUSP_CHECK)
        {
          size_t digits_space = 0;
          for (size_t i = 0; i < d.name.length(); ++i)
            if (std::isdigit(charbuff[i]) || std::isspace(charbuff[i]))
              digits_space++;

          if ((digits_space * 1.0) / d.name.length() > 0.5)
            {
              std::cout << "Warning: dropping suspicious name: " << d.name << "\n";
              continue;
            }
        }

      // check if there are too many commas
      if (std::count(d.name.begin(), d.name.end(), ',') > MAX_COMMAS)
        {
          std::cout << "Warning: dropping suspicious name - too many commas: " << d.name << "\n";
          continue;
        }

      // insert normalized, but not expanded string
      {
        char *normalized = libpostal_normalize_string(charbuff.data(),
                                                      LIBPOSTAL_NORMALIZE_DEFAULT_STRING_OPTIONS);
        if (normalized != NULL)
          {
            sqlite3pp::command cmd(db, "INSERT INTO normalized_name (prim_id, name) VALUES (?,?)");
            std::string        s = normalized;
            cmd.binder() << d.id << s;
            if (cmd.execute() != SQLITE_OK)
              {
                // std::cerr << "Error inserting: " << d.id << " " << s << std::endl;
                num_doubles_dropped++;
              }

            free(normalized);
          }
      }

      char **expansions = libpostal_expand_address(charbuff.data(), options, &num_expansions);

      if (num_expansions > MAX_NUMBER_OF_EXPANSIONS)
        {
          std::cout << "Warning: large number [" << num_expansions
                    << "] of normalization expansions of " << d.name
                    << " - dropping it from the table [" << d.id << "]\n";
          // for (size_t i=0; i < 10 && i < num_expansions; i++)
          //   std::cout << "   example expansion: " << expansions[i] << "\n";
          // std::cout << "\n";

          continue; // don't insert it, its probably wrong anyway
        }

      for (size_t i = 0; i < num_expansions; i++)
        {
          sqlite3pp::command cmd(db, "INSERT INTO normalized_name (prim_id, name) VALUES (?,?)");
          std::string        s = expansions[i];
          cmd.binder() << d.id << s;
          if (cmd.execute() != SQLITE_OK)
            {
              // std::cerr << "Error inserting: " << d.id << " " << s << std::endl;
              num_doubles_dropped++;
            }

          // to cover the street names that have Dr. or the firstname
          // in the front of the mainly used name, add substrings into
          // the normalized table as well
          const size_t max_substrings = 2;
          size_t       pos            = 1;
          for (size_t sbs = 0; sbs < max_substrings && pos < s.length(); ++sbs)
            {
              bool spacefound = false;
              for (; pos < s.length(); ++pos)
                {
                  char c = s[pos];
                  if (c == ' ')
                    spacefound = true;
                  if (spacefound && c != ' ')
                    break;
                }

              if (pos < s.length())
                {
                  try
                    {
                      sqlite3pp::command cmd(
                          db, "INSERT INTO normalized_name (prim_id, name) VALUES (?,?)");
                      std::string s = expansions[i];
                      cmd.binder() << d.id << s.substr(pos);
                      if (cmd.execute() != SQLITE_OK)
                        {
                          // std::cerr << "Error inserting: " << d.id << " " << s << std::endl;
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

      if (verbose)
        std::cout << "done" << std::endl;
    }

  std::cout << "Redundant records skipped: " << num_doubles_dropped << "\n";

  // Teardown libpostal
  libpostal_teardown_parser();
  libpostal_teardown();
  libpostal_teardown_language_classifier();
}

////////////////////////////////////////////////////////////////////////////
/// Libpostal normalization with search string expansion
void normalized_to_final(sqlite3pp::database &db, std::string path)
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
    sqlid       prim_id;
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

  std::map<GeoNLP::Geocoder::index_id_key, std::vector<GeoNLP::Geocoder::index_id_value> > bdata;
  for (auto d : data)
    {
      marisa::Agent agent;
      agent.set_query(d.name.c_str());
      if (trie.lookup(agent))
        {
          GeoNLP::Geocoder::index_id_key k = agent.key().id();
          if (bdata.count(k) == 0)
            bdata[k] = std::vector<GeoNLP::Geocoder::index_id_value>();
          bdata[k].push_back(d.prim_id);
        }
      else
        {
          std::cerr << "Error: cannot find in MARISA trie: " << d.name << std::endl;
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
    for (auto a : bdata)
      keys.push_back(GeoNLP::Geocoder::make_id_key(a.first));

    std::sort(keys.begin(), keys.end());

    for (auto key : keys)
      {
        std::vector<GeoNLP::Geocoder::index_id_value> &d = bdata[GeoNLP::Geocoder::get_id_key(key)];
        std::sort(d.begin(), d.end());
        std::string value = GeoNLP::Geocoder::make_id_value(d);
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
