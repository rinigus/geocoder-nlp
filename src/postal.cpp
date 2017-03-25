#include "postal.h"

#include <libpostal/libpostal.h>

#include <iostream>
#include <sstream>
#include <algorithm>
#include <functional>
#include <cctype>
#include <locale>

using namespace GeoNLP;

#define ADDRESS_PARSER_LABEL_HOUSE "house"
#define ADDRESS_PARSER_LABEL_HOUSE_NUMBER "house_number"
#define ADDRESS_PARSER_LABEL_ROAD "road"
#define ADDRESS_PARSER_LABEL_SUBURB "suburb"
#define ADDRESS_PARSER_LABEL_CITY_DISTRICT "city_district"
#define ADDRESS_PARSER_LABEL_CITY "city"
#define ADDRESS_PARSER_LABEL_STATE_DISTRICT  "state_district"
#define ADDRESS_PARSER_LABEL_STATE  "state"
#define ADDRESS_PARSER_LABEL_POSTAL_CODE  "postal_code"
#define ADDRESS_PARSER_LABEL_COUNTRY  "country"

//////////////////////////////////////////////////////////////////////
/// Helper string functions
///

static void str2vecchar(const std::string &s, std::vector<char> &v)
{
  v.resize(s.length() + 1);
  std::copy(s.c_str(), s.c_str() + s.length() + 1, v.begin());
}

// trim from start
static inline std::string &ltrim(std::string &s) {
  s.erase(s.begin(), std::find_if(s.begin(), s.end(),
				  std::not1(std::ptr_fun<int, int>(std::isspace))));
  return s;
}

// trim from end
static inline std::string &rtrim(std::string &s) {
  s.erase(std::find_if(s.rbegin(), s.rend(),
		       std::not1(std::ptr_fun<int, int>(std::isspace))).base(), s.end());
  return s;
}

// trim from both ends
static inline std::string &trim(std::string &s) {
  return ltrim(rtrim(s));
}

static void split_tokens(const std::string &s, char delim,
                         std::vector<std::string> &elems)
{
  std::stringstream ss;
  ss.str(s);
  std::string item;
  while (std::getline(ss, item, delim))
    if (!item.empty())
      elems.push_back(trim(item));
}

static std::string primitive_key(size_t ind)
{
  std::ostringstream ss;
  ss << "h-" << ind;
  return ss.str();
}

// //////////////////////////////////////////////////////
// /// Helper classes and functions for cartesian product

// // cartesian product from http://stackoverflow.com/questions/5279051/how-can-i-create-cartesian-product-of-vector-of-vectors
// typedef std::vector<std::string> Vi;
// typedef std::vector<Vi> Vvi;

// struct Digits {
//   Vi::const_iterator begin;
//   Vi::const_iterator end;
//   Vi::const_iterator me;
// };
// typedef std::vector<Digits> Vd;

// static void cart_product(
// 			 Vvi& out,
// 			 Vvi& in)

// {
//   Vd vd;

//   // Start all of the iterators at the beginning.
//   for(Vvi::const_iterator it = in.begin();
//       it != in.end();
//       ++it) {
//     Digits d = {(*it).begin(), (*it).end(), (*it).begin()};
//     vd.push_back(d);
//   }


//   while(1) {

//     // Construct your first product vector by pulling
//     // out the element of each vector via the iterator.
//     Vi result;
//     for(Vd::const_iterator it = vd.begin();
// 	it != vd.end();
// 	it++) {
//       result.push_back(*(it->me));
//     }
//     out.push_back(result);

//     // Increment the rightmost one, and repeat.

//     // When you reach the end, reset that one to the beginning and
//     // increment the next-to-last one. You can get the "next-to-last"
//     // iterator by pulling it out of the neighboring element in your
//     // vector of iterators.
//     for(Vd::iterator it = vd.begin(); ; ) {
//       // okay, I started at the left instead. sue me
//       ++(it->me);
//       if(it->me == it->end) {
// 	if(it+1 == vd.end()) {
// 	  // I'm the last digit, and I'm about to roll
// 	  return;
// 	} else {
// 	  // cascade
// 	  it->me = it->begin;
// 	  ++it;
// 	}
//       } else {
// 	// normal
// 	break;
//       }
//     }
//   }
// }

