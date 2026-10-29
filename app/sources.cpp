#include "sources.hpp"

#include <borealis.hpp>

#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <map>
#include <memory>
#include <set>
#include <string>
#include <vector>

#include "appdata.hpp"
#include "browse.hpp"        // stremioStreamSource
#include "config.hpp"
#include "i18n.hpp"
#include "jackett.hpp"
#include "library.hpp"
#include "player.hpp"        // startPlaybackPrompt
#include "release.hpp"
#include "stremio.hpp"
#include "theme.hpp"
#include "torrent_play.hpp"
#include "widgets.hpp"

namespace
{

// ---------------------------------------------------------------------------
// The rows
// ---------------------------------------------------------------------------

struct Source
{
    enum class Kind
    {
        Jackett,
        StremioTorrent,  // an addon's infoHash, played as a magnet
        StremioUrl,      // an addon's direct link
    } kind = Kind::Jackett;

    std::string title;    // the release name
    std::string tracker;  // tracker or addon
    int64_t size = 0;
    int seeders  = -1;
    release::Info info;
    int score = 0;

    jackett::Item jitem;  // Kind::Jackett
    std::string src;      // a magnet or a URL (Stremio)
    int fileIdx = -1;     // the addon's file index (season packs)
};

// "1.4 GB" / "850 MB" after the marker, or 0.
int64_t sizeAfter(const std::string& s, const char* marker)
{
    size_t k = s.find(marker);
    if (k == std::string::npos) return 0;
    const char* p = s.c_str() + k + std::strlen(marker);
    while (*p == ' ') p++;
    char* end = nullptr;
    double v = std::strtod(p, &end);
    if (end == p) return 0;
    while (*end == ' ') end++;
    double mul = 1;
    if (*end == 'G' || *end == 'g') mul = 1024.0 * 1024 * 1024;
    else if (*end == 'M' || *end == 'm') mul = 1024.0 * 1024;
    else if (*end == 'T' || *end == 't') mul = 1024.0 * 1024 * 1024 * 1024;
    return (int64_t)(v * mul);
}

int numberAfter(const std::string& s, const char* marker)
{
    size_t k = s.find(marker);
    if (k == std::string::npos) return -1;
    const char* p = s.c_str() + k + std::strlen(marker);
    while (*p == ' ') p++;
    if (*p < '0' || *p > '9') return -1;
    return std::atoi(p);
}

std::string restOfLineAfter(const std::string& s, const char* marker)
{
    size_t k = s.find(marker);
    if (k == std::string::npos) return "";
    size_t b = k + std::strlen(marker);
    while (b < s.size() && s[b] == ' ') b++;
    size_t e = s.find('\n', b);
    std::string r = s.substr(b, e == std::string::npos ? std::string::npos : e - b);
    while (!r.empty() && r.back() == ' ') r.pop_back();
    return r;
}

// An addon stream as a row. Torrentio and its kin write the details into the
// title: "Release.Name\n👤 23 💾 1.4 GB ⚙️ RuTracker".
Source fromStream(const stremio::Stream& s, const std::string& addon)
{
    static const char* kSeeds   = "\xF0\x9F\x91\xA4";             // 👤
    static const char* kSize    = "\xF0\x9F\x92\xBE";             // 💾
    static const char* kTracker = "\xE2\x9A\x99\xEF\xB8\x8F";    // ⚙️

    Source r;
    r.kind    = s.infoHash.empty() ? Source::Kind::StremioUrl : Source::Kind::StremioTorrent;
    r.src     = stremioStreamSource(s);
    r.fileIdx = s.fileIdx;
    std::string firstLine = s.title.substr(0, s.title.find('\n'));
    r.title   = firstLine.empty() ? s.name : firstLine;
    for (char& c : r.title)
        if (c == '\n') c = ' ';
    r.seeders = numberAfter(s.title, kSeeds);
    r.size    = sizeAfter(s.title, kSize);
    r.tracker = restOfLineAfter(s.title, kTracker);
    if (r.tracker.empty()) r.tracker = addon;
    // The quality is often only in the name ("Torrentio\n1080p").
    r.info = release::parse(s.name + " " + s.title);
    return r;
}

Source fromJackett(const jackett::Item& it)
{
    Source r;
    r.kind    = Source::Kind::Jackett;
    r.jitem   = it;
    r.title   = it.title;
    r.tracker = it.tracker;
    r.size    = it.size;
    r.seeders = it.seeders;
    r.info    = release::parse(it.title);
    return r;
}

// ---------------------------------------------------------------------------
// What was picked for a show last time
// ---------------------------------------------------------------------------

const char* kChoices = APPDATA_DIR "/choices.tsv";  // key \t voice \t group

void loadChoice(const std::string& key, std::string& voice, std::string& group)
{
    FILE* f = std::fopen(kChoices, "rb");
    if (!f) return;
    char line[512];
    while (std::fgets(line, sizeof(line), f))
    {
        std::string l = line;
        while (!l.empty() && (l.back() == '\n' || l.back() == '\r')) l.pop_back();
        size_t a = l.find('\t');
        if (a == std::string::npos || l.substr(0, a) != key) continue;
        size_t b = l.find('\t', a + 1);
        voice = l.substr(a + 1, b == std::string::npos ? std::string::npos : b - a - 1);
        group = b == std::string::npos ? "" : l.substr(b + 1);
    }
    std::fclose(f);
}

void saveChoice(const std::string& key, const std::string& voice, const std::string& group)
{
    if (voice.empty() && group.empty()) return;
    std::vector<std::string> lines;
    if (FILE* f = std::fopen(kChoices, "rb"))
    {
        char line[512];
        while (std::fgets(line, sizeof(line), f))
        {
            std::string l = line;
            while (!l.empty() && (l.back() == '\n' || l.back() == '\r')) l.pop_back();
            if (!l.empty() && l.compare(0, key.size() + 1, key + "\t") != 0) lines.push_back(l);
        }
        std::fclose(f);
    }
    lines.push_back(key + "\t" + voice + "\t" + group);
    // A few hundred shows at most; the oldest go first.
    if (lines.size() > 300) lines.erase(lines.begin(), lines.end() - 300);
    if (FILE* f = std::fopen(kChoices, "wb"))
    {
        for (const auto& l : lines) std::fprintf(f, "%s\n", l.c_str());
        std::fclose(f);
    }
}

// ---------------------------------------------------------------------------
// What the player is told about a catalog title
// ---------------------------------------------------------------------------

// Title, artwork and the watch context for `item` (an episode of it when
// season/episode are set): progress to the library, and to the Stremio account
// when signed in; the next episode when there is one. `nextPop`: activities to
// close before the next episode opens (see WatchInfo).
PlayHint hintFor(const content::Item& item, int season, int episode, int episodesInSeason,
                 int nextPop)
{
    PlayHint hint;
    bool ep      = item.isSeries() && season > 0 && episode > 0;
    hint.title   = item.title;
    hint.season  = season;
    hint.episode = episode;
    if (ep) hint.title += "  \xC2\xB7  S" + std::to_string(season) + "E" + std::to_string(episode);
    hint.art.posterPath = stremio::cachedPosterPath(item.key());
    hint.art.bgId       = item.key() + "_bd";
    hint.art.bgUrl      = item.backdropUrl;

    hint.watch.content = item;
    hint.watch.season  = ep ? season : 0;
    hint.watch.episode = ep ? episode : 0;
    hint.watch.nextPop = nextPop;
    bool hasNext = ep && (episodesInSeason <= 0 || episode < episodesInSeason);
    // The id only labels the "next episode" card here ("Season 1 · Episode 4");
    // the library opens it.
    if (hasNext)
        hint.watch.nextVideoId = item.key() + ":" + std::to_string(season) + ":" +
                                 std::to_string(episode + 1);

    // Signed in: the position goes to the account too, so Stremio's "Continue
    // watching" follows a Jackett playback as well.
    std::string authKey = stremio::loadAuthKey();
    if (!authKey.empty() && !item.imdbId.empty())
    {
        hint.watch.authKey = authKey;
        hint.watch.itemId  = item.imdbId;
        hint.watch.type    = item.stremioType();
        hint.watch.videoId = item.imdbId;
        if (ep)
        {
            hint.watch.videoId = item.imdbId + ":" + std::to_string(season) + ":" +
                                 std::to_string(episode);
            if (hasNext)
                hint.watch.nextVideoId = item.imdbId + ":" + std::to_string(season) + ":" +
                                         std::to_string(episode + 1);
        }
    }
    hint.watch.displayTitle = hint.title;
    return hint;
}

// ---------------------------------------------------------------------------
// The screen
// ---------------------------------------------------------------------------

enum class Order
{
    Recommended,
    Seeders,
    Size,
};

std::string qualityText(int h)
{
    return h > 0 ? std::to_string(h) + "p" : "\xE2\x80\x94";  // —
}

std::string sizeText(int64_t b) { return b > 0 ? jackett::formatSize(b) : ""; }

class SourcesView : public brls::Box
{
  public:
    SourcesView(const content::Item& item, int season, int episode, int episodesInSeason)
        : item(item), season(season), episode(episode), episodesInSeason(episodesInSeason)
    {
        setAxis(brls::Axis::COLUMN);
        setGrow(1.0f);
        setPadding(20.0f, 60.0f, 10.0f, 60.0f);

        want.series        = item.isSeries();
        want.season        = season;
        want.episode       = episode;
        want.preferRussian = i18n::lang() == "ru";
        if (item.isSeries()) loadChoice(item.key(), want.voice, want.group);

        // Head: "Torrents" and what for.
        auto* head = new brls::Box();
        head->setAxis(brls::Axis::ROW);
        head->setAlignItems(brls::AlignItems::FLEX_END);
        auto* h = new brls::Label();
        h->setText(tr("Torrents"));
        h->setFontSize(30.0f);
        head->addView(h);
        subtitle = new brls::Label();
        subtitle->setFontSize(18.0f);
        subtitle->setTextColor(theme::textMuted());
        subtitle->setMarginLeft(20.0f);
        subtitle->setSingleLine(true);
        head->addView(subtitle);
        addView(head);
        updateSubtitle();

        // Filters: quality, then the order.
        auto* filters = new brls::Box();
        filters->setAxis(brls::Axis::ROW);
        filters->setMargins(14.0f, 0.0f, 10.0f, 0.0f);
        const std::pair<const char*, int> qualities[] = {
            { "All", 0 }, { "2160p", 2160 }, { "1080p", 1080 }, { "720p", 720 }
        };
        for (const auto& q : qualities)
        {
            int height = q.second;
            auto* b = ui::actionButton(q.second ? q.first : tr(q.first), false, [this, height]() {
                filterHeight = height;
                restyleFilters();
                rebuild();
            });
            filterBtns.push_back({ b, height });
            filters->addView(b);
        }
        orderBtn = ui::actionButton("", false, [this]() { cycleOrder(); });
        filters->addView(orderBtn);
        addView(filters);

        // The second row: what the releases found actually differ in. Each
        // opens a list of the values present in the results.
        auto* more = new brls::Box();
        more->setAxis(brls::Axis::ROW);
        more->setMargins(0.0f, 0.0f, 10.0f, 0.0f);
        voiceBtn   = ui::actionButton("", false, [this]() { pickFacet(Facet::Voice); });
        sourceBtn  = ui::actionButton("", false, [this]() { pickFacet(Facet::Source); });
        hdrBtn     = ui::actionButton("", false, [this]() { pickFacet(Facet::Hdr); });
        trackerBtn = ui::actionButton("", false, [this]() { pickFacet(Facet::Tracker); });
        more->addView(voiceBtn);
        more->addView(sourceBtn);
        more->addView(hdrBtn);
        more->addView(trackerBtn);
        addView(more);
        restyleFilters();

        status = new brls::Label();
        status->setFontSize(20.0f);
        status->setTextColor(theme::textMuted());
        status->setMarginTop(20.0f);
        status->setText(tr("Searching..."));
        addView(status);

        auto* scroll = new brls::ScrollingFrame();
        scroll->setGrow(1.0f);
        scroll->setScrollingBehavior(brls::ScrollingBehavior::CENTERED);
        list = new brls::Box();
        list->setAxis(brls::Axis::COLUMN);
        list->setPadding(6.0f, 4.0f, 20.0f, 4.0f);
        scroll->setContentView(list);
        addView(scroll);

        registerAction(tr("Order"), brls::BUTTON_X, [this](brls::View*) {
            cycleOrder();
            return true;
        });

        search();
    }

