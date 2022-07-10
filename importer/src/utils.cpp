#include "utils.h"

#include <nlohmann/json.hpp>

using json = nlohmann::json;

std::string get_with_def(const std::map<std::string, std::string> &m, const std::string &key,
                         const std::string &defval)
{
  auto it = m.find(key);
  if (it == m.end())
    return defval;
  return it->second;
}

std::map<std::string, std::string> parse_to_map(const std::string &js)
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

bool same_starts_with(const std::string &start, const std::string &s1, const std::string &s2)
{
  return s1.rfind(start, 0) == 0 && s2.rfind(start, 0) == 0;
}

std::string geocoder_type(const std::string &t_class, const std::string &t_value)
{
  if (t_value == "yes" || t_value.empty())
    return t_class;
  return t_class + "_" + t_value;
}