#include "hierarchyitem.h"

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

sqlid HierarchyItem::index(sqlid idx)
{
  m_my_index = idx;
  ++idx;
  for (auto item : m_children)
    idx = item->index(idx);
  m_last_child_index = idx - 1;
  return idx;
}

void HierarchyItem::print_branch(unsigned int offset)
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
