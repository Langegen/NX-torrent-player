#include "autotest.hpp"

#ifdef __SWITCH__

namespace autotest
{
bool init() { return false; }
void tick() {}
} // namespace autotest

#else

#include <borealis.hpp>
#include <glad/glad.h>

#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <map>
#include <sstream>
#include <string>
#include <sys/stat.h>
#include <typeinfo>
#include <vector>

#define STB_IMAGE_WRITE_IMPLEMENTATION
#define STB_IMAGE_WRITE_STATIC
#include "../library/borealis/library/lib/extern/glfw/deps/stb_image_write.h"

#include "card.hpp"
#include "content.hpp"
#include "home.hpp"
#include "player.hpp"
#include "setup.hpp"

namespace autotest
{

namespace
{

using Clock = std::chrono::steady_clock;

std::vector<std::string> lines;
size_t pc = 0;
Clock::time_point resumeAt;
std::string outDir;
std::string pendingShot;  // taken by the frame-end hook
FILE* logf = nullptr;

void say(const std::string& s)
{
    if (!logf) return;
    std::fprintf(logf, "%s\n", s.c_str());
    std::fflush(logf);
}

const std::map<std::string, brls::ControllerButton>& buttons()
{
    static const std::map<std::string, brls::ControllerButton> m = {
        { "A", brls::BUTTON_A },           { "B", brls::BUTTON_B },
        { "X", brls::BUTTON_X },           { "Y", brls::BUTTON_Y },
        { "LB", brls::BUTTON_LB },         { "RB", brls::BUTTON_RB },
        { "LT", brls::BUTTON_LT },         { "RT", brls::BUTTON_RT },
        { "START", brls::BUTTON_START },   { "BACK", brls::BUTTON_BACK },
        { "UP", brls::BUTTON_NAV_UP },     { "DOWN", brls::BUTTON_NAV_DOWN },
        { "LEFT", brls::BUTTON_NAV_LEFT }, { "RIGHT", brls::BUTTON_NAV_RIGHT },
        // The D-pad itself (a real press sends both): the player's seek and
        // other actions bound to the pad rather than to navigation.
        { "DUP", brls::BUTTON_UP },        { "DDOWN", brls::BUTTON_DOWN },
        { "DLEFT", brls::BUTTON_LEFT },    { "DRIGHT", brls::BUTTON_RIGHT },
    };
    return m;
}

// The frame, read back before the swap and written top row first.
void takeShot()
{
    GLint vp[4] = { 0, 0, 0, 0 };
    glGetIntegerv(GL_VIEWPORT, vp);
    int w = vp[2], h = vp[3];
    if (w <= 0 || h <= 0) return;
    std::vector<unsigned char> px((size_t)w * h * 4);
    glPixelStorei(GL_PACK_ALIGNMENT, 1);
    glReadPixels(0, 0, w, h, GL_RGBA, GL_UNSIGNED_BYTE, px.data());
    std::vector<unsigned char> flip((size_t)w * h * 3);
    for (int y = 0; y < h; y++)
        for (int x = 0; x < w; x++)
        {
            const unsigned char* s = &px[((size_t)(h - 1 - y) * w + x) * 4];
            unsigned char* d       = &flip[((size_t)y * w + x) * 3];
            d[0] = s[0];
            d[1] = s[1];
            d[2] = s[2];
        }
    std::string path = outDir + "/" + pendingShot + ".png";
    stbi_write_png(path.c_str(), w, h, 3, flip.data(), w * 3);
    say("shot " + path + " " + std::to_string(w) + "x" + std::to_string(h));
}

std::string viewName(brls::View* v)
{
    std::string n = typeid(*v).name();
    // MinGW mangles: "N4brls5LabelE" -> keep it, it reads well enough; strip
    // the common prefixes.
    return n;
}

void dumpView(std::ostream& o, brls::View* v, int depth, brls::View* focus)
{
    if (!v) return;
    bool gone = v->getVisibility() == brls::Visibility::GONE;
    o << std::string(depth * 2, ' ') << viewName(v);
    char geo[96];
    std::snprintf(geo, sizeof(geo), " [%.0f,%.0f %.0fx%.0f]", v->getX(), v->getY(),
                  v->getWidth(), v->getHeight());
    o << geo;
    if (gone) o << " GONE";
    if (v->getVisibility() == brls::Visibility::INVISIBLE) o << " INVISIBLE";
    if (v->isFocusable()) o << " focusable";
    if (v == focus) o << " <<FOCUS>>";
    if (auto* l = dynamic_cast<brls::Label*>(v))
    {
        std::string t = l->getFullText();
        for (auto& c : t)
            if (c == '\n') c = '|';
        o << " \"" << t << "\"";
    }
    o << "\n";
    if (gone) return;
    if (auto* b = dynamic_cast<brls::Box*>(v))
        for (brls::View* c : b->getChildren()) dumpView(o, c, depth + 1, focus);
}

void dump(const std::string& name)
{
    auto stack = brls::Application::getActivitiesStack();
    std::ofstream o(outDir + "/" + name + ".txt");
    o << "activities: " << stack.size() << "\n";
    brls::View* focus = brls::Application::getCurrentFocus();
    o << "focus: " << (focus ? viewName(focus) : std::string("none"));
    if (focus)
    {
        char geo[96];
        std::snprintf(geo, sizeof(geo), " [%.0f,%.0f %.0fx%.0f]", focus->getX(),
                      focus->getY(), focus->getWidth(), focus->getHeight());
        o << geo;
    }
    o << "\n";
    if (!stack.empty()) dumpView(o, stack.back()->getContentView(), 0, focus);
    say("dump " + name);
}

content::Item itemFor(const std::string& kind, int id)
{
    content::Item it;
    it.kind   = kind == "tv" ? content::Kind::Series : content::Kind::Movie;
    it.tmdbId = id;
    it.title  = "#" + std::to_string(id);
    return it;
}

// One command; false to stop for now (a wait).
bool run(const std::string& line)
{
    std::istringstream in(line);
    std::string cmd;
    in >> cmd;
    if (cmd.empty() || cmd[0] == '#') return true;
    say("> " + line);
    if (cmd == "wait")
    {
        int ms = 0;
        in >> ms;
        resumeAt = Clock::now() + std::chrono::milliseconds(ms);
        return false;
    }
    if (cmd == "press")
    {
        std::string b;
        int n = 1;
        in >> b >> n;
        if (n < 1) n = 1;
        auto it = buttons().find(b);
        if (it == buttons().end())
        {
            say("unknown button " + b);
            return true;
        }
        brls::Application::onControllerButtonPressed(it->second, false);
        // The rest one by one, each after the pause below, as a hand would.
        if (n > 1)
            lines.insert(lines.begin() + (long)pc, "press " + b + " " + std::to_string(n - 1));
        // Let the press land (and any animation start) before the next line.
        resumeAt = Clock::now() + std::chrono::milliseconds(250);
        return false;
    }
    if (cmd == "shot")
    {
        in >> pendingShot;
        return false;  // the hook takes it at the end of the next frame
    }
    if (cmd == "focuson")
    {
        // The first focusable view of the top screen whose own text (its
        // first label, as `focus` prints it) contains the words given.
        std::string want;
        std::getline(in, want);
        while (!want.empty() && want[0] == ' ') want.erase(0, 1);
        auto stack = brls::Application::getActivitiesStack();
        brls::View* hit = nullptr;
        std::vector<brls::View*> todo;
        if (!stack.empty()) todo.push_back(stack.back()->getContentView());
        while (!todo.empty() && !hit)
        {
            brls::View* v = todo.front();
            todo.erase(todo.begin());
            if (!v || v->getVisibility() == brls::Visibility::GONE) continue;
            if (v->isFocusable())
            {
                std::vector<brls::View*> sub = { v };
                while (!sub.empty())
                {
                    brls::View* s = sub.front();
                    sub.erase(sub.begin());
                    if (auto* l = dynamic_cast<brls::Label*>(s))
                        if (!l->getFullText().empty())
                        {
                            if (l->getFullText().find(want) != std::string::npos) hit = v;
                            break;
                        }
                    if (auto* b = dynamic_cast<brls::Box*>(s))
                        for (brls::View* c : b->getChildren()) sub.push_back(c);
                }
                if (hit) break;
            }
            if (auto* b = dynamic_cast<brls::Box*>(v))
                for (brls::View* c : b->getChildren()) todo.push_back(c);
        }
        if (hit)
            brls::Application::giveFocus(hit);
        else
            say("focuson: nothing says \"" + want + "\"");
        resumeAt = Clock::now() + std::chrono::milliseconds(200);
        return false;
    }
    if (cmd == "focus")
    {
        brls::View* f = brls::Application::getCurrentFocus();
        if (!f)
        {
            say("focus: none");
            return true;
        }
        // The first text under it says what it is ("Фильмы", a title).
        std::string text;
        std::vector<brls::View*> todo = { f };
        while (!todo.empty() && text.empty())
        {
            brls::View* v = todo.front();
            todo.erase(todo.begin());
            if (auto* l = dynamic_cast<brls::Label*>(v)) text = l->getFullText();
            if (auto* b = dynamic_cast<brls::Box*>(v))
                for (brls::View* c : b->getChildren()) todo.push_back(c);
        }
        char geo[96];
        std::snprintf(geo, sizeof(geo), " [%.0f,%.0f %.0fx%.0f] ", f->getX(), f->getY(),
                      f->getWidth(), f->getHeight());
        say("focus: " + viewName(f) + geo + "\"" + text + "\"");
        return true;
    }
    if (cmd == "dump")
    {
        std::string n;
        in >> n;
        dump(n);
        return true;
    }
    if (cmd == "card")
    {
        std::string k;
        int id = 0;
        in >> k >> id;
        openContentCard(itemFor(k, id));
        return true;
    }
    if (cmd == "play")
    {
        // A direct link, as an addon's would be: the player without the engine.
        std::string url, title;
        in >> url;
        std::getline(in, title);
        while (!title.empty() && title[0] == ' ') title.erase(0, 1);
        startPlaybackPrompt(url, {}, title.empty() ? url : title);
        return true;
    }
    if (cmd == "setup")
    {
        openJackettSetup();
        return true;
    }
    if (cmd == "search")
    {
        openCatalogSearch();
        return true;
    }
    if (cmd == "log")
    {
        std::string rest;
        std::getline(in, rest);
        say(rest);
        return true;
    }
    if (cmd == "quit")
    {
        say("quit");
        brls::Application::quit();
        pc = lines.size();
        return false;
    }
    say("unknown command " + cmd);
    return true;
}

} // namespace

bool init()
{
    const char* script = std::getenv("NX_AUTOTEST");
    if (!script || !*script) return false;
    const char* out = std::getenv("NX_AUTOTEST_OUT");
    outDir          = out && *out ? out : "./autotest";
    mkdir(outDir.c_str(), 0755);
    logf = std::fopen((outDir + "/run.log").c_str(), "w");

    std::ifstream f(script);
    std::string l;
    while (std::getline(f, l))
    {
        if (!l.empty() && l.back() == '\r') l.pop_back();
        lines.push_back(l);
    }
    say("script " + std::string(script) + ": " + std::to_string(lines.size()) + " lines");
    resumeAt = Clock::now();

    brls::Application::setFrameEndHook([]() {
        if (pendingShot.empty()) return;
        takeShot();
        pendingShot.clear();
    });
    return true;
}

void tick()
{
    if (pc >= lines.size() || !pendingShot.empty()) return;
    if (Clock::now() < resumeAt) return;
    while (pc < lines.size())
        if (!run(lines[pc++])) break;
}

} // namespace autotest

#endif
