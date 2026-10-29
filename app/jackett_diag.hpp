#pragma once

#include <string>

// What went wrong talking to Jackett, in terms a person can act on: curl's
// "Couldn't connect to server" or an empty 401 told nobody whether to check
// the address, the port, the key or Jackett itself. Pure (no UI, no network):
// jackett.cpp turns a Diagnosis into a translated sentence, the PC tests feed
// it canned answers.
namespace jackett
{

enum class Problem
{
    None,
    BadUrl,          // not a URL curl can use ("htp://", spaces)
    NoHost,          // the name does not resolve
    Unreachable,     // nothing listens there: Jackett not running, wrong port, wrong IP
    Timeout,         // asked, never answered in time
    Certificate,     // https with a chain that does not verify (often the clock)
    BadKey,          // 401 / 403 / "Invalid API key"
    Forbidden,       // a web server's own 403 page (a public mirror refusing us)
    NotJackett,      // something answered, but not Jackett's API (a web page, a 404)
    NoIndexers,      // Jackett works, but has no indexers to search
    IndexersFailed,  // every indexer returned an error
    Other,           // anything else; `detail` has the raw text
};

struct Diagnosis
{
    Problem problem = Problem::None;
    std::string detail;  // the raw error, or the first indexer's error
    int indexers = 0;    // in the answer's "Indexers" list
    int failed   = 0;    // of those, how many reported an error
    bool hasIndexerList = false;  // Jackett lists its indexers; Jacred does not
};

// curl gave up before an answer arrived: `err` is its message.
Diagnosis diagnoseTransport(const std::string& err);

// An answer arrived: its HTTP status (0 = unknown) and body. `query` is true
// for a search: an empty indexer list is then not an error by itself (a
// search can legitimately find nothing), but every indexer failing is.
Diagnosis diagnoseAnswer(long status, const std::string& body, bool query);

// The address as typed, made usable: spaces trimmed, "http://" added when no
// scheme was given, a trailing "/" dropped, and Jackett's default port 9117
// added when neither a scheme nor a port was typed for a local machine (an IP
// address, "localhost", a name without dots or ending .local / .lan / .home).
// A public name ("ru.jac.black", a Jacred mirror) keeps the default port.
std::string normalizeUrl(const std::string& typed);

} // namespace jackett
