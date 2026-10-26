#pragma once

// Small URL helpers, header-only so the PC tests can use them as is.

#include <cstdio>
#include <cstdlib>
#include <string>

namespace netutil
{

// "https://user:pw@Host.Example:8090/a?b" -> "host.example". Lower-cased, no
// port, no credentials; IPv6 brackets dropped.
inline std::string hostOf(const std::string& url)
{
    size_t b = url.find("://");
    b = (b == std::string::npos) ? 0 : b + 3;
    size_t e = url.find_first_of("/?#", b);
    std::string h = url.substr(b, e == std::string::npos ? std::string::npos : e - b);
    size_t at = h.rfind('@');
    if (at != std::string::npos) h = h.substr(at + 1);
    if (!h.empty() && h[0] == '[')
    {
        size_t close = h.find(']');
        h = h.substr(1, close == std::string::npos ? std::string::npos : close - 1);
    }
    else
    {
        size_t colon = h.find(':');
        if (colon != std::string::npos) h = h.substr(0, colon);
    }
    for (char& c : h)
        if (c >= 'A' && c <= 'Z') c = (char)(c - 'A' + 'a');
    return h;
}

// A host on this machine or the local network: loopback, the RFC 1918 ranges,
// link-local, and the names a home network hands out (no dot, .local, .lan,
// .home, .internal). A proxy can never reach those, so requests to them go
// direct.
inline bool isLocalHost(const std::string& host)
{
    if (host.empty()) return false;
    if (host == "localhost" || host == "::1") return true;
    if (host.find('.') == std::string::npos && host.find(':') == std::string::npos)
        return true;  // a bare name: "nas", "raspberrypi"
    auto endsWith = [&](const char* s) {
        std::string x = s;
        return host.size() > x.size() &&
               host.compare(host.size() - x.size(), x.size(), x) == 0;
    };
    if (endsWith(".local") || endsWith(".lan") || endsWith(".home") ||
        endsWith(".internal") || endsWith(".localhost"))
        return true;

    // Dotted IPv4.
    int o[4];
    char tail;
    if (std::sscanf(host.c_str(), "%d.%d.%d.%d%c", &o[0], &o[1], &o[2], &o[3], &tail) != 4)
        return false;
    for (int v : o)
        if (v < 0 || v > 255) return false;
    return o[0] == 127 || o[0] == 10 || (o[0] == 192 && o[1] == 168) ||
           (o[0] == 172 && o[1] >= 16 && o[1] <= 31) ||
           (o[0] == 169 && o[1] == 254);
}

} // namespace netutil
