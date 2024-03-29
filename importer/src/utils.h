#ifndef UTILS_H
#define UTILS_H

#include <map>
#include <string>

std::string get_with_def(const std::map<std::string, std::string> &m, const std::string &key,
                         const std::string &defval = std::string());

std::string geocoder_type(const std::string &t_class, const std::string &t_value);

std::map<std::string, std::string> parse_to_map(const std::string &js);

bool same_starts_with(const std::string &start, const std::string &s1, const std::string &s2);

#endif
