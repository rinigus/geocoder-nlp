#ifndef HIERARCHYITEM_H
#define HIERARCHYITEM_H

#include "config.h"

#include <deque>
#include <memory>
#include <pqxx/pqxx>
#include <set>
#include <sqlite3pp.h>
#include <string>

class HierarchyItem
{
public:
  HierarchyItem(const pqxx::row &row);
  ~HierarchyItem(){};

  hindex             id() const { return m_id; }
  hindex             linked_id() const { return m_linked_id; }
  hindex             parent_id() const { return m_parent_id; }
  const std::string &country() const { return m_country; }
  bool               keep(bool verbose = true) const;
  bool               indexed() const { return m_my_index > 0; }

  void drop();
  bool dropped() const { return m_dropped; }

  const std::deque<std::shared_ptr<HierarchyItem> > &children() { return m_children; }

  void  add_child(std::shared_ptr<HierarchyItem> child);
  void  add_linked(std::shared_ptr<HierarchyItem> linked);
  void  set_parent(hindex parent, bool force = false);
  void  cleanup_children(bool duplicate_only = false);
  sqlid index(sqlid idx, sqlid parent);
  void  write(sqlite3pp::database &db) const;

  void print_item(unsigned int offset) const;
  void print_branch(unsigned int offset) const;

public:
  static void load_priority_list(const std::string &fname);
  static void load_skip_list(const std::string &fname);

  static std::set<std::string> get_priority_list() { return s_priority_types; }

protected:
  void        set_names();
  bool        is_duplicate(std::shared_ptr<HierarchyItem> item) const;
  std::string key() const;

private:
  hindex      m_id;
  hindex      m_linked_id{ 0 };
  hindex      m_parent_id;
  sqlid       m_my_index{ 0 };
  sqlid       m_parent_index{ 0 };
  sqlid       m_last_child_index{ 0 };
  bool        m_dropped{ false };
  std::string m_key;

  std::string m_type;
  float       m_latitude;
  float       m_longitude;
  uint64_t    m_osm_id;
  std::string m_country;
  std::string m_postcode;
  std::string m_housenumber;
  std::string m_name;
  std::string m_name_extra;
  int         m_search_rank;

  std::map<std::string, std::string> m_data_name;
  std::map<std::string, std::string> m_data_extra;

  std::deque<std::shared_ptr<HierarchyItem> > m_children;

  static std::set<std::string> s_priority_types;
  static std::set<std::string> s_skip_types;
};

#endif