    ~SourcesView() override { *alive = false; }

  private:
    content::Item item;
    int season, episode, episodesInSeason;
    release::Want want;
    std::shared_ptr<bool> alive = std::make_shared<bool>(true);

    std::vector<Source> found;
    std::set<std::string> seen;  // dedup keys
    int pending = 0;             // requests not answered yet, all sources
    // Jackett is the main source: its answers are shown as soon as they are
    // all in, without waiting for the Stremio addons (one slow addon held the
    // whole list back). Addon results are folded in as they land.
    bool jackettAsked  = false;
    int pendingJackett = 0;
    bool shown         = false;  // the list has been built at least once
    bool dirty         = false;  // new results while another screen was on top
    bool rebuildQueued = false;
    std::vector<std::pair<brls::View*, std::string>> rowKeys;  // row -> dedupKey
    std::vector<std::string> errors;

    static std::string dedupKey(const Source& s)
    {
        return s.title + "|" + std::to_string(s.size / (1024 * 1024));
    }

    // What can be listed: Jackett's answers once they are all in; without
    // Jackett, everything (the addons only).
    bool listReady() const { return jackettAsked ? pendingJackett == 0 : pending == 0; }

    bool isOnTop()
    {
        auto stack = brls::Application::getActivitiesStack();
        return !stack.empty() && stack.back() == getParentActivity();
    }

