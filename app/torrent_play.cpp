#include "torrent_play.hpp"

#include <borealis.hpp>

#include <algorithm>
#include <cstdlib>
#include <cstring>
#include <memory>
#include <strings.h>
#include <vector>

#include "browse.hpp"   // ListActivity, Row, naturalCompare, pickerNames
#include "i18n.hpp"
#include "release.hpp"  // matchEpisode

extern "C" {
#include "torrent.h"
}

namespace
{

struct VideoFileInfo
{
    int index;
    std::string name;  // picker label (folder added when names repeat)
    std::string path;  // the path inside the torrent
    int64_t length;
};

bool isVideoExtension(const std::string& path)
{
    static const char* kExt[] = { ".mkv", ".mp4", ".avi", ".m4v",  ".mov",
                                  ".webm", ".ts", ".m2ts", ".mpg", ".mpeg",
                                  ".wmv", ".flv", ".ogv" };
    for (const char* e : kExt)
    {
        size_t n = std::strlen(e);
        if (path.size() > n &&
            strcasecmp(path.c_str() + path.size() - n, e) == 0)
            return true;
    }
    return false;
}

// The video files of a loaded torrent, sorted naturally by their labels.
std::vector<VideoFileInfo> videosOf(const torrent_meta* m)
{
    std::vector<int> idx;
    std::vector<std::string> paths;
    for (int i = 0; i < m->file_count; i++)
    {
        if (!isVideoExtension(m->files[i].path)) continue;
        idx.push_back(i);
        paths.push_back(m->files[i].path);
    }
    std::vector<std::string> names = pickerNames(paths);
    std::vector<VideoFileInfo> out;
    for (size_t k = 0; k < idx.size(); k++)
        out.push_back({ idx[k], names[k], paths[k], m->files[idx[k]].length });
    std::sort(out.begin(), out.end(), [](const VideoFileInfo& a, const VideoFileInfo& b) {
        return naturalCompare(a.name, b.name);
    });
    return out;
}

std::vector<VideoFileInfo> parseTorrentVideos(const std::string& path)
{
    std::vector<VideoFileInfo> out;
    torrent_meta* m = (torrent_meta*)std::calloc(1, sizeof(torrent_meta));
    if (!m) return out;
    char err[128] = { 0 };
    if (torrent_load(m, path.c_str(), err, sizeof(err)) == 0)
    {
        out = videosOf(m);
        torrent_unload(m);
    }
    std::free(m);
    return out;
}

// The loading dialog's body. Its destruction (the dialog closed by B or by
// Cancel) marks the job cancelled, so a late result is dropped.
class LoadingBox : public brls::Box
{
  public:
    std::shared_ptr<bool> cancelled;
    explicit LoadingBox(std::shared_ptr<bool> c) : cancelled(std::move(c)) {}
    ~LoadingBox() override
    {
        if (cancelled) *cancelled = true;
    }
};

struct Job
{
    PlayHint hint;
    brls::Dialog* loading = nullptr;
    LoadingBox* box       = nullptr;
    std::shared_ptr<bool> cancelled;
};

void errorDialog(const std::string& msg)
{
    auto* d = new brls::Dialog(msg);
    d->addButton("OK", []() {});
    d->open();
}

std::shared_ptr<Job> openJob(const PlayHint& hint)
{
    auto job       = std::make_shared<Job>();
    job->hint      = hint;
    job->cancelled = std::make_shared<bool>(false);

    job->box = new LoadingBox(job->cancelled);
    job->box->setAxis(brls::Axis::COLUMN);
    job->box->setAlignItems(brls::AlignItems::CENTER);
    job->box->setJustifyContent(brls::JustifyContent::CENTER);
    job->box->setPadding(24.0f);
    job->box->addView(new brls::ProgressSpinner());
    auto* lbl = new brls::Label();
    lbl->setText(tr("Preparing torrent..."));
    lbl->setFontSize(20.0f);
    lbl->setMarginTop(20.0f);
    job->box->addView(lbl);

    job->loading = new brls::Dialog(job->box);
    auto cancelled = job->cancelled;
    job->loading->addButton(tr("Cancel"), [cancelled]() { *cancelled = true; });
    job->loading->setCancelable(true);
    job->loading->open();
    return job;
}

// Closes the dialog, then runs `then` (dialogs animate out before the next
// screen can be pushed).
void finish(const std::shared_ptr<Job>& job, std::function<void()> then)
{
    job->box->cancelled = nullptr;  // closing is not cancelling
    job->loading->close(std::move(then));
}

void start(const PlayHint& h, const std::string& source, const VideoFileInfo& v)
{
    WatchInfo w = h.watch;
    if (w.displayTitle.empty()) w.displayTitle = h.title;
    startPlaybackPrompt(source, h.art, h.title.empty() ? v.name : h.title, v.index, w,
                        h.sourceType);
}

void playWithVideos(const PlayHint& h, const std::string& source,
                    const std::vector<VideoFileInfo>& vids)
{
    if (vids.empty())
    {
        errorDialog(tr("This torrent has no video file to stream."));
        return;
    }
    if (vids.size() == 1)
    {
        start(h, source, vids[0]);
        return;
    }
    // A season pack opened for one episode: find its file.
    if (h.season > 0 && h.episode > 0)
    {
        std::vector<std::string> paths;
        for (const auto& v : vids) paths.push_back(v.path);
        int i = release::matchEpisode(paths, h.season, h.episode);
        if (i >= 0)
        {
            brls::Logger::info("[play] episode S{}E{} -> file {}", h.season, h.episode,
                               vids[i].index);
            start(h, source, vids[i]);
            return;
        }
    }

    std::vector<Row> rows;
    for (const auto& v : vids)
    {
        Row r;
        r.label  = v.name;
        r.detail = jackett::formatSize(v.length);
        rows.push_back(r);
    }
    PlayHint hint = h;
    brls::Application::pushActivity(new ListActivity(
        h.title, tr("Pick a file"), rows, [hint, source, vids](int i) {
            // A file picked by hand: its own name is the better title.
            PlayHint picked = hint;
            if (hint.episode <= 0) picked.title = vids[i].name;
            start(picked, source, vids[i]);
        }));
}

void resolveMagnet(const std::shared_ptr<Job>& job, const std::string& magnet)
{
    brls::async([job, magnet]() {
        torrent_meta* m = (torrent_meta*)std::calloc(1, sizeof(torrent_meta));
        char err[128] = { 0 };
        int rc = -1;
        std::vector<VideoFileInfo> vids;
        if (m)
        {
            rc = torrent_load_magnet(m, magnet.c_str(), err, sizeof(err));
            if (rc == 0)
            {
                vids = videosOf(m);
                torrent_unload(m);
            }
            std::free(m);
        }
        std::string e = err;
        brls::sync([job, magnet, vids, rc, e]() {
            if (*job->cancelled) return;
            finish(job, [job, magnet, vids, rc, e]() {
                if (rc != 0)
                {
                    // The engine's reasons are in the table too (i18n_ru.cpp).
                    errorDialog(tr("Could not resolve torrent metadata: ") +
                                std::string(tr(e.c_str())));
                    return;
                }
                playWithVideos(job->hint, magnet, vids);
            });
        });
    });
}

} // namespace

