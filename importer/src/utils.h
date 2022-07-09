#ifndef UTILS_H
#define UTILS_H

#include <map>
#include <string>

std::string get_with_def(const std::map<std::string, std::string> &m, const std::string &key,
                         const std::string &defval = std::string())
{
  auto it = m.find(key);
  if (it == m.end())
    return defval;
  return it->second;
}

#endif