///////////////////////////////////////////////////////////////////
/// Postal class

Postal::Postal()
{

}

Postal::~Postal()
{
  drop();
}

void Postal::set_postal_datadir(const std::string &global, const std::string &country)
{
  if (global.length() < 1) m_postal_datadir_global.clear();
  else str2vecchar(global, m_postal_datadir_global);

  if (country.length() < 1) m_postal_datadir_country.clear();
  else str2vecchar(country, m_postal_datadir_country);

  drop(); // force reinitialization
}

void Postal::add_language(const std::string &lang)
{
  std::vector<char> l;
  str2vecchar(lang, l);
  m_postal_languages.push_back(l);
  drop();
}

bool Postal::init()
{
  if (m_initialized) return true;

  if ( (m_postal_datadir_global.empty() && !libpostal_setup() ) ||
       (!m_postal_datadir_global.empty() && !libpostal_setup_datadir(m_postal_datadir_global.data())) )
    return false;

  if ( (m_postal_datadir_country.empty() && !libpostal_setup_parser() ) ||
       (!m_postal_datadir_country.empty() && !libpostal_setup_parser_datadir(m_postal_datadir_country.data())) )
    return false;

  if ( m_postal_languages.empty() )
    {
      if ( (m_postal_datadir_global.empty() && !libpostal_setup_language_classifier() ) ||
	   (!m_postal_datadir_global.empty() && !libpostal_setup_language_classifier_datadir(m_postal_datadir_global.data())) )
	return false;
    }

  m_initialized = true;
  return true;
}

void Postal::drop()
{
  if (!m_initialized) return;
  libpostal_teardown();
  libpostal_teardown_language_classifier();
  libpostal_teardown_parser();
  m_initialized = false;
}

/// Normalize first, expand next. Seems to miss few expansions in current implementation
/// Have to wait for new libpostal version to switch to this approach

//bool Postal::parse(const std::string &input, std::vector<Postal::ParseResult> &result)
//{
//    if (!init()) return false;

//    // convert string into vector of chars (libpostal uses char* as an argument, not const char*)
//    std::vector<char> charbuff;
//    charbuff.resize(input.length() + 1);
//    std::copy(input.c_str(), input.c_str() + input.length() + 1, charbuff.begin());

//    size_t num_expansions;
//    normalize_options_t options_norm = get_libpostal_default_options();
//    address_parser_options_t options_parse = get_libpostal_address_parser_default_options();

//    char **expansions = expand_address(charbuff.data(), options_norm, &num_expansions);
//    if (m_initialize_for_every_call)
//        libpostal_teardown_language_classifier();

//    for (size_t i = 0; i < num_expansions; i++)
//    {
//        address_parser_response_t *parsed = parse_address(expansions[i], options_parse);

//        ParseResult r;
//        for (size_t j = 0; j < parsed->num_components; j++)
//            r[ parsed->labels[j] ] = parsed->components[j];

//        result.push_back(r);

//        address_parser_response_destroy(parsed);
//    }

//    expansion_array_destroy(expansions, num_expansions);

//    if (m_initialize_for_every_call) drop();

//    return true;
//}

//////////////////////////////////////////////////////////////
/// expand first then normalize