    // Late results (an addon answering while the player or a dialog is open)
    // wait until this screen is on top again: borealis' focus stack keeps a
    // raw pointer to the row that opened the screen above, and freeing that
    // row would crash when it closes and hands the focus back.
    void refresh()
    {
        if (isOnTop())
        {
            dirty = false;
            rebuild();
        }
        else
            dirty = true;
    }

    void draw(NVGcontext* vg, float x, float y, float w, float h, brls::Style style,
              brls::FrameContext* ctx) override
    {
        brls::Box::draw(vg, x, y, w, h, style, ctx);
        if (dirty && !rebuildQueued && isOnTop())
        {
            rebuildQueued = true;
            auto live     = alive;
            brls::sync([this, live]() {
                if (!*live) return;
                rebuildQueued = false;
                if (dirty) refresh();  // still not on top: draw() retries
            });
        }
    }

    int filterHeight = 0;

    // The second filter row. "" = any.
    enum class Facet
    {
        Voice,
        Source,
        Hdr,
        Tracker,
    };
    std::string filterVoice, filterSource, filterTracker;
    int filterHdr = 0;  // 0 any, 1 without HDR, 2 HDR / Dolby Vision only
    brls::Button *voiceBtn = nullptr, *sourceBtn = nullptr, *hdrBtn = nullptr,
                 *trackerBtn = nullptr;

