#include "hierarchyitem.h"
#include "utils.h"

#include <iostream>
#include <nlohmann/json.hpp>
#include <stdexcept>
#include <string>

using json = nlohmann::json;

static std::map<std::string, std::string> parse_to_map(const std::string &js)
{
  std::map<std::string, std::string> m;
  if (js.size())
    {
      json j = json::parse(js);
      for (auto v : j.items())
        m[v.key()] = v.value();
    }
  return m;
}

HierarchyItem::HierarchyItem(const pqxx::row &row)
{
  m_id          = row["place_id"].as<hindex>(0);
  m_linked_id   = row["linked_place_id"].as<hindex>(0);
  m_parent_id   = row["parent_place_id"].as<hindex>(0);
  m_country     = row["country_code"].as<std::string>("");
  m_class       = row["class"].as<std::string>("");
  m_type        = row["type"].as<std::string>("");
  m_housenumber = row["housenumber"].as<std::string>("");
  m_postcode    = row["postcode"].as<std::string>("");
  m_latitude    = row["latitude"].as<float>(0);
  m_longitude   = row["longitude"].as<float>(0);

  m_name  = parse_to_map(row["name"].as<std::string>(""));
  m_extra = parse_to_map(row["extra"].as<std::string>(""));
}

void HierarchyItem::add_child(std::shared_ptr<HierarchyItem> child)
{
  m_children.push_back(child);
}

void HierarchyItem::set_parent(hindex parent)
{
  if (m_parent_id != parent && m_parent_id != 0)
    {
      std::cout << "New parent (" << parent << ") for " << m_id << " does not match old one ("
                << m_parent_id << ")\n";
      throw std::runtime_error("Mismatch between new and old parent");
    }
  m_parent_id = parent;
  for (auto c : m_children)
    c->set_parent(m_id);
}

sqlid HierarchyItem::index(sqlid idx, sqlid parent)
{
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
  // primary data
  std::string name    = get_with_def(m_name, "name");
  std::string name_en = get_with_def(m_name, "name:en");
  std::string phone   = get_with_def(m_extra, "phone");
  std::string website = get_with_def(m_extra, "website");

  std::string name_extra;
  if (!m_housenumber.empty())
    {
      name_extra = name;
      name       = m_housenumber;
    }

  if (name_extra.empty())
    name_extra = get_with_def(m_extra, "brand");
  {
    sqlite3pp::command cmd(db, "INSERT INTO object_primary_tmp (id, postgres_id, name, name_extra, "
                               "name_en, phone, postal_code, website, parent, longitude, "
                               "latitude) VALUES (?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?)");
    cmd.binder() << m_my_index << (int)m_id << name << name_extra << name_en << phone << m_postcode
                 << website << m_parent_index << m_longitude << m_latitude;
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
    cmd.binder() << m_my_index << m_class + "_" + m_type;
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

void HierarchyItem::print_branch(unsigned int offset) const
{
  std::cout << std::string(offset, ' ') << "- " << m_id << " ";
  if (!m_housenumber.empty())
    std::cout << "house " << m_housenumber << " ";
  for (const auto &i : m_name)
    std::cout << i.first << ": " << i.second << " ";
  std::cout << "(" << m_my_index << " " << m_last_child_index - m_my_index << ": " << m_parent_id
            << ", " << m_country << ")\n";
  if (m_children.size())
    std::cout << std::string(offset + 2, ' ') << "|\n";
  for (auto c : m_children)
    c->print_branch(offset + 3);
}
