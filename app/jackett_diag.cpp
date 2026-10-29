#include "jackett_diag.hpp"

#include <algorithm>
#include <cctype>

#include "json.hpp"

namespace jackett
{

namespace
{

std::string lower(std::string s)
{
    for (auto& c : s) c = (char)std::tolower((unsigned char)c);
    return s;
}

bool has(const std::string& hay, const char* needle)
{
    return hay.find(needle) != std::string::npos;
}

bool mentionsKey(const std::string& low)
{
    return has(low, "api key") || has(low, "apikey") || has(low, "api_key") ||
           has(low, "unauthorized");
}

} // namespace

Diagnosis diagnoseTransport(const std::string& err)
{
    Diagnosis d;
    d.detail        = err;
    std::string low = lower(err);
    if (has(low, "resolve host") || has(low, "resolve proxy"))
        d.problem = Problem::NoHost;
    else if (has(low, "timed out") || has(low, "timeout"))
        d.problem = Problem::Timeout;
    else if (has(low, "certificate") || has(low, "ssl") || has(low, "tls"))
        d.problem = Problem::Certificate;
    else if (has(low, "connect") || has(low, "refused") || has(low, "no route") ||
             has(low, "unreachable") || has(low, "reset by peer") ||
             has(low, "empty reply"))
        d.problem = Problem::Unreachable;
    else if (has(low, "url") || has(low, "protocol") || has(low, "malformed"))
        d.problem = Problem::BadUrl;
    else
        d.problem = Problem::Other;
    return d;
}

Diagnosis diagnoseAnswer(long status, const std::string& body, bool query)
{
    Diagnosis d;
    std::string low = lower(body);

    if (status == 401 || status == 403)
    {
        d.problem = Problem::BadKey;
        return d;
    }

    // Jackett's own error object: {"error": "..."} or {"message": "..."}.
    std::string apiErr = json::str(body, "error");
    if (apiErr.empty()) apiErr = json::str(body, "message");

    bool isApi = has(body, "\"Results\"") || has(body, "\"Indexers\"");
    if (!isApi)
    {
        if (!apiErr.empty())
        {
            d.problem = mentionsKey(lower(apiErr)) ? Problem::BadKey : Problem::Other;
            d.detail  = apiErr;
        }
        else if (status >= 500)
        {
            d.problem = Problem::Other;
            d.detail  = "HTTP " + std::to_string(status);
        }
        else
            // A login page, a router's page, a 404: not the API.
            d.problem = Problem::NotJackett;
        return d;
    }

    // {"Results":[...],"Indexers":[{"Name":..,"Error":null|"..."}]}
    for (const auto& ix : json::objects(body, "Indexers"))
    {
        d.indexers++;
        std::string e = json::str(ix, "Error");
        if (!e.empty())
        {
            d.failed++;
            if (d.detail.empty())
            {
                std::string name = json::str(ix, "Name");
                d.detail         = name.empty() ? e : name + ": " + e;
            }
        }
    }
    bool anyResult = !json::objects(body, "Results").empty();
    if (d.indexers == 0 && !query)
        d.problem = Problem::NoIndexers;
    else if (d.indexers > 0 && d.failed == d.indexers && !anyResult)
        d.problem = Problem::IndexersFailed;
    return d;
}

std::string normalizeUrl(const std::string& typed)
{
    std::string s = typed;
    auto space    = [](char c) { return std::isspace((unsigned char)c) != 0; };
    s.erase(s.begin(), std::find_if_not(s.begin(), s.end(), space));
    while (!s.empty() && space(s.back())) s.pop_back();
    while (!s.empty() && s.back() == '/') s.pop_back();
    if (s.empty()) return s;

    bool scheme = s.find("://") != std::string::npos;
    if (!scheme)
    {
        // A port after the host ("nas:9117", "[::1]:9117")? The host ends at
        // the first '/', and a port is a ':' after any IPv6 bracket.
        std::string host = s.substr(0, s.find('/'));
        size_t bracket   = host.rfind(']');
        size_t colon     = host.rfind(':');
        bool port = colon != std::string::npos &&
                    (bracket == std::string::npos || colon > bracket);
        if (!port)
        {
            size_t slash = s.find('/');
            if (slash == std::string::npos)
                s += ":9117";
            else
                s.insert(slash, ":9117");
        }
        s = "http://" + s;
    }
    return s;
}

} // namespace jackett
