#ifndef HIERARCHYITEM_H
#define HIERARCHYITEM_H

#pragma once

#include <deque>
#include <memory>
#include <pqxx/pqxx>
#include <string>

typedef unsigned long hindex;
typedef long long int sqlid; /// type used by IDs in SQLite

class HierarchyItem
{
public:
  HierarchyItem(const pqxx::row &row);
  ~HierarchyItem(){};

  hindex             id() const { return m_id; }
  hindex             linked_id() const { return m_linked_id; }
  hindex             parent_id() const { return m_parent_id; }
  const std::string &country() const { return m_country; }

  void  add_child(std::shared_ptr<HierarchyItem> child);
  void  set_parent(hindex);
  sqlid index(sqlid idx);

  void print_branch(unsigned int offset);

private:
  hindex m_id;
  hindex m_linked_id{ 0 };
  hindex m_parent_id;
  sqlid  m_my_index;
  sqlid  m_last_child_index;

  std::string m_class;
  std::string m_type;
  float       m_latitude;
  float       m_longitude;
  std::string m_country;
  std::string m_postcode;
  std::string m_housenumber;

  std::map<std::string, std::string> m_name;
  std::map<std::string, std::string> m_extra;

  std::deque<std::shared_ptr<HierarchyItem> > m_children;
};

#endif