    // The values of a facet among the releases shown (another season's are
    // not offered), most common first.
    std::vector<std::string> facetValues(Facet f) const
    {
        std::map<std::string, int> count;
        for (const auto& r : found)
        {
            if (r.score <= -500) continue;
            if (f == Facet::Voice)
                for (const auto& v : r.info.voices) count[v]++;
            else if (f == Facet::Source && !r.info.source.empty())
                count[r.info.source]++;
            else if (f == Facet::Tracker && !r.tracker.empty())
                count[r.tracker]++;
        }
        std::vector<std::pair<std::string, int>> v(count.begin(), count.end());
        std::stable_sort(v.begin(), v.end(),
                         [](const auto& a, const auto& b) { return a.second > b.second; });
        std::vector<std::string> out;
        for (const auto& p : v) out.push_back(p.first);
        return out;
    }

    void pickFacet(Facet f)
    {
        if (!listReady()) return;  // nothing listed yet to pick from
        std::vector<std::string> labels, values;
        if (f == Facet::Hdr)
        {
            labels = { tr("Any picture"), tr("Without HDR"), tr("HDR / Dolby Vision") };
            values = { "0", "1", "2" };
        }
        else
        {
            labels.push_back(f == Facet::Voice    ? tr("Any voice-over")
                             : f == Facet::Source ? tr("Any source")
                                                  : tr("Any tracker"));
            values.push_back("");
            for (const auto& v : facetValues(f))
            {
                labels.push_back(v);
                values.push_back(v);
            }
        }
        const std::string& cur = f == Facet::Voice    ? filterVoice
                                 : f == Facet::Source ? filterSource
                                 : f == Facet::Tracker ? filterTracker
                                                       : std::string();
        int sel = 0;
        for (size_t i = 0; i < values.size(); i++)
            if (f == Facet::Hdr ? (int)i == filterHdr : values[i] == cur) sel = (int)i;
        const char* title = f == Facet::Voice    ? "Voice-over"
                            : f == Facet::Source ? "Source"
                            : f == Facet::Hdr    ? "Picture"
                                                 : "Tracker";
        auto* dd = new brls::Dropdown(tr(title), labels, [this, f, values](int i) {
            if (i < 0 || i >= (int)values.size()) return;
            if (f == Facet::Voice) filterVoice = values[i];
            else if (f == Facet::Source) filterSource = values[i];
            else if (f == Facet::Tracker) filterTracker = values[i];
            else filterHdr = i;
            restyleFilters();
            rebuild();
        }, sel);
        brls::Application::pushActivity(new brls::Activity(dd));
    }

