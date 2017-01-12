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

    typedef std::map< std::string, std::string > ParseResult;

    bool parse(const std::string &input, std::vector< ParseResult > &parsed, Postal::ParseResult &nonormalization);

    static void result2hierarchy(const std::vector< ParseResult > &pr, std::vector< std::vector<std::string> > &h);

    bool get_initialize_every_call() const { return m_initialize_for_every_call; }
    void set_initialize_every_call(bool v) { m_initialize_for_every_call = v; }

    bool get_use_postal() const { return m_use_postal; }
    void set_use_postal(bool v) { m_use_postal = v; if (!v) drop(); }

    bool get_use_primitive() const { return m_use_primitive; }
    void set_use_primitive(bool v) { m_use_primitive = v; }

protected:
    bool init();
    void drop();

    void expand(const Postal::ParseResult &input, std::vector< Postal::ParseResult > &result);

protected:
    bool m_initialized = false;
    bool m_initialize_for_every_call = false;
    bool m_use_postal = true;
    bool m_use_primitive = true;
};

}

#endif // POSTAL_H
