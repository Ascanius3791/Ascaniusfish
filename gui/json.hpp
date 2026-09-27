// OWNERSHIP=Claude
// The little bit of JSON the GUI server needs: a writer for responses and a
// parser for request bodies. The bodies the browser sends are flat objects of
// scalars ({"id":"main","uci":"e2e4"}), so the parser only handles those and
// says so rather than pretending to be a general JSON parser.
#ifndef GUI_JSON_HPP
#define GUI_JSON_HPP
#include <cstdlib>
#include <map>
#include <string>

namespace json
{

inline std::string escape(const std::string& s)
{
    std::string out;
    for(unsigned char c : s)
    {
        switch(c)
        {
            case '"':  out += "\\\""; break;
            case '\\': out += "\\\\"; break;
            case '\n': out += "\\n";  break;
            case '\r': out += "\\r";  break;
            case '\t': out += "\\t";  break;
            default:
            if(c<0x20)
            {
                const char* hex = "0123456789abcdef";
                out += "\\u00";
                out += hex[c>>4];
                out += hex[c&15];
            }
            else
            out += (char)c;
        }
    }
    return out;
}

// Builds a JSON document, inserting the commas. Every value knows whether it
// follows another value in the same container, so callers only write structure:
//   Out o; o.obj().key("fen").str(f).key("dests").obj() ... .end_obj().end_obj();
struct Out
{
    std::string s;
    bool need_comma = false;  // a value was just written, so the next one needs a ','

    void sep()
    {
        if(need_comma)
        s += ',';
        need_comma = false;
    }

    Out& obj()     { sep(); s += '{'; return *this; }
    Out& end_obj() { s += '}'; need_comma = true; return *this; }
    Out& arr()     { sep(); s += '['; return *this; }
    Out& end_arr() { s += ']'; need_comma = true; return *this; }

    // A member name; the value that follows it must not be preceded by a comma.
    Out& key(const std::string& k)
    {
        sep();
        s += '"';
        s += escape(k);
        s += "\":";
        return *this;
    }

    Out& str(const std::string& v)  { sep(); s += '"'; s += escape(v); s += '"'; need_comma = true; return *this; }
    Out& num(long long v)           { sep(); s += std::to_string(v); need_comma = true; return *this; }
    Out& boolean(bool v)            { sep(); s += v ? "true" : "false"; need_comma = true; return *this; }
    Out& null()                     { sep(); s += "null"; need_comma = true; return *this; }
};

inline std::string error(const std::string& message)
{
    Out o;
    o.obj().key("error").str(message).end_obj();
    return o.s;
}

// Parses a flat JSON object into name -> value text (strings unescaped, numbers
// and true/false/null kept as written). Nested objects/arrays are refused.
inline bool parse_flat_object(const std::string& text, std::map<std::string, std::string>& out)
{
    size_t i = 0;
    auto skip = [&] { while(i<text.size() && (text[i]==' ' || text[i]=='\t' || text[i]=='\n' || text[i]=='\r')) i++; };
    auto string_at = [&](std::string& value)
    {
        if(i>=text.size() || text[i]!='"')
        return false;
        i++;
        value.clear();
        while(i<text.size() && text[i]!='"')
        {
            if(text[i]!='\\')
            {
                value += text[i++];
                continue;
            }
            if(++i>=text.size())
            return false;
            char c = text[i++];
            if(c=='u')
            {
                if(i+4>text.size())
                return false;
                int code = (int)strtol(text.substr(i, 4).c_str(), nullptr, 16);
                i += 4;
                if(code<0x80)                       // the GUI's strings are ASCII;
                value += (char)code;                // anything wider is encoded as UTF-8
                else if(code<0x800)
                {
                    value += (char)(0xC0 | code>>6);
                    value += (char)(0x80 | (code&0x3F));
                }
                else
                {
                    value += (char)(0xE0 | code>>12);
                    value += (char)(0x80 | ((code>>6)&0x3F));
                    value += (char)(0x80 | (code&0x3F));
                }
            }
            else
            value += c=='n' ? '\n' : c=='r' ? '\r' : c=='t' ? '\t' : c=='b' ? '\b' : c=='f' ? '\f' : c;
        }
        if(i>=text.size())
        return false;
        i++;
        return true;
    };

    out.clear();
    skip();
    if(i>=text.size() || text[i]!='{')
    return false;
    i++;
    skip();
    if(i<text.size() && text[i]=='}')
    return true;
    for(;;)
    {
        skip();
        std::string name, value;
        if(!string_at(name))
        return false;
        skip();
        if(i>=text.size() || text[i]!=':')
        return false;
        i++;
        skip();
        if(i>=text.size())
        return false;
        if(text[i]=='"')
        {
            if(!string_at(value))
            return false;
        }
        else if(text[i]=='{' || text[i]=='[')
        return false;  // only flat objects
        else
        {
            size_t start = i;
            while(i<text.size() && text[i]!=',' && text[i]!='}' && text[i]!=' ' && text[i]!='\n' && text[i]!='\r' && text[i]!='\t')
            i++;
            value = text.substr(start, i-start);
            if(value.empty())
            return false;
        }
        out[name] = value;
        skip();
        if(i<text.size() && text[i]==',')
        {
            i++;
            continue;
        }
        return i<text.size() && text[i]=='}';
    }
}

}  // namespace json

#endif // GUI_JSON_HPP
