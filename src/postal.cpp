#include "postal.h"

#include <libpostal/libpostal.h>

#include <iostream>

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

Postal::Postal()
{

}

Postal::~Postal()
{
    drop();
}

bool Postal::init()
{
    if (m_initialized) return true;

    if (!libpostal_setup() || !libpostal_setup_language_classifier() || !libpostal_setup_parser())
    {
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

// cartesian product from http://stackoverflow.com/questions/5279051/how-can-i-create-cartesian-product-of-vector-of-vectors
typedef std::vector<std::string> Vi;
typedef std::vector<Vi> Vvi;

// Seems like you'd want a vector of iterators
// which iterate over your individual vector<int>s.
struct Digits {
    Vi::const_iterator begin;
    Vi::const_iterator end;
    Vi::const_iterator me;
};
typedef std::vector<Digits> Vd;

static void cart_product(
        Vvi& out,
        Vvi& in)

{
    Vd vd;

    // Start all of the iterators at the beginning.
    for(Vvi::const_iterator it = in.begin();
        it != in.end();
        ++it) {
        Digits d = {(*it).begin(), (*it).end(), (*it).begin()};
        vd.push_back(d);
    }


    while(1) {

        // Construct your first product vector by pulling
        // out the element of each vector via the iterator.
        Vi result;
        for(Vd::const_iterator it = vd.begin();
            it != vd.end();
            it++) {
            result.push_back(*(it->me));
        }
        out.push_back(result);

        // Increment the rightmost one, and repeat.

        // When you reach the end, reset that one to the beginning and
        // increment the next-to-last one. You can get the "next-to-last"
        // iterator by pulling it out of the neighboring element in your
        // vector of iterators.
        for(Vd::iterator it = vd.begin(); ; ) {
            // okay, I started at the left instead. sue me
            ++(it->me);
            if(it->me == it->end) {
                if(it+1 == vd.end()) {
                    // I'm the last digit, and I'm about to roll
                    return;
                } else {
                    // cascade
                    it->me = it->begin;
                    ++it;
                }
            } else {
                // normal
                break;
            }
        }
    }
}

bool Postal::parse(const std::string &input, std::vector<Postal::ParseResult> &result, Postal::ParseResult &nonormalization)
{
    if (!init()) return false;

    // convert string into vector of chars (libpostal uses char* as an argument, not const char*)
    std::vector<char> charbuff;
    charbuff.resize(input.length() + 1);
    std::copy(input.c_str(), input.c_str() + input.length() + 1, charbuff.begin());

    size_t num_expansions;
    normalize_options_t options_norm = get_libpostal_default_options();
    address_parser_options_t options_parse = get_libpostal_address_parser_default_options();

    // parse the address
    address_parser_response_t *parsed = parse_address(charbuff.data(), options_parse);
    nonormalization.clear();
    for (size_t j = 0; j < parsed->num_components; j++)
        nonormalization[ parsed->labels[j] ] = parsed->components[j];
    address_parser_response_destroy(parsed);

    std::vector< std::vector< std::string > > address_expansions;
    std::vector< std::string > address_keys;
    for (const auto i: nonormalization)
    {
        const std::string &tonorm = i.second;
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

    std::vector< std::vector< std::string > > address_expansions_combos;
    cart_product(address_expansions_combos, address_expansions);

    for (const auto &ae: address_expansions_combos)
    {
        ParseResult r;
        for (size_t i=0; i < address_keys.size(); ++i)
            r[ address_keys[i] ] = ae[i];
        result.push_back(r);
    }

    if (m_initialize_for_every_call) drop();

    return true;
}

void Postal::result2hierarchy(const std::vector<ParseResult> &p, std::vector<std::vector<std::string> > &h)
{
    h.clear();
    for (const ParseResult &r: p)
    {
        std::vector<std::string> h_result;
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

        h.push_back(h_result);
    }
}
