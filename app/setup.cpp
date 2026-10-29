#include "setup.hpp"

#include <borealis.hpp>

#include <memory>

#include "config.hpp"
#include "i18n.hpp"
#include "jackett.hpp"
#include "theme.hpp"
#include "widgets.hpp"

namespace
{

std::string maskKey(const std::string& k)
{
    if (k.empty()) return tr("None");
    if (k.size() <= 8) return "********";
    return k.substr(0, 4) + "..." + k.substr(k.size() - 4);
}

class JackettSetupView : public brls::Box
{
  public:
    JackettSetupView()
    {
        setAxis(brls::Axis::COLUMN);
        setGrow(1.0f);
        setPadding(44.0f, 80.0f, 20.0f, 80.0f);

        auto* title = new brls::Label();
        title->setText(tr("Set up Jackett"));
        title->setFontSize(30.0f);
        addView(title);

        auto* intro = new brls::Label();
        intro->setText(tr("The app finds releases through Jackett: a server on your computer "
                          "or NAS that searches the torrent trackers. Enter its address and "
                          "API key, then check the connection."));
        auto* jacred = new brls::Label();
        jacred->setText(tr("A public server with the same API (Jacred) works too: enter its "
                           "address and leave the key empty."));
        jacred->setFontSize(16.0f);
        jacred->setLineHeight(1.4f);
        jacred->setTextColor(theme::textMuted());
        jacred->setMarginBottom(14.0f);
        intro->setFontSize(18.0f);
        intro->setLineHeight(1.4f);
        intro->setTextColor(theme::textMuted());
        intro->setMargins(12.0f, 0.0f, 8.0f, 0.0f);
        addView(intro);
        addView(jacred);

        const config::Config& cfg = config::get();

        urlCell = new brls::DetailCell();
        urlCell->setText(tr("Server URL"));
        urlCell->setDetailText(cfg.jackettUrl.empty() ? tr("Not set") : cfg.jackettUrl);
        urlCell->registerClickAction([this](brls::View*) {
            brls::Application::getImeManager()->openForText(
                [this](std::string out) {
                    out = jackett::normalizeUrl(out);
                    config::get().jackettUrl = out;
                    config::save();
                    urlCell->setDetailText(out.empty() ? tr("Not set") : out);
                    resetCheck();
                },
                tr("Jackett Server URL"), tr("e.g. http://192.168.1.100:9117"), 256,
                config::get().jackettUrl);
            return true;
        });
        addView(urlCell);

        keyCell = new brls::DetailCell();
        keyCell->setText(tr("API Key"));
        keyCell->setDetailText(maskKey(cfg.jackettApiKey));
        keyCell->registerClickAction([this](brls::View*) {
            brls::Application::getImeManager()->openForText(
                [this](std::string out) {
                    // Pasted keys pick up spaces at either end.
                    while (!out.empty() && out.back() == ' ') out.pop_back();
                    while (!out.empty() && out.front() == ' ') out.erase(0, 1);
                    config::get().jackettApiKey = out;
                    config::save();
                    keyCell->setDetailText(maskKey(out));
                    resetCheck();
                },
                tr("Jackett API Key (optional)"), tr("Leave empty if disabled in Jackett"),
                128, config::get().jackettApiKey);
            return true;
        });
        addView(keyCell);

        auto* hint = new brls::Label();
        hint->setText(tr("The API Key shown in the top right corner of the Jackett web "
                         "interface (leave empty if disabled)."));
        hint->setFontSize(15.0f);
        hint->setTextColor(theme::textMuted());
        hint->setMargins(10.0f, 20.0f, 0.0f, 20.0f);
        addView(hint);

        status = new brls::Label();
        status->setFontSize(18.0f);
        status->setLineHeight(1.4f);
        status->setMargins(20.0f, 20.0f, 0.0f, 20.0f);
        status->setVisibility(brls::Visibility::GONE);
        addView(status);

        auto* buttons = new brls::Box();
        buttons->setAxis(brls::Axis::ROW);
        buttons->setMarginTop(24.0f);
        checkBtn = ui::actionButton(tr("Check and save"), true, [this]() { onCheck(); });
        buttons->addView(checkBtn);
        skipBtn = ui::actionButton(tr("Skip"), false, []() {
            brls::Application::popActivity();
        });
        buttons->addView(skipBtn);
        addView(buttons);

        auto* later = new brls::Label();
        later->setText(tr("You can change this later in Options, Jackett."));
        later->setFontSize(15.0f);
        later->setTextColor(theme::textMuted());
        later->setMargins(14.0f, 4.0f, 0.0f, 0.0f);
        addView(later);
    }

    ~JackettSetupView() override { *alive = false; }

  private:
    std::shared_ptr<bool> alive = std::make_shared<bool>(true);
    brls::DetailCell* urlCell = nullptr;
    brls::DetailCell* keyCell = nullptr;
    brls::Label* status       = nullptr;
    brls::Button* checkBtn    = nullptr;
    brls::Button* skipBtn     = nullptr;
    bool checking = false;
    bool working  = false;  // the last check passed: the button now closes

    void say(const std::string& text, NVGcolor color)
    {
        status->setText(text);
        status->setTextColor(color);
        status->setVisibility(brls::Visibility::VISIBLE);
    }

    // The address or the key changed: the last result no longer holds.
    void resetCheck()
    {
        working = false;
        checkBtn->setText(tr("Check and save"));
        skipBtn->setVisibility(brls::Visibility::VISIBLE);
        status->setVisibility(brls::Visibility::GONE);
    }

    void onCheck()
    {
        if (working)
        {
            brls::Application::popActivity();
            return;
        }
        if (checking) return;
        const config::Config& cfg = config::get();
        if (cfg.jackettUrl.empty())
        {
            say(tr("Please set Server URL first."), theme::textWarn());
            return;
        }
        checking = true;
        checkBtn->setText(tr("Testing..."));
        say(tr("Asking Jackett and its indexers..."), theme::textMuted());
        auto live = alive;
        jackett::testConnectionAsync(cfg.jackettUrl, cfg.jackettApiKey,
                                     [this, live](bool ok, const std::string& msg) {
            if (!*live) return;
            checking = false;
            working  = ok;
            checkBtn->setText(ok ? tr("Done") : tr("Check and save"));
            // Nothing left to skip once it works.
            skipBtn->setVisibility(ok ? brls::Visibility::GONE : brls::Visibility::VISIBLE);
            say(msg, ok ? theme::textGood() : theme::textWarn());
        });
    }
};

class JackettSetupActivity : public brls::Activity
{
  public:
    brls::View* createContentView() override
    {
        auto* frame = new ui::FieldFrame(new JackettSetupView());
        frame->setTitle("Jackett");
        // "Set up Jackett" heads the view: no second title above it.
        frame->setHeaderVisibility(brls::Visibility::GONE);
        return frame;
    }
};

} // namespace

void openJackettSetup()
{
    brls::Application::pushActivity(new JackettSetupActivity());
}

void maybeOfferJackettSetup()
{
    config::Config& cfg = config::get();
    if (cfg.jackettSetupAsked || !cfg.jackettUrl.empty()) return;
    // Once: skipping it (or B) is an answer too.
    cfg.jackettSetupAsked = true;
    config::save();
    openJackettSetup();
}
