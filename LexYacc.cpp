#include <filesystem>
#include <fstream>
#include <iostream>
#include <sstream>
#include <string>
#include <unordered_map>

namespace stdfs = std::filesystem;

void find_and_replace(std::string& source, std::string const& find, std::string const& replace)
{
    for (std::string::size_type i = 0; (i = source.find(find, i)) != std::string::npos;)
    {
        source.replace(i, find.length(), replace);
        i += replace.length();
    }
}

std::string tryreadfile(stdfs::path fname)
{
    if (!stdfs::exists(fname))
    {
        return "";
    }

    std::ifstream ly(fname);
    if (!ly.is_open())
    {
        throw std::invalid_argument("cannot open file : " + fname.string());
    }

    return std::string((std::istreambuf_iterator<char>(ly)), (std::istreambuf_iterator<char>()));
}

void generate(stdfs::path fname, std::string tmpl, std::unordered_map<std::string, std::string> const& params)
{
    for (auto const& [k, v] : params)
    {
        find_and_replace(tmpl, "zz" + std::string(k) + "zz", v);
    }
    if (tmpl == tryreadfile(fname)) return;
    std::ofstream of(fname);
    of.write(tmpl.data(), tmpl.size());
    of.close();
}

int main(int argc, char* argv[])
try
{
    int         req = 0;
    std::string outdir(".");
    std::string prefix;
    std::string lyfile;
    for (int i = 0; i < argc; i++)
    {
        if (argv[i][0] == '-' && argv[i][1] == '-' && std::string(&argv[i][2]) == "outdir")
        {
            if (i == (argc - 1)) throw std::invalid_argument("outdir");
            outdir = argv[i + 1];
            i++;
            continue;
        }
        if (argv[i][0] == '-' && argv[i][1] == '-' && std::string(&argv[i][2]) == "prefix")
        {
            if (i == (argc - 1)) throw std::invalid_argument("prefix");
            prefix = argv[i + 1];
            i++;
            continue;
        }
        if (req == 0)
        {
            lyfile = argv[i];
        }
        else
        {
            throw std::invalid_argument("unexpected");
        }
    }

    if (prefix.empty())
    {
        prefix = stdfs::path(lyfile).stem().string();
    }

    stdfs::path   path(lyfile);
    std::ifstream ly(lyfile);
    if (!ly.is_open())
    {
        throw std::invalid_argument("cannot open file : " + lyfile);
    }
    std::string content((std::istreambuf_iterator<char>(ly)), (std::istreambuf_iterator<char>()));
    auto        findstart = [](std::string const& str, std::string const& tag) { return str.begin() + str.find(tag) + tag.length(); };
    auto        findend   = [](std::string const& str, std::string const& tag) { return str.begin() + str.find(tag); };

    auto        flex = std::string(findstart(content, "LEXYACC:LEX:START"), findend(content, "LEXYACC:LEX:END"));
    auto        yacc = std::string(findstart(content, "LEXYACC:YACC:START"), findend(content, "LEXYACC:YACC:END"));
    std::string nmsp;
    auto        it   = findstart(content, "LEXYACC:NAMESPACE ");
    auto        temp = std::string(it, content.cend());
    std::stringstream(temp) >> nmsp;

    std::string template_lyh = R"(
#include "zzPREFIXzz.h"

#include <filesystem>
#include <fstream>
#include <sstream>

namespace zzNAMESPACEzz
{
    void Load(Context& context, std::istream& strm);

    inline void LoadString(Context& context, const char *str)
    {
        std::stringstream sstrm(str);
        return Load(context, sstrm);
    }

    inline void LoadString(Context& context, std::string const& str)
    {
        std::stringstream sstrm(str);
        return Load(context, sstrm);
    }

    inline void LoadFile(Context& context, std::filesystem::path const& path)
    {
        std::ifstream file(path);
        if (!file.is_open()) throw std::invalid_argument("Cannot open file");
        return Load(context, file);
    }
}
)";

    std::string template_impl_hh = R"(
#include <sstream>
class zzPREFIXzzFlexLexer;
namespace zzNAMESPACEzz::impl
{
class Scanner : public zzPREFIXzzFlexLexer
{
public:
    Scanner(std::istream* in) : zzPREFIXzzFlexLexer(in){};
    // get rid of override virtual function warning
#undef yylex
    virtual int yylex(BisonParser::semantic_type* const lval, BisonParser::location_type* location);

private:
    std::stringstream sstr;    // TODO  Move this outside
};

}
)";

    std::string template_y = R"(
%debug 
%defines 
%define api.namespace {zzNAMESPACEzz::impl}
%define api.parser.class {BisonParser}

%code requires{ // Included in the header
#include "zzPREFIXzz.h"         // Written by user
#include "zzPREFIXzz.ly.h"      // Public access apis

namespace zzNAMESPACEzz::impl { class Scanner; }

}

%parse-param { zzNAMESPACEzz::impl::Scanner &scanner }
%parse-param { zzNAMESPACEzz::Context &context }
%locations

%code
{
// Included in CPP
#include <cstdlib>
#include <fstream>
#include <iostream>
#include <sstream>

#define yyFlexLexer zzPREFIXzzFlexLexer
#undef yylex
#include <FlexLexer.h>

#include "zzPREFIXzz.ly.impl.h"  // Generated us
#define yylex scanner.yylex

}

%define api.value.type variant
%define parse.assert

zzYACCzz

void zzNAMESPACEzz::impl::BisonParser::error(const location_type&l, const std::string& message)
{
    std::cerr << "Error: " << message << " at " << l << "\n";
}

namespace zzNAMESPACEzz
{
void Load(Context& ctx, std::istream& strm)
{
    impl::Scanner scanner(&strm);
    impl::BisonParser p(scanner, ctx);
    p.set_debug_level(ctx.Debug());
    if (p.parse() != 0) throw std::logic_error("Unexpected Error");
}
}
)";

    std::string                                  template_l = R"(
%{
#include "zzPREFIXzz.yacc.h"         // Generated by bison
#include "zzPREFIXzz.ly.impl.h"      // Generated us

#undef  YY_DECL
#define YY_DECL int zzNAMESPACEzz::impl::Scanner::yylex(zzNAMESPACEzz::impl::BisonParser::semantic_type * const lval, zzNAMESPACEzz::impl::BisonParser::location_type *location )

using token = zzNAMESPACEzz::impl::BisonParser::token;

/* define yyterminate as this instead of NULL */
#define yyterminate() return( token::END )

/* msvc2010 requires that we exclude this header file. */
#define YY_NO_UNISTD_H

/* update location on matching */
#define YY_USER_ACTION location->begin.line = yylineno; location->begin.column = yyleng;

%}


%option debug
%option nodefault
%option noyywrap
%option noyywrap
%option nounput
%option yylineno
%option yyclass="zzNAMESPACEzz::impl::Scanner"
%option c++
zzFLEXzz

)";
    std::unordered_map<std::string, std::string> params     = {{"NAMESPACE", nmsp}, {"PREFIX", prefix}, {"YACC", yacc}, {"FLEX", flex}};
    std::unordered_map<std::string, std::string> files
        = {{"y", template_y}, {"l", template_l}, {"ly.impl.h", template_impl_hh}, {"ly.h", template_lyh}};

    for (auto const& [ext, tmpl] : files)
    {
        generate(stdfs::path(outdir) / stdfs::path(prefix + "." + ext), tmpl, params);
    }
}
catch (std::exception const& ex)
{
    std::cerr << ex.what();
    return -1;
}