    bool facetsPass(const Source& r) const
    {
        if (!filterVoice.empty() &&
            std::find(r.info.voices.begin(), r.info.voices.end(), filterVoice) ==
                r.info.voices.end())
            return false;
        if (!filterSource.empty() && r.info.source != filterSource) return false;
        if (!filterTracker.empty() && r.tracker != filterTracker) return false;
        if (filterHdr == 1 && r.info.hdr) return false;
        if (filterHdr == 2 && !r.info.hdr) return false;
        return true;
    }
    Order order      = Order::Recommended;

    brls::Label* subtitle = nullptr;
    brls::Label* status   = nullptr;
    brls::Box* list       = nullptr;
    brls::Button* orderBtn = nullptr;
    std::vector<std::pair<brls::Button*, int>> filterBtns;

    void updateSubtitle()
    {
        std::string s = item.title;
        if (item.isSeries() && season > 0)
        {
            s += std::string("  \xC2\xB7  ") + tr("Season ") + std::to_string(season);
            if (episode > 0)
                s += std::string("  \xC2\xB7  ") + tr("Episode ") + std::to_string(episode);
        }
        else if (!item.year.empty())
            s += " (" + item.year + ")";
        if (listReady())
        {
            int n = 0;
            for (const auto& r : found)
                if (r.score > -500) n++;
            s += "  \xC2\xB7  " + std::to_string(n) + tr(" releases");
            // Listed already, the addons still answering.
            if (pending > 0) s += std::string("  \xC2\xB7  ") + tr("searching addons...");
        }
        subtitle->setText(s);
    }

    void restyleFilters()
    {
        for (auto& f : filterBtns)
            ui::setPillActive(f.first, f.second == filterHeight);
        const char* o = order == Order::Recommended ? "Order: recommended"
                        : order == Order::Seeders   ? "Order: seeders"
                                                    : "Order: size";
        orderBtn->setText(tr(o));

        // The second row says what it filters on, or the value picked.
        voiceBtn->setText(filterVoice.empty() ? std::string(tr("Voice-over")) : filterVoice);
        sourceBtn->setText(filterSource.empty() ? std::string(tr("Source")) : filterSource);
        hdrBtn->setText(filterHdr == 1   ? tr("Without HDR")
                        : filterHdr == 2 ? tr("HDR / Dolby Vision")
                                         : tr("Picture"));
        trackerBtn->setText(filterTracker.empty() ? std::string(tr("Tracker")) : filterTracker);
        ui::setPillActive(voiceBtn, !filterVoice.empty());
        ui::setPillActive(sourceBtn, !filterSource.empty());
        ui::setPillActive(hdrBtn, filterHdr != 0);
        ui::setPillActive(trackerBtn, !filterTracker.empty());
    }

    void cycleOrder()
    {
        order = order == Order::Recommended ? Order::Seeders
                : order == Order::Seeders   ? Order::Size
                                            : Order::Recommended;
        restyleFilters();
        rebuild();
    }

    // ---- gathering ----------------------------------------------------------

    bool addSource(Source s)
    {
        // The same release from two indexers, or from Jackett and an addon.
        if (!seen.insert(dedupKey(s)).second) return false;
        s.score = release::score(s.info, s.seeders, s.size, want);
        found.push_back(std::move(s));
        return true;
    }

    // One request answered. `jackett`: it was one of Jackett's; `added`: it
    // brought releases not seen yet.
    void done(const std::string& err, bool jackett, bool added)
    {
        if (!err.empty()) errors.push_back(err);
        pending--;
        if (jackett) pendingJackett--;
        updateSubtitle();
        if (!listReady())
        {
            status->setText(tr("Searching...") + std::string("  ") +
                            std::to_string(found.size()) + tr(" found so far"));
            return;
        }
        // The first listing, new releases, or the last answer (which turns
        // "searching" into "no results" when nothing came).
        if (!shown || added || pending == 0) refresh();
    }

