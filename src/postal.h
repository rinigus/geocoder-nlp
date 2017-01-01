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

    bool parse(const std::string &input, std::vector< ParseResult > &parsed);

    bool parse(const std::string &input, std::vector< std::vector<std::string> > &parsed);

    static void result2hierarchy(const std::vector< ParseResult > &pr, std::vector< std::vector<std::string> > &h);

protected:
    bool init();
    void drop();

protected:
    bool m_initialized = false;
    bool m_initialize_for_every_call = false;
};

}

#endif // POSTAL_H
