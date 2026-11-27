// Offline unit tests for app/online/* (URL parsing, BWA/Lampac JSON, NX Online Plugin v1,
// stable online:// source URI round-trip, quality picking).

#include <cassert>
#include <cstdio>
#include <string>

#include "online/lampac_provider.hpp"
#include "online/nx_plugin_provider.hpp"
#include "online/online_types.hpp"

#define CHECK(cond)                                                          \
    do {                                                                     \
        if (!(cond)) {                                                       \
            std::fprintf(stderr, "FAILED %s:%d: %s\n", __FILE__, __LINE__,   \
                         #cond);                                             \
            return 1;                                                        \
        }                                                                    \
    } while (0)

int main()
{
    // 1. parseLampaPluginUrl variants
    {
        auto p1 = online::parseLampaPluginUrl("https://app.bwa.ad/online.js");
        CHECK(p1.baseUrl == "https://app.bwa.ad");
        CHECK(p1.token.empty());
        CHECK(p1.looksLikeJsScript);

        auto p2 = online::parseLampaPluginUrl("http://192.168.1.50:9118/online/js/tok_abc123");
        CHECK(p2.baseUrl == "http://192.168.1.50:9118");
        CHECK(p2.token == "tok_abc123");
        CHECK(p2.looksLikeJsScript);

        auto p3 = online::parseLampaPluginUrl("https://my.lampac.org/lite.js?token=xyz789");
        CHECK(p3.baseUrl == "https://my.lampac.org");
        CHECK(p3.token == "xyz789");
        CHECK(p3.looksLikeJsScript);

        auto p4 = online::parseLampaPluginUrl("192.168.1.10:9118/");
        CHECK(p4.baseUrl == "http://192.168.1.10:9118");
        CHECK(!p4.looksLikeJsScript);

        auto p5 = online::parseLampaPluginUrl("https://nb557.github.io/plugins/online_mod.js");
        CHECK(p5.baseUrl == "https://nb557.github.io/plugins");
        CHECK(p5.looksLikeJsScript);
    }

    // 2. BWA /lite/events parsing (real fixture from app.bwa.ad)
    {
        const std::string eventsJson = R"([
            {"balanser":"videodb","name":"VideoDB","uhd":true,"url":"/lite/videodb"},
            {"balanser":"hidden","name":"Hidden","show":false,"url":"/lite/hidden"},
            {"account_only":true,"balanser":"kinopub","bound":false,"name":"KinoPub","url":"/lite/kinopub"},
            {"account_only":false,"avoid":true,"balanser":"rezka","bound":false,"name":"Rezka","url":"/lite/rezka"},
            {"balanser":"torrserver","name":"TorrServer","url":"/lite/torrserver"}
        ])";

        auto bals = online::parseLampacEventsJson(eventsJson, "bwa", "https://app.bwa.ad", "");
        CHECK(bals.size() == 2);
        CHECK(bals[0].id == "videodb");
        CHECK(bals[0].url == "https://app.bwa.ad/lite/videodb?rjson=true");
        CHECK(bals[1].id == "rezka");
        CHECK(bals[1].avoid);

        // With token, account_only balancer is included
        auto balsTok = online::parseLampacEventsJson(eventsJson, "bwa", "https://app.bwa.ad", "t1");
        CHECK(balsTok.size() == 3);
    }

    // 3. BWA Format A page parsing (streams[] + src_seasons)
    {
        const std::string bwaSeriesJson = R"({
            "episodes": null,
            "src_seasons": [[1, 25], [2, 12]],
            "streams": [
                {"kind":"hls","quality":"720p","url":"https://cdn.bwa.ad/dub_720.m3u8","voice":"LostFilm"},
                {"kind":"hls","quality":"1080p","url":"https://cdn.bwa.ad/dub_1080.m3u8","voice":"LostFilm"},
                {"kind":"hls","quality":"480p","url":"https://cdn.bwa.ad/orig_480.m3u8","voice":""}
            ],
            "title": "Breaking Bad"
        })";

        auto pr = online::parseLampacPageJson(bwaSeriesJson, "https://app.bwa.ad", "", 1, 3, true);
        CHECK(pr.ok);
        CHECK(pr.perEpisodeMode);
        CHECK(pr.type == online::PageType::Episodes);
        CHECK(pr.seasons.size() == 2);
        CHECK(pr.seasons[0].number == 1);
        CHECK(pr.seasons[0].episodeCount == 25);
        CHECK(pr.entries.size() == 2);
        CHECK(pr.entries[0].voiceName == "LostFilm");
        CHECK(pr.entries[0].qualities.size() == 2);
        CHECK(pr.entries[0].qualities[0].label == "1080p");
        CHECK(pr.entries[0].url == "https://cdn.bwa.ad/dub_1080.m3u8");
        CHECK(pr.entries[1].voiceName == "Original");

        // BWA call resolve matching voice
        online::OnlineEntry callHint;
        callHint.voiceName = "LostFilm";
        callHint.season    = 1;
        callHint.episode   = 4;
        CHECK(online::parseLampacCallJson(bwaSeriesJson, "https://app.bwa.ad", "", callHint));
        CHECK(callHint.method == online::OnlineMethod::Play);
        CHECK(callHint.url == "https://cdn.bwa.ad/dub_1080.m3u8");
    }

    // 4. Classic Lampac Format B (type: episode, voice, data, quality map, headers, subtitles)
    {
        const std::string lampacJson = R"({
            "type": "episode",
            "voice": [
                {"name": "HDrezka Studio", "url": "/lite/rezka?t=1", "active": true},
                {"name": "Original", "url": "/lite/rezka?t=2", "active": false}
            ],
            "data": [
                {
                    "method": "call",
                    "url": "/lite/rezka/play?s=1&e=2",
                    "s": 1,
                    "e": 2,
                    "title": "Cat's in the Bag...",
                    "maxquality": "1080p",
                    "quality": {
                        "720p": "https://cdn.example/720.m3u8",
                        "1080p": "https://cdn.example/1080.m3u8"
                    },
                    "subtitles": [
                        {"label": "Russian", "lang": "ru", "url": "https://cdn.example/ru.vtt"}
                    ],
                    "headers": {
                        "Referer": "https://rezka.example/",
                        "User-Agent": "TestUA"
                    }
                }
            ]
        })";

        auto pr = online::parseLampacPageJson(lampacJson, "http://192.168.1.10:9118", "secret", 1, 2, true);
        CHECK(pr.ok);
        CHECK(!pr.perEpisodeMode);
        CHECK(pr.type == online::PageType::Episodes);
        CHECK(pr.voices.size() == 2);
        CHECK(pr.voices[0].active);
        CHECK(pr.voices[1].url == "http://192.168.1.10:9118/lite/rezka?t=2&rjson=true&token=secret");
        CHECK(pr.entries.size() == 1);
        const auto& e = pr.entries[0];
        CHECK(e.method == online::OnlineMethod::Call);
        CHECK(e.season == 1 && e.episode == 2);
        CHECK(e.voiceName == "HDrezka Studio");
        CHECK(e.qualities.size() == 2);
        CHECK(e.qualities[0].label == "1080p");
        CHECK(e.subtitles.size() == 1 && e.subtitles[0].lang == "ru");
        CHECK(e.headers.size() == 2);
    }

    // 5. NX Online Plugin v1 manifest & streams
    {
        const std::string manifest = R"({
            "schema": "nx-online/v1",
            "id": "custom-1",
            "name": "My Worker",
            "version": "1.0.0",
            "balancers": [{"id": "demo", "name": "Demo HLS"}],
            "endpoints": {"streams": "/streams", "resolve": "/resolve"}
        })";
        auto m = online::parseNxManifestJson(manifest);
        CHECK(m.valid);
        CHECK(m.name == "My Worker");
        CHECK(m.balancers.size() == 1);
        CHECK(m.balancers[0].id == "demo");

        const std::string streams = R"({
            "type": "movie",
            "items": [{
                "title": "Big Buck Bunny",
                "voice": "Original",
                "method": "play",
                "url": "https://test-streams.mux.dev/x36xhzz/x36xhzz.m3u8",
                "qualities": {
                    "2160p": "https://cdn.example/4k.m3u8",
                    "1080p": "https://cdn.example/1080.m3u8",
                    "720p": "https://cdn.example/720.m3u8"
                }
            }]
        })";
        auto pr = online::parseNxStreamsJson(streams, "https://worker.example");
        CHECK(pr.ok);
        CHECK(pr.type == online::PageType::Movie);
        CHECK(pr.entries.size() == 1);

        // Quality picking respects hide4k and preferHeight
        std::string qLbl;
        std::string u1 = online::pickBestQuality(pr.entries[0], 0, true, &qLbl);
        CHECK(qLbl == "1080p");
        CHECK(u1 == "https://cdn.example/1080.m3u8");

        std::string u2 = online::pickBestQuality(pr.entries[0], 720, false, &qLbl);
        CHECK(qLbl == "720p");
        CHECK(u2 == "https://cdn.example/720.m3u8");
    }

    // 6. online:// URI round-trip with Cyrillic & spaces
    {
        online::OnlineRef in{ "bwa_1", "videodb", "(RU) Кубик в Кубе" };
        std::string uri = online::makeOnlineSourceUri(in);
        online::OnlineRef out;
        CHECK(online::parseOnlineSourceUri(uri, out));
        CHECK(out.pluginId == in.pluginId);
        CHECK(out.balancerId == in.balancerId);
        CHECK(out.voiceName == in.voiceName);
    }

    std::printf("test_online: ALL PASSED\n");
    return 0;
}