void playMagnet(const std::string& magnet, const PlayHint& hint)
{
    resolveMagnet(openJob(hint), magnet);
}

void playJackettItem(const jackett::Item& item, const PlayHint& hint)
{
    std::string link   = item.link;
    std::string magnet = (!link.empty() && link.rfind("magnet:", 0) == 0) ? link
                                                                          : item.magnetUri;
    bool isHttp = !link.empty() && link.rfind("magnet:", 0) != 0;

    if (!isHttp && magnet.empty())
    {
        errorDialog(tr("No playable link or magnet URI found for this torrent."));
        return;
    }

    PlayHint h = hint;
    if (h.title.empty()) h.title = item.title;
    auto job = openJob(h);
    if (!isHttp)
    {
        resolveMagnet(job, magnet);
        return;
    }

    // One file per link: history entries resume from this path later, so it
    // must not be overwritten by the next result played.
    std::string tempPath = jackett::cachedTorrentPath(link);
    jackett::downloadTorrentAsync(link, tempPath, [job, tempPath, magnet](
                                                      bool ok, const std::string& err,
                                                      const std::string& redirected) {
        if (*job->cancelled) return;
        // The indexer pointed at a magnet instead of a file (common for public
        // trackers): resolve that, it is the same torrent.
        if (ok && !redirected.empty())
        {
            resolveMagnet(job, redirected);
            return;
        }
        if (ok)
        {
            auto vids = parseTorrentVideos(tempPath);
            if (!vids.empty())
            {
                finish(job, [job, tempPath, vids]() {
                    playWithVideos(job->hint, tempPath, vids);
                });
                return;
            }
        }
        // The .torrent failed or held no video: the magnet, if there is one.
        if (!magnet.empty())
        {
            resolveMagnet(job, magnet);
            return;
        }
        finish(job, [err, ok]() {
            errorDialog(ok ? tr("This torrent has no video file to stream.")
                           : tr("Could not download torrent: ") + err);
        });
    });
}
