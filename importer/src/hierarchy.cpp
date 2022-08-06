#include "hierarchy.h"

#include <iostream>
#include <stdexcept>

Hierarchy::Hierarchy() {}

Hierarchy::~Hierarchy() {}

void Hierarchy::add_item(std::shared_ptr<HierarchyItem> &item)
{
  hindex id        = item->id();
  hindex parent_id = item->parent_id();
  if (m_items.count(id))
    throw std::runtime_error("Trying to insert item that has been inserted earlier");

  m_items[id] = item;
  auto p      = m_items.find(parent_id);
  if (p != m_items.end())
    p->second->add_child(item);
  else
    {
      auto root_leaf = m_root.find(parent_id);
      if (root_leaf == m_root.end())
        m_root[parent_id] = std::set<std::shared_ptr<HierarchyItem> >({ item });
      else
        root_leaf->second.insert(item);
    }

  // check if added item was a parent for someone and adjust root accordingly
  auto root_leaf = m_root.find(id);
  if (root_leaf != m_root.end())
    {
      for (auto root_iter : root_leaf->second)
        {
          if (root_iter->parent_id() != id)
            throw std::runtime_error("Mismatch between expected parent and root location");
          item->add_child(root_iter);
        }
      m_root.erase(root_leaf);
    }
}

void Hierarchy::add_linked_item(std::shared_ptr<HierarchyItem> &item)
{
  hindex linked = item->linked_id();
  auto   tolink = m_items.find(linked);
  if (tolink == m_items.end())
    {
      std::cout << "Failed to find linked object " << linked << " required by " << item->id()
                << ". Skipping linkage.";
      return;
    }

  tolink->second->add_linked(item);
}

void Hierarchy::cleanup()
{
  for (auto root_iter = m_root.begin(); root_iter != m_root.end(); ++root_iter)
    {
      std::set<std::shared_ptr<HierarchyItem> > keep;
      for (auto item : root_iter->second)
        {
          item->cleanup_children();
          if (item->keep())
            keep.insert(item);
          else
            keep.insert(item->children().begin(), item->children().end());
        }
      root_iter->second = keep;

      // ensure that the parent is set correctly
      for (auto item : root_iter->second)
        item->set_parent(root_iter->first, true);
    }
}

void Hierarchy::set_country(const std::string &country, hindex id)
{
  if (!m_items.count(id))
    {
      std::cout << "Missing country in the database: " << country << " / " << id << "\n";
      for (auto item : root_items())
        if (item->country() == country)
          item->print_branch(0);
    }

  auto parent = m_items[id];
  for (auto root_iter = m_root.begin(); root_iter != m_root.end(); ++root_iter)
    {
      std::set<std::shared_ptr<HierarchyItem> > remove;
      for (auto item : root_iter->second)
        if (item->country() == country && item->id() != id)
          {
            parent->add_child(item);
            remove.insert(item);
          }
      std::cout << "Relocated to country: " << country << " - " << remove.size() << "\n";
      for (auto item : remove)
        root_iter->second.erase(item);
    }
}

void Hierarchy::finalize()
{
  m_root_finalized = root_items();
  sqlid index      = 1;
  for (auto item : m_root_finalized)
    {
      index = item->index(index, 0);
      item->set_parent(0);
    }

  std::cout << "Hierarchy: active items: " << index
            << " / cleared items: " << m_items.size() - index << "\n";
}

void Hierarchy::write(sqlite3pp::database &db) const
{
  for (auto item : m_root_finalized)
    item->write(db);
}

std::deque<std::shared_ptr<HierarchyItem> > Hierarchy::root_items() const
{
  std::deque<std::shared_ptr<HierarchyItem> > q;
  for (auto root_iter = m_root.begin(); root_iter != m_root.end(); ++root_iter)
    for (auto item : root_iter->second)
      q.push_back(item);
  return q;
}

size_t Hierarchy::get_root_count() const
{
  size_t count{ 0 };
  for (auto i : m_root)
    count += i.second.size();
  return count;
}

hindex Hierarchy::get_next_nonzero_root_parent() const
{
  for (auto root_iter = m_root.begin(); root_iter != m_root.end(); ++root_iter)
    if (root_iter->first)
      return root_iter->first;
  return 0;
}

std::set<std::string> Hierarchy::get_root_countries() const
{
  std::set<std::string> missing;
  for (auto item : root_items())
    missing.insert(item->country());
  return missing;
}

bool Hierarchy::has_item(hindex id) const
{
  return m_items.count(id);
}

void Hierarchy::print(bool full) const
{
  std::set<hindex> root_ids;
  for (auto item : root_items())
    {
      if (full)
        item->print_branch(0);
      root_ids.insert(item->id());
    }

  std::cout << (full ? "\n\n" : "") << "Root items:\n";
  for (auto id : root_ids)
    std::cout << id << " ";
  std::cout << "\n";
  std::cout << "Root items count: " << get_root_count() << "\n";

  std::cout << "Countries: ";
  for (auto c : get_root_countries())
    std::cout << c << " ";
  std::cout << "\n";
}
