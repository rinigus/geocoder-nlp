#ifndef HIERARCHY_H
#define HIERARCHY_H

#pragma once

#include "hierarchyitem.h"

#include <deque>
#include <map>
#include <memory>
#include <set>
#include <sqlite3pp.h>
#include <string>

class Hierarchy
{
public:
  Hierarchy();
  ~Hierarchy();

  void add_item(std::shared_ptr<HierarchyItem> &item);
  bool add_linked_item(std::shared_ptr<HierarchyItem> &item);
  void set_country(const std::string &country, hindex id);
  void cleanup();
  void finalize();
  void check_indexing();
  void write(sqlite3pp::database &db) const;

  size_t get_missing_count() const { return m_root.size(); }
  size_t get_root_count() const;
  bool   has_item(hindex id) const;

  hindex                get_next_nonzero_root_parent() const;
  std::set<std::string> get_root_countries() const;

  void print(bool full = true) const;
  void print_root_with_parent_id(hindex parent_id) const;

private:
  std::deque<std::shared_ptr<HierarchyItem> > root_items() const;

private:
  std::map<hindex, std::shared_ptr<HierarchyItem> >            m_items;
  std::map<hindex, std::set<std::shared_ptr<HierarchyItem> > > m_root;
  std::deque<std::shared_ptr<HierarchyItem> >                  m_root_finalized;
};

#endif
