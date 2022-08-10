#include "hierarchyitem.h"
#include "utils.h"

#include <fstream>
#include <iostream>
#include <stdexcept>

std::set<std::string> HierarchyItem::s_priority_types;
std::set<std::string> HierarchyItem::s_skip_types;

static std::string allowed_type_chars = "abcdefghijklmnopqrstuvwxyz_-";

HierarchyItem::HierarchyItem(const pqxx::row &row)
{
  m_id          = row["place_id"].as<hindex>(0);
  m_linked_id   = row["linked_place_id"].as<hindex>(0);
  m_parent_id   = row["parent_place_id"].as<hindex>(0);
  m_country     = row["country_code"].as<std::string>("");
  m_type        = geocoder_type(row["class"].as<std::string>(""), row["type"].as<std::string>(""));
  m_housenumber = row["housenumber"].as<std::string>("");
  m_postcode    = row["postcode"].as<std::string>("");
  m_latitude    = row["latitude"].as<float>(0);
  m_longitude   = row["longitude"].as<float>(0);
  m_osm_id      = row["osm_id"].as<uint64_t>(0);

  m_data_name  = parse_to_map(row["name"].as<std::string>(""));
  m_data_extra = parse_to_map(row["extra"].as<std::string>(""));

  set_names();
}

// trim from start (in place)
static inline void ltrim(std::string &s)
{
  s.erase(s.begin(), std::find_if(s.begin(), s.end(), [](int ch) { return !std::isspace(ch); }));
}

// trim from end (in place)
static inline void rtrim(std::string &s)
{
  s.erase(std::find_if(s.rbegin(), s.rend(), [](int ch) { return !std::isspace(ch); }).base(),
          s.end());
}

// trim from both ends (in place)
static inline void trim(std::string &s)
{
  ltrim(s);
  rtrim(s);
}

static std::set<std::string> load_list(const std::string &fname)
{
  std::set<std::string> d;
  if (fname.empty())
    return d;

  std::ifstream f(fname);
  std::string   line;
  if (!f)
    {
      std::cerr << "Failed to open a file: " << fname << std::endl;
      throw std::runtime_error("File cannot be opened");
    }

  while (std::getline(f, line))
    {
      trim(line);
      if (!line.empty())
        d.insert(line);
    }

  return d;
}

void HierarchyItem::load_priority_list(const std::string &fname)
{
  s_priority_types = load_list(fname);
}

void HierarchyItem::load_skip_list(const std::string &fname)
{
  s_skip_types = load_list(fname);
}

bool HierarchyItem::keep() const
{
  if (m_type.find_first_not_of(allowed_type_chars) != std::string::npos)
    {
      std::cout << "Dropping " << m_type << "\n";
      return false;
    }
  if (s_skip_types.count(m_type) > 0)
    return false;
  return !m_name.empty() || s_priority_types.count(m_type) > 0;
}

bool HierarchyItem::is_duplicate(std::shared_ptr<HierarchyItem> item) const
{
  if (s_priority_types.count(m_type) > 0)
    return false;

  if (m_name != item->m_name || m_postcode != item->m_postcode)
    return false;

  if (m_type == item->m_type || same_starts_with("building", m_type, item->m_type)
      || same_starts_with("highway", m_type, item->m_type))
    return true;

  return false;
}

void HierarchyItem::add_child(std::shared_ptr<HierarchyItem> child)
{
  m_children.push_back(child);
  child->set_parent(m_id);
}

void HierarchyItem::add_linked(std::shared_ptr<HierarchyItem> linked)
{
  m_data_name.insert(linked->m_data_name.begin(), linked->m_data_name.end());
  m_data_extra.insert(linked->m_data_extra.begin(), linked->m_data_extra.end());
  set_names();
}

void HierarchyItem::set_names()
{
  m_name = get_with_def(m_data_name, "name");
  m_name_extra.clear();
  if (!m_housenumber.empty())
    {
      m_name_extra = m_name;
      m_name       = m_housenumber;
    }

  if (m_name_extra.empty())
    m_name_extra = get_with_def(m_data_extra, "brand");
}

void HierarchyItem::set_parent(hindex parent, bool force)
{
  if (!force && m_parent_id != parent && m_parent_id != 0 && parent != 0)
    {
      std::cout << "New parent (" << parent << ") for " << m_id << " does not match old one ("
                << m_parent_id << ")\n";
      throw std::runtime_error("Mismatch between new and old parent");
    }
  m_parent_id = parent;
  //   for (auto c : m_children)
  //     c->set_parent(m_id, force);
}