    void search()
    {
        bool jackettOn = !config::get().jackettUrl.empty();
        std::string authKey = stremio::loadAuthKey();
        bool stremioOn = !authKey.empty() && !item.imdbId.empty();

        if (!jackettOn && !stremioOn)
        {
            status->setText(tr("To find sources, set up Jackett in Options (or sign in to "
                               "Stremio)."));
            return;
        }

        // Jackett: the title in the UI language and the original one -- a
        // Russian tracker files "Дюна", an English one "Dune". A film gets
        // its year, which keeps remakes and namesakes out.
        std::vector<std::string> queries;
        if (jackettOn)
        {
            auto q = [&](const std::string& t) {
                if (t.empty()) return;
                std::string s = t;
                if (!item.isSeries() && !item.year.empty()) s += " " + item.year;
                if (std::find(queries.begin(), queries.end(), s) == queries.end())
                    queries.push_back(s);
            };
            q(item.title);
            q(item.originalTitle);
        }
        pending        = (int)queries.size() + (stremioOn ? 1 : 0);
        pendingJackett = (int)queries.size();
        jackettAsked   = !queries.empty();

        jackett::Category cat =
            item.isSeries() ? jackett::Category::SERIES : jackett::Category::MOVIES;
        for (const auto& q : queries)
        {
            brls::Logger::info("[sources] jackett: {}", q);
            jackett::searchAsync(q, cat, [this, a = alive](jackett::SearchResult r) {
                if (!*a) return;
                bool added = false;
                if (r.ok)
                    for (const auto& it : r.items) added |= addSource(fromJackett(it));
                done(r.ok ? "" : "Jackett: " + r.error, true, added);
            });
        }

        if (stremioOn)
        {
            std::string type = item.stremioType();
            std::string id   = item.imdbId;
            if (item.isSeries() && season > 0 && episode > 0)
                id += ":" + std::to_string(season) + ":" + std::to_string(episode);
            stremio::fetchAddonsAsync(authKey, [this, a = alive, type, id](stremio::AddonsResult r) {
                if (!*a) return;
                std::vector<stremio::Addon> addons;
                if (r.ok)
                    for (const auto& ad : r.addons)
                        if (ad.hasStream && ad.supportsType(type) &&
                            !stremio::isStreamAddonHidden(ad.name))
                            addons.push_back(ad);
                // One pending for the account, swapped for one per addon.
                pending += (int)addons.size();
                for (const auto& ad : addons)
                {
                    std::string name = ad.name;
                    stremio::fetchStreamsAsync(ad.base, type, id,
                                               [this, a, name](stremio::StreamsResult s) {
                        if (!*a) return;
                        bool added = false;
                        if (s.ok)
                            for (const auto& st : s.streams)
                            {
                                Source src = fromStream(st, name);
                                if (!src.src.empty()) added |= addSource(std::move(src));
                            }
                        done(s.ok ? "" : name + ": " + s.error, false, added);
                    });
                }
                done(r.ok ? "" : "Stremio: " + r.error, false, false);
            });
        }
    }

    // ---- the list -------------------------------------------------------------

    std::vector<const Source*> visible() const
    {
        std::vector<const Source*> v;
        for (const auto& r : found)
        {
            if (r.score <= -500) continue;  // another season / episode, a cam
            if (filterHeight && r.info.height != filterHeight) continue;
            if (!facetsPass(r)) continue;
            v.push_back(&r);
        }
        std::stable_sort(v.begin(), v.end(), [this](const Source* a, const Source* b) {
            switch (order)
            {
                case Order::Seeders: return a->seeders > b->seeders;
                case Order::Size:    return a->size > b->size;
                default:             return a->score > b->score;
            }
        });
        return v;
    }

    void rebuild()
    {
        // The focus may sit on a row about to be freed (X pressed on it, or
        // an addon's results folded in under it): park it on the filter row,
        // then hand it to the same release in the new list.
        bool focusInList = false;
        std::string focusKey;
        brls::View* child = nullptr;
        for (brls::View* v = brls::Application::getCurrentFocus(); v; v = v->getParent())
        {
            if (v == list)
            {
                focusInList = true;
                break;
            }
            child = v;
        }
        if (focusInList)
        {
            for (const auto& rk : rowKeys)
                if (rk.first == child) focusKey = rk.second;
            brls::Application::giveFocus(filterBtns[0].first);
        }
        list->clearViews();
        rowKeys.clear();
        if (!listReady()) return;
        shown = true;
        updateSubtitle();

        auto rows = visible();
        if (rows.empty() && pending > 0)
        {
            // Jackett found nothing (yet) that fits; the addons may.
            status->setText(tr("Searching...") + std::string("  ") +
                            std::to_string(found.size()) + tr(" found so far"));
            status->setVisibility(brls::Visibility::VISIBLE);
            return;
        }
        if (rows.empty())
        {
            std::string msg = found.empty() ? tr("No results found")
                                            : tr("Nothing matches this filter.");
            if (found.empty() && !errors.empty()) msg += "\n" + errors[0];
            status->setText(msg);
            status->setVisibility(brls::Visibility::VISIBLE);
            return;
        }
        status->setVisibility(brls::Visibility::GONE);

        // "Best choice": the top of the recommended order, whatever is shown.
        const Source* best = nullptr;
        for (const auto& r : found)
            if (r.score > -500 && (!best || r.score > best->score)) best = &r;

        brls::View* refocus = nullptr;
        for (const Source* r : rows)
        {
            brls::Box* row = buildRow(*r, r == best);
            list->addView(row);
            std::string key = dedupKey(*r);
            if (focusInList && !refocus && key == focusKey) refocus = row;
            rowKeys.push_back({ row, key });
        }
        if (focusInList) brls::Application::giveFocus(refocus ? refocus : rowKeys[0].first);
    }

