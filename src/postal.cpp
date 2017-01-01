#include "postal.h"

#include <libpostal/libpostal.h>

using namespace GeoNLP;

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

bool Postal::parse(const std::string &input, std::vector<Postal::ParseResult> &result)
{
    if (!init()) return false;

    // convert string into vector of chars (libpostal uses char* as an argument, not const char*)
    std::vector<char> charbuff;
    charbuff.resize(input.length() + 1);
    std::copy(input.c_str(), input.c_str() + input.length() + 1, charbuff.begin());

    size_t num_expansions;
    normalize_options_t options_norm = get_libpostal_default_options();
    address_parser_options_t options_parse = get_libpostal_address_parser_default_options();

    char **expansions = expand_address(charbuff.data(), options_norm, &num_expansions);
    if (m_initialize_for_every_call)
        libpostal_teardown_language_classifier();

    for (size_t i = 0; i < num_expansions; i++)
    {
        address_parser_response_t *parsed = parse_address(expansions[i], options_parse);

        ParseResult r;
        for (size_t j = 0; j < parsed->num_components; j++)
            r[ parsed->labels[j] ] = parsed->components[j];

        result.push_back(r);

        address_parser_response_destroy(parsed);
    }

    expansion_array_destroy(expansions, num_expansions);

    if (m_initialize_for_every_call) drop();

    return true;
}

bool Postal::parse(const std::string &input, std::vector<std::vector<std::string> > &parsed)
{
    std::vector<ParseResult> p;
    if (!parse(input, p)) return false;

    result2hierarchy(p, parsed);

    return true;
}

void Postal::result2hierarchy(const std::vector<ParseResult> &p, std::vector<std::vector<std::string> > &h)
{
    h.clear();
    for (const ParseResult &r: p)
    {
        std::vector<std::string> h_result;
#define ADDIFHAS(k) { ParseResult::const_iterator it = r.find(k); if (it != r.end()) h_result.push_back(it->second); }

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