void HierarchyItem::cleanup_children(bool duplicate_only)
{
  // as a result of this run, children that are supposed to be kept are staying in children
  // property. all disposed ones are still pointed to via Hierarchy map, but should not be accessed
  // while moving along hierarchy for indexing or writing it
  {
    std::deque<std::shared_ptr<HierarchyItem> > children;
    for (auto item : m_children)
      {
        item->cleanup_children();
        if (item->keep())
          children.push_back(item);
        else
          children.insert(children.end(), item->m_children.begin(), item->m_children.end());
      }
    m_children = children;
  }

  // check for duplicates
  bool had_duplicates = false;
  for (size_t child_index = 0; child_index < m_children.size(); ++child_index)
    {
      std::shared_ptr<HierarchyItem>              item = m_children[child_index];
      std::deque<std::shared_ptr<HierarchyItem> > children;
      std::deque<std::shared_ptr<HierarchyItem> > duplicates;

      children.insert(children.end(), m_children.begin(), m_children.begin() + child_index + 1);

      for (size_t i = child_index + 1; i < m_children.size(); ++i)
        if (m_children[i]->is_duplicate(item))
          duplicates.push_back(m_children[i]);
        else
          children.push_back(m_children[i]);

      // merge duplicates
      for (auto &i : duplicates)
        {
          had_duplicates = true;
          item->add_linked(i);
          item->m_children.insert(item->m_children.end(), i->m_children.begin(),
                                  i->m_children.end());
          for (auto &i_children : i->m_children)
            i_children->set_parent(item->m_id, true);
        }

      if (had_duplicates)
        item->cleanup_children(true);

      m_children = children;
    }

  // set parent, forced
  for (auto item : m_children)
    item->set_parent(m_id, true);
}

sqlid HierarchyItem::index(sqlid idx, sqlid parent)
{
  if (!keep())
    throw std::runtime_error("Trying to index a location that was not supposed to be kept");
  m_my_index     = idx;
  m_parent_index = parent;
  ++idx;
  for (auto item : m_children)
    idx = item->index(idx, m_my_index);
  m_last_child_index = idx - 1;
  return idx;
}

void HierarchyItem::write(sqlite3pp::database &db) const
{
  if (!keep())
    throw std::runtime_error("Trying to write a location that was not supposed to be kept");

  // primary data
  std::string name_en = get_with_def(m_data_name, "name:en");
  std::string phone   = get_with_def(m_data_extra, "phone");
  std::string website = get_with_def(m_data_extra, "website");

  {
    sqlite3pp::command cmd(db, "INSERT INTO object_primary_tmp (id, postgres_id, name, name_extra, "
                               "name_en, phone, postal_code, website, parent, longitude, "
                               "latitude) VALUES (?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?)");
    cmd.binder() << m_my_index << (int)m_id << m_name << m_name_extra << name_en << phone
                 << m_postcode << website << m_parent_index << m_longitude << m_latitude;
    if (cmd.execute() != SQLITE_OK)
      std::cerr << "WriteSQL : error inserting primary data for " << m_id << ", " << m_my_index
                << "\n";
  }

  // type
  {
    std::string command
        //= "INSERT INTO object_type_tmp (prim_id, type) VALUES (?, \"" + type + "\")";
        = "INSERT INTO object_type_tmp (prim_id, type) VALUES (?, ?)";
    sqlite3pp::command cmd(db, command.c_str());
    cmd.binder() << m_my_index << m_type;
    if (cmd.execute() != SQLITE_OK)
      std::cerr << "WriteSQL: error inserting type for " << m_id << ", " << m_my_index << "\n";
  }

  // hierarchy
  if (m_last_child_index > m_my_index)
    {
      sqlite3pp::command cmd(db, "INSERT INTO hierarchy (prim_id, last_subobject) VALUES (?, ?)");
      cmd.binder() << m_my_index << m_last_child_index;
      if (cmd.execute() != SQLITE_OK)
        std::cerr << "WriteSQL: error inserting hierarchy for " << m_id << ", " << m_my_index
                  << " - " << m_last_child_index << "\n";
    }

  // children
  for (const auto &c : m_children)
    c->write(db);
}

void HierarchyItem::print_item(unsigned int offset) const
{
  std::cout << std::string(offset, ' ') << "- " << m_id << " ";
  if (!m_housenumber.empty())
    std::cout << "house " << m_housenumber << " ";
  for (const auto &i : m_data_name)
    std::cout << i.first << ": " << i.second << " ";
  std::cout << "(" << m_my_index << " " << m_last_child_index << ": "
            << m_last_child_index - m_my_index << ": " << m_parent_id << ", " << m_country
            << ", osmid=" << m_osm_id << ")\n";
}

void HierarchyItem::print_branch(unsigned int offset) const
{
  print_item(offset);
  if (m_children.size())
    std::cout << std::string(offset + 2, ' ') << "|\n";
  for (auto c : m_children)
    c->print_branch(offset + 3);
}