    brls::Box* buildRow(const Source& s, bool best)
    {
        auto* row = new brls::Box();
        row->setAxis(brls::Axis::ROW);
        row->setAlignItems(brls::AlignItems::CENTER);
        row->setPadding(12.0f, 18.0f, 12.0f, 18.0f);
        row->setMarginBottom(10.0f);
        row->setCornerRadius(10.0f);
        row->setBackgroundColor(theme::scrim(14));
        row->setFocusable(true);
        row->setHighlightCornerRadius(10.0f);

        auto* q = new brls::Label();
        q->setText(qualityText(s.info.height));
        q->setFontSize(24.0f);
        q->setWidth(100.0f);
        q->setSingleLine(true);
        row->addView(q);

        auto* mid = new brls::Box();
        mid->setAxis(brls::Axis::COLUMN);
        mid->setGrow(1.0f);
        mid->setShrink(1.0f);
        mid->setMarginRight(16.0f);
        auto* t = new brls::Label();
        t->setText(s.title);
        t->setFontSize(19.0f);
        t->setSingleLine(true);
        mid->addView(t);

        auto* tags = new brls::Box();
        tags->setAxis(brls::Axis::ROW);
        tags->setMarginTop(8.0f);
        auto tag = [tags](const std::string& text, NVGcolor color) {
            auto* b = new brls::Box();
            b->setCornerRadius(5.0f);
            b->setPadding(4.0f, 8.0f, 2.0f, 8.0f);
            b->setMarginRight(8.0f);
            b->setBorderColor(color);
            b->setBorderThickness(1.0f);
            auto* l = new brls::Label();
            l->setText(text);
            l->setFontSize(14.0f);
            l->setTextColor(color);
            l->setSingleLine(true);
            b->addView(l);
            tags->addView(b);
        };
        if (best) tag(tr("best choice"), nvgRGB(0x9b, 0xe3, 0x8b));
        for (const auto& x : release::tags(s.info)) tag(x, theme::textDim());
        if (item.isSeries() && s.info.fullSeason)
            tag(s.info.seasonTo > s.info.season ? tr("several seasons") : tr("whole season"),
                theme::textDim());
        if (s.info.height == 2160 || s.info.tenBit)
            tag(tr("heavy for Wi-Fi"), theme::textWarn());
        if (!s.tracker.empty()) tag(s.tracker, theme::textMuted());
        mid->addView(tags);
        row->addView(mid);

        auto* right = new brls::Box();
        right->setAxis(brls::Axis::COLUMN);
        right->setAlignItems(brls::AlignItems::FLEX_END);
        right->setWidth(130.0f);
        auto* sz = new brls::Label();
        sz->setText(sizeText(s.size));
        sz->setFontSize(17.0f);
        sz->setHorizontalAlign(brls::HorizontalAlign::RIGHT);
        right->addView(sz);
        if (s.seeders >= 0)
        {
            auto* sd = new brls::Label();
            sd->setText(tr("seeds ") + std::to_string(s.seeders));
            sd->setFontSize(15.0f);
            sd->setTextColor(s.seeders > 0 ? theme::textMuted() : theme::textWarn());
            sd->setHorizontalAlign(brls::HorizontalAlign::RIGHT);
            sd->setMarginTop(6.0f);
            right->addView(sd);
        }
        row->addView(right);

        Source copy = s;
        row->registerClickAction([this, copy](brls::View*) {
            play(copy);
            return true;
        });
        row->addGestureRecognizer(new brls::TapGestureRecognizer(row));
        if (s.kind == Source::Kind::Jackett)
        {
            jackett::Item ji = s.jitem;
            row->registerAction(tr("Save to Local"), brls::BUTTON_Y, [ji](brls::View*) {
                jackett::saveToLocalAsync(ji, [](bool ok, const std::string& msg) {
                    auto* d = new brls::Dialog(ok ? tr("Saved to Local library") : msg);
                    d->addButton("OK", []() {});
                    d->open();
                });
                return true;
            });
        }
        return row;
    }

