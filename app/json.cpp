#include "json.hpp"

#include <cstring>

// A JSON reader sized to what this app actually receives: flat "key": value
// lookups and arrays of objects, from APIs whose shape we know (Stremio,
// GitHub). It is not a parser -- it does no validation and has no model of the
// document -- but it keeps a real JSON library out of a build that cross-
// compiles for the Switch.

namespace json
{

std::string str(const std::string& body, const char* key, size_t from)
{
    std::string pat = std::string("\"") + key + "\"";
    while (from < body.size())
    {
        size_t k = body.find(pat, from);
        if (k == std::string::npos) return "";

        size_t colon = k + pat.size();
        while (colon < body.size() && (body[colon] == ' ' || body[colon] == '\t' ||
                                      body[colon] == '\r' || body[colon] == '\n'))
            colon++;

        if (colon < body.size() && body[colon] == ':')
        {
            size_t valStart = colon + 1;
            while (valStart < body.size() && (body[valStart] == ' ' || body[valStart] == '\t' ||
                                              body[valStart] == '\r' || body[valStart] == '\n'))
                valStart++;

            if (valStart >= body.size() || body[valStart] != '"')
                return "";

            size_t q1 = valStart;

            auto hex = [](char c) -> int {
                if (c >= '0' && c <= '9') return c - '0';
                if (c >= 'a' && c <= 'f') return c - 'a' + 10;
                if (c >= 'A' && c <= 'F') return c - 'A' + 10;
                return -1;
            };

            std::string out;
            for (size_t i = q1 + 1; i < body.size(); i++)
            {
                if (body[i] == '"') break;
                if (body[i] != '\\' || i + 1 >= body.size())
                {
                    out += body[i];
                    continue;
                }

                char esc = body[++i];
                switch (esc)
                {
                    case 'n': out += '\n'; break;
                    case 'r': out += '\r'; break;
                    case 't': out += '\t'; break;
                    case 'b': out += '\b'; break;
                    case 'f': out += '\f'; break;
                    case 'u':
                    {
                        // \uXXXX. Previously the backslash was dropped and the escape
                        // letter kept verbatim, so "&" (sent as &) surfaced as the
                        // literal text "u0026".
                        if (i + 4 >= body.size()) { out += esc; break; }
                        int cp = 0, ok = 1;
                        for (int d = 1; d <= 4; d++)
                        {
                            int h = hex(body[i + d]);
                            if (h < 0) { ok = 0; break; }
                            cp = cp * 16 + h;
                        }
                        if (!ok) { out += esc; break; }
                        i += 4;

                        // Surrogate pair: the high half alone is not a character.
                        if (cp >= 0xD800 && cp <= 0xDBFF && i + 6 < body.size() &&
                            body[i + 1] == '\\' && body[i + 2] == 'u')
                        {
                            int lo = 0, ok2 = 1;
                            for (int d = 3; d <= 6; d++)
                            {
                                int h = hex(body[i + d]);
                                if (h < 0) { ok2 = 0; break; }
                                lo = lo * 16 + h;
                            }
                            if (ok2 && lo >= 0xDC00 && lo <= 0xDFFF)
                            {
                                cp = 0x10000 + ((cp - 0xD800) << 10) + (lo - 0xDC00);
                                i += 6;
                            }
                        }

                        // Encode as UTF-8; the UI's text stack expects it.
                        if (cp < 0x80)
                            out += (char)cp;
                        else if (cp < 0x800)
                        {
                            out += (char)(0xC0 | (cp >> 6));
                            out += (char)(0x80 | (cp & 0x3F));
                        }
                        else if (cp < 0x10000)
                        {
                            out += (char)(0xE0 | (cp >> 12));
                            out += (char)(0x80 | ((cp >> 6) & 0x3F));
                            out += (char)(0x80 | (cp & 0x3F));
                        }
                        else
                        {
                            out += (char)(0xF0 | (cp >> 18));
                            out += (char)(0x80 | ((cp >> 12) & 0x3F));
                            out += (char)(0x80 | ((cp >> 6) & 0x3F));
                            out += (char)(0x80 | (cp & 0x3F));
                        }
                        break;
                    }
                    default: out += esc; break;  // \" \\ \/ and anything unknown
                }
            }
            return out;
        }

        from = k + 1;
    }
    return "";
}

// A literal find of "\"key\":true" silently fails the moment a server pretty-
// prints, and a filter that never matches is invisible -- hence the whitespace
// skip.
bool boolean(const std::string& body, const char* key, bool dflt)
{
    std::string pat = std::string("\"") + key + "\"";
    size_t from = 0;
    while (from < body.size())
    {
        size_t k = body.find(pat, from);
        if (k == std::string::npos) return dflt;
        size_t colon = k + pat.size();
        while (colon < body.size() && (body[colon] == ' ' || body[colon] == '\t' ||
                                      body[colon] == '\r' || body[colon] == '\n'))
            colon++;
        if (colon < body.size() && body[colon] == ':')
        {
            size_t v = colon + 1;
            while (v < body.size() && (body[v] == ' ' || body[v] == '\t' ||
                                       body[v] == '\r' || body[v] == '\n')) v++;
            if (v >= body.size()) return dflt;
            if (body.compare(v, 4, "true") == 0) return true;
            if (body.compare(v, 5, "false") == 0) return false;
            return dflt;
        }
        from = k + 1;
    }
    return dflt;
}

std::string escape(const std::string& s)
{
    std::string out;
    for (char c : s)
    {
        if (c == '"' || c == '\\') out += '\\';
        out += c;
    }
    return out;
}

long integer(const std::string& body, const char* key, long dflt)
{
    std::string pat = std::string("\"") + key + "\"";
    size_t from = 0;
    while (from < body.size())
    {
        size_t k = body.find(pat, from);
        if (k == std::string::npos) return dflt;
        size_t colon = k + pat.size();
        while (colon < body.size() && (body[colon] == ' ' || body[colon] == '\t' ||
                                      body[colon] == '\r' || body[colon] == '\n'))
            colon++;
        if (colon < body.size() && body[colon] == ':')
        {
            size_t v = colon + 1;
            while (v < body.size() && (body[v] == ' ' || body[v] == '\t' ||
                                       body[v] == '\r' || body[v] == '\n')) v++;
            if (v >= body.size()) return dflt;
            bool neg = body[v] == '-';
            if (neg) v++;
            if (v >= body.size() || body[v] < '0' || body[v] > '9') return dflt;
            long n = 0;
            while (v < body.size() && body[v] >= '0' && body[v] <= '9')
                n = n * 10 + (body[v++] - '0');
            return neg ? -n : n;
        }
        from = k + 1;
    }
    return dflt;
}

double number(const std::string& body, const char* key, double dflt)
{
    std::string pat = std::string("\"") + key + "\"";
    size_t from = 0;
    while (from < body.size())
    {
        size_t k = body.find(pat, from);
        if (k == std::string::npos) return dflt;
        size_t colon = k + pat.size();
        while (colon < body.size() && (body[colon] == ' ' || body[colon] == '\t' ||
                                      body[colon] == '\r' || body[colon] == '\n'))
            colon++;
        if (colon < body.size() && body[colon] == ':')
        {
            size_t v = colon + 1;
            while (v < body.size() && (body[v] == ' ' || body[v] == '\t' ||
                                       body[v] == '\r' || body[v] == '\n')) v++;
            if (v >= body.size()) return dflt;
            char* end = nullptr;
            double n = std::strtod(body.c_str() + v, &end);
            if (end == body.c_str() + v) return dflt;
            return n;
        }
        from = k + 1;
    }
    return dflt;
}

// Splits the top-level objects out of the array at `key` by brace depth, so each
// item can be read on its own. Depth-counting (rather than searching for keys
// across the whole body) is what keeps nested objects -- "state", "behaviorHints"
// -- from being mistaken for items, and it skips braces inside strings.
std::vector<std::string> objects(const std::string& body, const char* key)
{
    std::vector<std::string> out;
    std::string pat = std::string("\"") + key + "\"";
    size_t r        = body.find(pat);
    if (r == std::string::npos) return out;
    size_t colon = body.find(':', r + pat.size());
    if (colon == std::string::npos) return out;
    size_t open = colon + 1;
    while (open < body.size() && (body[open] == ' ' || body[open] == '\t' ||
                                  body[open] == '\r' || body[open] == '\n'))
        open++;
    if (open >= body.size() || body[open] != '[') return out;

    int depth      = 0;
    bool inStr     = false;
    size_t objFrom = std::string::npos;
    for (size_t i = open + 1; i < body.size(); i++)
    {
        char c = body[i];
        if (inStr)
        {
            if (c == '\\') { i++; continue; }
            if (c == '"') inStr = false;
            continue;
        }
        if (c == '"') { inStr = true; continue; }
        if (c == '{')
        {
            if (depth == 0) objFrom = i;
            depth++;
        }
        else if (c == '}')
        {
            depth--;
            if (depth == 0 && objFrom != std::string::npos)
            {
                out.push_back(body.substr(objFrom, i - objFrom + 1));
                objFrom = std::string::npos;
            }
        }
        else if (c == ']' && depth == 0)
        {
            break;  // end of the result array
        }
    }
    return out;
}

std::string topLevel(const std::string& obj)
{
    // Copy the text, but inside any nested {...} or [...] keep only the
    // brackets themselves: then a lookup can only land on the outer object's
    // own keys. String contents are copied as they are (a brace in a title is
    // not structure).
    std::string out;
    out.reserve(obj.size());
    int depth  = 0;  // 1 = inside the outer object
    bool inStr = false;
    for (size_t i = 0; i < obj.size(); i++)
    {
        char c = obj[i];
        bool keep = depth <= 1;
        if (inStr)
        {
            if (keep) out += c;
            if (c == '\\' && i + 1 < obj.size())
            {
                if (keep) out += obj[i + 1];
                i++;
            }
            else if (c == '"')
                inStr = false;
            continue;
        }
        if (c == '"')
        {
            inStr = true;
            if (keep) out += c;
            continue;
        }
        if (c == '{' || c == '[')
        {
            if (depth <= 1) out += c;
            depth++;
            continue;
        }
        if (c == '}' || c == ']')
        {
            depth--;
            if (depth <= 1) out += c;
            continue;
        }
        if (keep) out += c;
    }
    return out;
}

std::string object(const std::string& body, const char* key)
{
    std::string pat = std::string("\"") + key + "\"";
    size_t from = 0;
    while (from < body.size())
    {
        size_t k = body.find(pat, from);
        if (k == std::string::npos) return "";
        size_t v = k + pat.size();
        while (v < body.size() && (body[v] == ' ' || body[v] == '\t' ||
                                   body[v] == '\r' || body[v] == '\n')) v++;
        if (v < body.size() && body[v] == ':')
        {
            v++;
            while (v < body.size() && (body[v] == ' ' || body[v] == '\t' ||
                                       body[v] == '\r' || body[v] == '\n')) v++;
            if (v >= body.size() || body[v] != '{') return "";
            int depth  = 0;
            bool inStr = false;
            for (size_t i = v; i < body.size(); i++)
            {
                char c = body[i];
                if (inStr)
                {
                    if (c == '\\') i++;
                    else if (c == '"') inStr = false;
                    continue;
                }
                if (c == '"') inStr = true;
                else if (c == '{') depth++;
                else if (c == '}' && --depth == 0)
                    return body.substr(v, i - v + 1);
            }
            return "";
        }
        from = k + 1;
    }
    return "";
}

// Collects the plain strings of a JSON array like "types":["movie","series"].
std::vector<std::string> strings(const std::string& body, const char* key)
{
    std::vector<std::string> out;
    std::string pat = std::string("\"") + key + "\"";
    size_t k        = body.find(pat);
    if (k == std::string::npos) return out;
    size_t colon = body.find(':', k + pat.size());
    if (colon == std::string::npos) return out;
    size_t open = colon + 1;
    while (open < body.size() && (body[open] == ' ' || body[open] == '\t' ||
                                  body[open] == '\r' || body[open] == '\n'))
        open++;
    if (open >= body.size() || body[open] != '[') return out;
    bool inStr = false;
    std::string cur;
    for (size_t i = open + 1; i < body.size(); i++)
    {
        char c = body[i];
        if (inStr)
        {
            if (c == '\\' && i + 1 < body.size()) { cur += body[++i]; continue; }
            if (c == '"') { out.push_back(cur); cur.clear(); inStr = false; continue; }
            cur += c;
        }
        else if (c == '"') inStr = true;
        else if (c == ']') break;
        else if (c == '{' || c == '[') return out;  // not a flat string array
    }
    return out;
}

} // namespace json