bool Postal::parse(const std::string &input, std::vector<Postal::ParseResult> &result,
                   Postal::ParseResult &nonormalization)
{
  if (m_use_postal && !init())
    return false;

  // libpostal parsing
  if (m_use_postal)
    {
      // convert string into vector of chars (libpostal uses char* as an argument, not const char*)
      std::vector<char> charbuff;
      charbuff.resize(input.length() + 1);
      std::copy(input.c_str(), input.c_str() + input.length() + 1, charbuff.begin());

      address_parser_options_t options_parse = get_libpostal_address_parser_default_options();

      // parse the address
      address_parser_response_t *parsed = parse_address(charbuff.data(), options_parse);
      nonormalization.clear();
      for (size_t j = 0; j < parsed->num_components; j++)
        {
          std::vector<std::string> pc; pc.push_back(parsed->components[j]);
          nonormalization[ parsed->labels[j] ] = pc;
        }
      address_parser_response_destroy(parsed);

      expand(nonormalization, result);
    }

  // primitive parsing
  if (m_use_primitive)
    {
      std::vector<std::string> hier;
      split_tokens(input, ',', hier);

      if (!hier.empty())
        {
	  ParseResult prim;
	  for (size_t j = 0; j < hier.size(); j++)
            {
              std::vector<std::string> pc; pc.push_back(hier[hier.size()-j-1]);
              prim[ primitive_key(j) ] = pc;
            }

	  expand(prim, result);
        }
    }

  if (m_initialize_for_every_call) drop();

  return true;
}

void Postal::expand(const Postal::ParseResult &input, std::vector<Postal::ParseResult> &result)
{
  if (!m_use_postal || !init())
    {
      result.push_back(input);
      return;
    }

  size_t num_expansions;
  normalize_options_t options_norm = get_libpostal_default_options();

  std::vector<char*> lang;
  for (std::vector<char> &l: m_postal_languages)
    lang.push_back(l.data());

  options_norm.languages = lang.data();
  options_norm.num_languages = lang.size();

  std::vector<char> charbuff;

  std::vector< std::vector< std::string > > address_expansions;
  std::vector< std::string > address_keys;
  for (const auto i: input)
    {
      // in practice, its only one element at ParseResult at this stage
      for (const std::string tonorm: i.second)
        {
          charbuff.resize(tonorm.length() + 1);
          std::copy(tonorm.c_str(), tonorm.c_str() + tonorm.length() + 1, charbuff.begin());

          char **expansions = expand_address(charbuff.data(), options_norm, &num_expansions);
          std::vector< std::string > norm;
          for (size_t j = 0; j < num_expansions; j++)
            norm.push_back(expansions[j]);

          expansion_array_destroy(expansions, num_expansions);

          address_expansions.push_back(norm);
          address_keys.push_back(i.first);
        }
    }

  ParseResult r;
  for (size_t i=0; i < address_keys.size(); ++i)
    r[ address_keys[i] ] = address_expansions[i];
  result.push_back(r);
}

void Postal::result2hierarchy(const std::vector<ParseResult> &p, std::vector<Hierarchy> &h)
{
  h.clear();
  for (const ParseResult &r: p)
    {
      Hierarchy h_result;
#define ADDIFHAS(k) { ParseResult::const_iterator it = r.find(k); if (it != r.end()) h_result.push_back(it->second); }

      ADDIFHAS(ADDRESS_PARSER_LABEL_COUNTRY);
      ADDIFHAS(ADDRESS_PARSER_LABEL_STATE);
      ADDIFHAS(ADDRESS_PARSER_LABEL_STATE_DISTRICT);
      ADDIFHAS(ADDRESS_PARSER_LABEL_CITY);
      ADDIFHAS(ADDRESS_PARSER_LABEL_CITY_DISTRICT);
      ADDIFHAS(ADDRESS_PARSER_LABEL_SUBURB);
      ADDIFHAS(ADDRESS_PARSER_LABEL_ROAD);
      ADDIFHAS(ADDRESS_PARSER_LABEL_HOUSE_NUMBER);
      ADDIFHAS(ADDRESS_PARSER_LABEL_HOUSE);

      // test if its primitive expansion result
      if (h_result.empty())
        {
	  bool done = false;
	  for (size_t i=0; !done; ++i)
            {
	      ParseResult::const_iterator it = r.find(primitive_key(i));
	      if (it == r.end())
		done = true;
	      else
		h_result.push_back(it->second);
            }
        }

      h.push_back(h_result);
    }
}