    // ---- playing --------------------------------------------------------------

    void play(const Source& s)
    {
        if (item.isSeries())
        {
            saveChoice(item.key(), s.info.voices.empty() ? "" : s.info.voices[0], s.info.group);
            // A season pack: the next episode can open straight from it.
            if (season > 0 && s.info.fullSeason && s.kind != Source::Kind::StremioUrl)
            {
                bool jk = s.kind == Source::Kind::Jackett;
                library::rememberPack(item, s.title, jk ? s.jitem.link : "",
                                      jk ? s.jitem.magnetUri : s.src, s.info.season,
                                      s.info.seasonTo);
            }
        }

        // From this screen: the next episode replaces it (pop it and the player).
        PlayHint hint = hintFor(item, season, episode, episodesInSeason, 2);

        switch (s.kind)
        {
            case Source::Kind::Jackett:
                hint.sourceType = "Jackett";
                playJackettItem(s.jitem, hint);
                break;
            case Source::Kind::StremioTorrent:
                hint.sourceType = "Stremio";
                // The addon said which file: no need to open the torrent here.
                if (s.fileIdx >= 0 || !item.isSeries() || episode <= 0)
                    startPlaybackPrompt(s.src, hint.art, hint.title, s.fileIdx, hint.watch,
                                        hint.sourceType);
                else
                    playMagnet(s.src, hint);
                break;
            case Source::Kind::StremioUrl:
                hint.sourceType = "Stremio";
                startPlaybackPrompt(s.src, hint.art, hint.title, -1, hint.watch, hint.sourceType);
                break;
        }
    }
};

} // namespace

void openSourcesScreen(const content::Item& item, int season, int episode,
                       int episodesInSeason)
{
    auto* frame = new brls::AppletFrame(new SourcesView(item, season, episode, episodesInSeason));
    frame->setTitle("");
    brls::Application::pushActivity(new brls::Activity(frame));
}

void openNextEpisode(const content::Item& item, int season, int episode)
{
    library::Entry en;
    bool have = library::find(item.key(), en);
    content::Item it = item;
    if (have && it.imdbId.empty()) it.imdbId = en.item.imdbId;

    // The last episode came from a season pack that holds this one too: open
    // the same torrent and let torrent_play find the episode's file.
    bool packHasIt = have && en.packSeason > 0 && season >= en.packSeason &&
                     season <= en.packSeasonTo &&
                     (!en.packLink.empty() || !en.packMagnet.empty());
    if (packHasIt)
    {
        brls::Logger::info("[sources] next episode S{}E{} from the same pack", season, episode);
        PlayHint hint = hintFor(it, season, episode, 0, 1);
        bool httpLink = !en.packLink.empty() && en.packLink.rfind("magnet:", 0) != 0;
        if (httpLink)
        {
            jackett::Item ji;
            ji.title     = en.packTitle;
            ji.link      = en.packLink;
            ji.magnetUri = en.packMagnet;
            hint.sourceType = "Jackett";
            playJackettItem(ji, hint);
        }
        else
        {
            hint.sourceType = "Jackett";
            playMagnet(en.packMagnet.empty() ? en.packLink : en.packMagnet, hint);
        }
        return;
    }
    openSourcesScreen(it, season, episode, 0);
}

void continueContent(const library::Entry& en)
{
    library::Next n = library::nextOf(en);
    if (n.kind == library::Next::Kind::NextEp)
    {
        openNextEpisode(en.item, n.season, n.episode);
        return;
    }
    if (n.kind == library::Next::Kind::Resume && !en.source.empty())
    {
        // Straight back into what was playing: the player's own prompt offers
        // the saved position (it is keyed on this source).
        PlayHint hint = hintFor(en.item, n.season, n.episode, 0, 1);
        startPlaybackPrompt(en.source, hint.art, hint.title, en.fileIndex, hint.watch,
                            en.sourceType);
        return;
    }
    // Nothing to resume from: the sources for it.
    openSourcesScreen(en.item, n.season, n.episode, 0);
}
