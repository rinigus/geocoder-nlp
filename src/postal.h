#ifndef POSTAL_H
#define POSTAL_H

#include <string>
#include <map>
#include <vector>

namespace GeoNLP {

  class Postal
  {
  public:
    Postal();
    ~Postal();

    typedef std::map< std::string, std::vector<std::string> > ParseResult;
    typedef std::vector< std::vector<std::string> > Hierarchy;

    bool parse(const std::string &input, std::vector<Postal::ParseResult> &parsed, ParseResult &nonormalization);

    static void result2hierarchy(const std::vector<ParseResult> &p, std::vector<Hierarchy> &h);

    bool get_initialize_every_call() const { return m_initialize_for_every_call; }
    void set_initialize_every_call(bool v) { m_initialize_for_every_call = v; }

    bool get_use_postal() const { return m_use_postal; }
    void set_use_postal(bool v) { m_use_postal = v; if (!v) drop(); }

    bool get_use_primitive() const { return m_use_primitive; }
    void set_use_primitive(bool v) { m_use_primitive = v; }

    void set_postal_datadir(const std::string &global, const std::string &country);

    void clear_languages() { m_postal_languages.clear(); drop(); }
    void add_language(const std::string &lang);

  protected:
    bool init();
    void drop();

    void expand(const Postal::ParseResult &input, std::vector< Postal::ParseResult > &result);

  protected:
    bool m_initialized = false;
    bool m_initialize_for_every_call = false;
    bool m_use_postal = true;
    bool m_use_primitive = true;

    std::vector<char> m_postal_datadir_global;
    std::vector<char> m_postal_datadir_country;
    std::vector< std::vector<char> > m_postal_languages;
  };

}

#endif // POSTAL_H
