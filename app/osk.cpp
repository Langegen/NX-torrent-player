#include "osk.hpp"

#include <borealis.hpp>
#include <borealis/views/button.hpp>
#include <borealis/views/label.hpp>

#include <algorithm>
#include <cmath>
#include <functional>
#include <string>
#include <vector>

#include "config.hpp"
#include "i18n.hpp"
#include "theme.hpp"
#include "widgets.hpp"

namespace
{

size_t utf8Count(const std::string& s)
{
    size_t n = 0;
    for (unsigned char c : s)
        if ((c & 0xC0u) != 0x80u) n++;
    return n;
}

void utf8PopBack(std::string& s)
{
    while (!s.empty() && ((unsigned char)s.back() & 0xC0u) == 0x80u)
        s.pop_back();
    if (!s.empty())
        s.pop_back();
}

std::string utf8Truncate(const std::string& s, size_t maxCp)
{
    if (maxCp == 0) return "";
    size_t cp = 0;
    for (size_t i = 0; i < s.size(); i++)
    {
        if (((unsigned char)s[i] & 0xC0u) != 0x80u)
        {
            if (cp == maxCp) return s.substr(0, i);
            cp++;
        }
    }
    return s;
}

bool isKeyDisabled(const std::string& ch, int mask)
{
    if (ch.empty() || mask == brls::KEYBOARD_DISABLE_NONE) return false;
    if ((mask & brls::KEYBOARD_DISABLE_SPACE) && ch == " ") return true;
    if ((mask & brls::KEYBOARD_DISABLE_AT) && ch == "@") return true;
    if ((mask & brls::KEYBOARD_DISABLE_PERCENT) && ch == "%") return true;
    if ((mask & brls::KEYBOARD_DISABLE_FORWSLASH) && ch == "/") return true;
    if ((mask & brls::KEYBOARD_DISABLE_BACKSLASH) && ch == "\\") return true;
    if ((mask & brls::KEYBOARD_DISABLE_NUMBERS) && ch.size() == 1 && ch[0] >= '0' && ch[0] <= '9')
        return true;
    return false;
}

struct KeyPair
{
    const char* normal;
    const char* shifted;
};

static const std::vector<std::vector<KeyPair>> kEnRows = {
    { {"1","!"}, {"2","@"}, {"3","#"}, {"4","$"}, {"5","%"}, {"6","^"}, {"7","&"}, {"8","*"}, {"9","("}, {"0",")"} },
    { {"q","Q"}, {"w","W"}, {"e","E"}, {"r","R"}, {"t","T"}, {"y","Y"}, {"u","U"}, {"i","I"}, {"o","O"}, {"p","P"} },
    { {"a","A"}, {"s","S"}, {"d","D"}, {"f","F"}, {"g","G"}, {"h","H"}, {"j","J"}, {"k","K"}, {"l","L"}, {"-","_"} },
    { {"z","Z"}, {"x","X"}, {"c","C"}, {"v","V"}, {"b","B"}, {"n","N"}, {"m","M"}, {".",":"}, {"/","?"}, {",",";"} },
};

static const std::vector<std::vector<KeyPair>> kRuRows = {
    { {"1","!"}, {"2","\""}, {"3","№"}, {"4",";"}, {"5","%"}, {"6",":"}, {"7","?"}, {"8","*"}, {"9","("}, {"0",")"}, {"-","_"}, {"+","="} },
    { {"й","Й"}, {"ц","Ц"}, {"у","У"}, {"к","К"}, {"е","Е"}, {"н","Н"}, {"г","Г"}, {"ш","Ш"}, {"щ","Щ"}, {"з","З"}, {"х","Х"}, {"ъ","Ъ"} },
    { {"ф","Ф"}, {"ы","Ы"}, {"в","В"}, {"а","А"}, {"п","П"}, {"р","Р"}, {"о","О"}, {"л","Л"}, {"д","Д"}, {"ж","Ж"}, {"э","Э"} },
    { {"я","Я"}, {"ч","Ч"}, {"с","С"}, {"м","М"}, {"и","И"}, {"т","Т"}, {"ь","Ь"}, {"б","Б"}, {"ю","Ю"}, {"ё","Ё"}, {".",","} },
};

static const std::vector<std::vector<KeyPair>> kSymRows = {
    { {"1","~"}, {"2","@"}, {"3","#"}, {"4","$"}, {"5","%"}, {"6","^"}, {"7","&"}, {"8","*"}, {"9","("}, {"0",")"} },
    { {"@","!"}, {"#","?"}, {"$","€"}, {"%","£"}, {"&","¥"}, {"*","°"}, {"-","_"}, {"+","="}, {"(","["}, {")","]"} },
    { {":",";"}, {"/","\\"}, {"?","¿"}, {"!","¡"}, {"=","+"}, {"_","-"}, {".",","}, {",","."}, {";","\""}, {"'","`"} },
    { {"\"","'"}, {"\\","/"}, {"|","¦"}, {"<","{"}, {">","}"}, {"[","<"}, {"]",">"}, {"{","("}, {"}",")"}, {"~","§"} },
};

class OskActivity : public brls::Activity
{
  public:
    explicit OskActivity(brls::View* v) : brls::Activity(v) {}
    bool isTranslucent() override { return true; }
};

class OskDialog : public brls::Box
{
  public:
    enum class Mode { Text, Number };
    enum class Layout { En, Ru, Sym };

    OskDialog(Mode mode,
              std::string header,
              std::string subText,
              int maxLen,
              std::string initial,
              int disableMask,
              std::string leftOpt,
              std::string rightOpt,
              std::function<void(std::string)> submitCb)
        : mode(mode)
        , placeholder(std::move(subText))
        , maxLen(maxLen > 0 ? maxLen : (mode == Mode::Number ? 18 : 128))
        , text(std::move(initial))
        , disableMask(disableMask)
        , leftOpt(std::move(leftOpt))
        , rightOpt(std::move(rightOpt))
        , submitCb(std::move(submitCb))
    {
        if (utf8Count(text) > (size_t)this->maxLen)
            text = utf8Truncate(text, (size_t)this->maxLen);

        layout = (i18n::lang() == "ru") ? Layout::Ru : Layout::En;

        setWidth(brls::View::AUTO);
        setHeight(brls::View::AUTO);
        setGrow(1.0f);
        setAxis(brls::Axis::COLUMN);
        setJustifyContent(brls::JustifyContent::CENTER);
        setAlignItems(brls::AlignItems::CENTER);
        setBackgroundColor(nvgRGBA(0, 0, 0, 180));

        float cw = brls::Application::contentWidth > 0 ? brls::Application::contentWidth : 1280.0f;
        panelW = (mode == Mode::Number)
                     ? std::min(560.0f, cw - 60.0f)
                     : std::min(1060.0f, cw - 60.0f);

        auto* panel = new brls::Box();
        panel->setWidth(panelW);
        panel->setAxis(brls::Axis::COLUMN);
        panel->setCornerRadius(14.0f);
        panel->setPadding(20.0f, 22.0f, 18.0f, 22.0f);
        panel->setBackgroundColor(theme::surface());
        addView(panel);

        // Header + character counter
        auto* headRow = new brls::Box();
        headRow->setAxis(brls::Axis::ROW);
        headRow->setAlignItems(brls::AlignItems::CENTER);
        headRow->setJustifyContent(brls::JustifyContent::SPACE_BETWEEN);
        headRow->setMarginBottom(10.0f);

        auto* hdrLbl = new brls::Label();
        hdrLbl->setText(header.empty() ? placeholder : header);
        hdrLbl->setFontSize(20.0f);
        hdrLbl->setTextColor(theme::text());
        hdrLbl->setSingleLine(true);
        hdrLbl->setGrow(1.0f);
        hdrLbl->setMarginRight(12.0f);
        headRow->addView(hdrLbl);

        countLbl = new brls::Label();
        countLbl->setFontSize(16.0f);
        countLbl->setTextColor(theme::textMuted());
        countLbl->setSingleLine(true);
        headRow->addView(countLbl);
        panel->addView(headRow);

        // Input preview box
        auto* inputBox = new brls::Box();
        inputBox->setCornerRadius(8.0f);
        inputBox->setPadding(12.0f, 16.0f, 12.0f, 16.0f);
        inputBox->setMarginBottom(14.0f);
        inputBox->setBackgroundColor(theme::surfaceSunken());
        inputLbl = new brls::Label();
        inputLbl->setFontSize(22.0f);
        inputLbl->setSingleLine(false);
        inputLbl->setAutoAnimate(false);
        inputLbl->setMinHeight(30.0f);
        inputLbl->setGrow(1.0f);
        inputBox->addView(inputLbl);
        panel->addView(inputBox);

        keysBox = new brls::Box();
        keysBox->setAxis(brls::Axis::COLUMN);
        keysBox->setAlignItems(brls::AlignItems::CENTER);
        panel->addView(keysBox);

        // Persistent bottom action row (never destroyed across Shift/layout switches)
        auto* actions = new ui::NearestRow();
        actions->setAxis(brls::Axis::ROW);
        actions->setJustifyContent(brls::JustifyContent::CENTER);
        actions->setAlignItems(brls::AlignItems::CENTER);
        actions->setMarginTop(10.0f);

        auto addActionBtn = [actions](const std::string& label, bool primary,
                                      float minW, std::function<void()> fn) {
            auto* b = new brls::Button();
            b->setShrink(0.0f);
            b->setMinWidth(minW);
            b->setHeight(46.0f);
            b->setText(label);
            if (auto* l = dynamic_cast<brls::Label*>(b->getView("brls/button/label")))
                l->setSingleLine(true);
            b->setStyle(primary ? &brls::BUTTONSTYLE_PRIMARY : &brls::BUTTONSTYLE_BORDERLESS);
            if (!primary)
                b->setBackgroundColor(theme::scrim(28));
            b->setFontSize(18.0f);
            b->setPadding(6.0f, 16.0f, 6.0f, 16.0f);
            b->setCornerRadius(8.0f);
            b->setHighlightCornerRadius(8.0f +
                brls::Application::getStyle()["brls/highlight/stroke_width"] / 2.0f);
            b->setMarginLeft(4.0f);
            b->setMarginRight(4.0f);
            b->registerClickAction([fn](brls::View*) {
                if (fn) fn();
                return true;
            });
            actions->addView(b);
            return b;
        };

        if (mode == Mode::Text)
        {
            shiftBtn = addActionBtn("Shift", false, 88.0f, [this]() { toggleShift(); });
            layoutBtn = addActionBtn(layoutLabel(), false, 84.0f, [this]() { cycleLayout(); });
            spaceBtn = addActionBtn(tr("Space"), false, 200.0f, [this]() { appendChar(" "); });
            if (disableMask & brls::KEYBOARD_DISABLE_SPACE)
            {
                spaceBtn->setTextColor(theme::textFaint());
                spaceBtn->setBackgroundColor(theme::scrim(14));
                spaceBtn->setFocusable(false);
            }
        }
        bkspBtn = addActionBtn("<-", false, 84.0f, [this]() { backspace(); });
        clearBtn = addActionBtn(tr("Clear"), false, 96.0f, [this]() { clearText(); });
        okBtn = addActionBtn("OK", true, 108.0f, [this]() { submit(); });
        panel->addView(actions);

        // Quick gamepad shortcuts
        registerAction(tr("Cancel"), brls::BUTTON_B, [this](brls::View*) {
            cancel();
            return true;
        }, false, false, brls::SOUND_BACK);

        registerAction(tr("Delete"), brls::BUTTON_X, [this](brls::View*) {
            backspace();
            return true;
        }, false, true);

        if (mode == Mode::Text)
        {
            registerAction(tr("Space"), brls::BUTTON_Y, [this](brls::View*) {
                appendChar(" ");
                return true;
            }, false, true);

            registerAction("Shift", brls::BUTTON_LB, [this](brls::View*) {
                toggleShift();
                return true;
            });

            registerAction(layoutLabel(), brls::BUTTON_RB, [this](brls::View*) {
                cycleLayout();
                return true;
            });
        }

        registerAction("OK", brls::BUTTON_START, [this](brls::View*) {
            submit();
            return true;
        });

        rebuildKeys(false);
        updatePreview();
    }

    brls::View* getDefaultFocus() override
    {
        for (brls::View* r : keysBox->getChildren())
        {
            if (auto* row = dynamic_cast<brls::Box*>(r))
            {
                for (brls::View* k : row->getChildren())
                    if (k->isFocusable()) return k;
            }
        }
        return okBtn ? (brls::View*)okBtn : brls::Box::getDefaultFocus();
    }

  private:
    struct KeySlot
    {
        brls::Button* btn = nullptr;
        std::string normal;
        std::string shifted;
    };

    Mode mode;
    Layout layout = Layout::En;
    bool shifted = false;
    bool finished = false;
    float panelW = 1000.0f;
    std::string placeholder;
    int maxLen = 128;
    std::string text;
    int disableMask = 0;
    std::string leftOpt;
    std::string rightOpt;
    std::function<void(std::string)> submitCb;

    brls::Label* countLbl = nullptr;
    brls::Label* inputLbl = nullptr;
    brls::Box* keysBox = nullptr;
    brls::Button* shiftBtn = nullptr;
    brls::Button* layoutBtn = nullptr;
    brls::Button* spaceBtn = nullptr;
    brls::Button* bkspBtn = nullptr;
    brls::Button* clearBtn = nullptr;
    brls::Button* okBtn = nullptr;
    std::vector<KeySlot> keySlots;

    std::string layoutLabel() const
    {
        switch (layout)
        {
            case Layout::En: return "EN";
            case Layout::Ru: return "RU";
            case Layout::Sym: return "#+=";
        }
        return "EN";
    }

    void updatePreview()
    {
        if (text.empty())
        {
            inputLbl->setText(placeholder.empty() ? std::string("|") : placeholder);
            inputLbl->setTextColor(placeholder.empty() ? theme::text() : theme::textMuted());
        }
        else
        {
            inputLbl->setText(text + "|");
            inputLbl->setTextColor(theme::text());
        }
        char buf[48];
        std::snprintf(buf, sizeof(buf), "%zu / %d", utf8Count(text), maxLen);
        countLbl->setText(buf);
    }

    void appendChar(const std::string& ch)
    {
        if (ch.empty() || isKeyDisabled(ch, disableMask)) return;
        if (utf8Count(text) >= (size_t)maxLen) return;
        text += ch;
        updatePreview();
    }

    void backspace()
    {
        if (text.empty()) return;
        utf8PopBack(text);
        updatePreview();
    }

    void clearText()
    {
        if (text.empty()) return;
        text.clear();
        updatePreview();
    }

    void toggleShift()
    {
        if (mode != Mode::Text) return;
        shifted = !shifted;
        if (shiftBtn)
            ui::setPillActive(shiftBtn, shifted);

        for (const auto& slot : keySlots)
        {
            const std::string& lbl = shifted ? slot.shifted : slot.normal;
            slot.btn->setText(lbl);
            bool dis = isKeyDisabled(lbl, disableMask);
            if (dis)
                slot.btn->setTextColor(theme::textFaint());
            else
                slot.btn->setTextColor(theme::text());
            slot.btn->setBackgroundColor(dis ? theme::scrim(14) : theme::scrim(26));
            slot.btn->setFocusable(!dis);
        }
    }

    void cycleLayout()
    {
        if (mode != Mode::Text) return;
        if (layout == Layout::En) layout = Layout::Ru;
        else if (layout == Layout::Ru) layout = Layout::Sym;
        else layout = Layout::En;

        if (layoutBtn) layoutBtn->setText(layoutLabel());
        rebuildKeys(true);
    }

    void rebuildKeys(bool preserveGridFocus)
    {
        int focusedRow = -1, focusedCol = -1;
        brls::View* curFocus = brls::Application::getCurrentFocus();
        if (preserveGridFocus && curFocus)
        {
            const auto& rows = keysBox->getChildren();
            for (size_t r = 0; r < rows.size(); r++)
            {
                if (auto* rowBox = dynamic_cast<brls::Box*>(rows[r]))
                {
                    const auto& cols = rowBox->getChildren();
                    for (size_t c = 0; c < cols.size(); c++)
                    {
                        if (cols[c] == curFocus)
                        {
                            focusedRow = (int)r;
                            focusedCol = (int)c;
                            break;
                        }
                    }
                }
                if (focusedRow >= 0) break;
            }
        }

        brls::View* fallback = layoutBtn ? (brls::View*)layoutBtn
                                         : (okBtn ? (brls::View*)okBtn : (brls::View*)this);
        ui::safeClearViews(keysBox, fallback);
        keySlots.clear();

        if (mode == Mode::Number)
        {
            const std::vector<std::vector<std::string>> numRows = {
                { "1", "2", "3" },
                { "4", "5", "6" },
                { "7", "8", "9" },
                { leftOpt, "0", rightOpt },
            };
            for (const auto& nr : numRows)
            {
                auto* rowBox = new ui::NearestRow();
                rowBox->setAxis(brls::Axis::ROW);
                rowBox->setJustifyContent(brls::JustifyContent::CENTER);
                rowBox->setMarginBottom(6.0f);
                for (const auto& sym : nr)
                {
                    auto* b = new brls::Button();
                    b->setShrink(0.0f);
                    b->setWidth(140.0f);
                    b->setHeight(50.0f);
                    b->setMarginLeft(4.0f);
                    b->setMarginRight(4.0f);
                    b->setText(sym);
                    b->setStyle(&brls::BUTTONSTYLE_BORDERLESS);
                    bool emptyOrDis = sym.empty() || isKeyDisabled(sym, disableMask);
                    if (emptyOrDis)
                        b->setTextColor(theme::textFaint());
                    b->setBackgroundColor(emptyOrDis ? theme::scrim(12) : theme::scrim(26));
                    b->setFontSize(20.0f);
                    b->setCornerRadius(8.0f);
                    b->setHighlightCornerRadius(8.0f +
                        brls::Application::getStyle()["brls/highlight/stroke_width"] / 2.0f);
                    b->setFocusable(!emptyOrDis);
                    if (!emptyOrDis)
                    {
                        b->registerClickAction([this, sym](brls::View*) {
                            appendChar(sym);
                            return true;
                        });
                    }
                    rowBox->addView(b);
                }
                keysBox->addView(rowBox);
            }
            return;
        }

        const auto& srcRows = (layout == Layout::Ru)  ? kRuRows
                            : (layout == Layout::Sym) ? kSymRows
                                                      : kEnRows;
        const float keyGap = 6.0f;
        const float keyW = std::floor((panelW - 44.0f - 11.0f * keyGap) / 12.0f);
        const float keyH = 48.0f;

        for (const auto& srcRow : srcRows)
        {
            auto* rowBox = new ui::NearestRow();
            rowBox->setAxis(brls::Axis::ROW);
            rowBox->setJustifyContent(brls::JustifyContent::CENTER);
            rowBox->setMarginBottom(6.0f);

            for (const auto& kp : srcRow)
            {
                size_t slotIdx = keySlots.size();
                keySlots.push_back({ nullptr, kp.normal, kp.shifted });

                auto* b = new brls::Button();
                b->setShrink(0.0f);
                b->setWidth(keyW);
                b->setHeight(keyH);
                b->setMarginLeft(keyGap * 0.5f);
                b->setMarginRight(keyGap * 0.5f);
                b->setPadding(0.0f, 2.0f, 0.0f, 2.0f);
                const std::string& lbl = shifted ? kp.shifted : kp.normal;
                b->setText(lbl);
                if (auto* l = dynamic_cast<brls::Label*>(b->getView("brls/button/label")))
                    l->setSingleLine(true);
                b->setStyle(&brls::BUTTONSTYLE_BORDERLESS);
                bool dis = isKeyDisabled(lbl, disableMask);
                if (dis)
                    b->setTextColor(theme::textFaint());
                b->setBackgroundColor(dis ? theme::scrim(14) : theme::scrim(26));
                b->setFontSize(20.0f);
                b->setCornerRadius(8.0f);
                b->setHighlightCornerRadius(8.0f +
                    brls::Application::getStyle()["brls/highlight/stroke_width"] / 2.0f);
                b->setFocusable(!dis);
                b->registerClickAction([this, slotIdx](brls::View*) {
                    if (slotIdx < keySlots.size())
                        appendChar(shifted ? keySlots[slotIdx].shifted : keySlots[slotIdx].normal);
                    return true;
                });

                keySlots[slotIdx].btn = b;
                rowBox->addView(b);
            }
            keysBox->addView(rowBox);
        }

        if (focusedRow >= 0 && !keysBox->getChildren().empty())
        {
            int r = std::min(focusedRow, (int)keysBox->getChildren().size() - 1);
            if (auto* rowBox = dynamic_cast<brls::Box*>(keysBox->getChildren()[r]))
            {
                const auto& cols = rowBox->getChildren();
                if (!cols.empty())
                {
                    int c = std::min(std::max(focusedCol, 0), (int)cols.size() - 1);
                    brls::View* target = cols[c];
                    if (!target->isFocusable())
                    {
                        for (brls::View* cand : cols)
                        {
                            if (cand->isFocusable())
                            {
                                target = cand;
                                break;
                            }
                        }
                    }
                    if (target && target->isFocusable())
                        brls::Application::giveFocus(target);
                }
            }
        }
    }

    void cancel()
    {
        if (finished) return;
        finished = true;
        brls::Application::popActivity(brls::TransitionAnimation::FADE);
    }

    void submit()
    {
        if (finished) return;
        finished = true;
        std::string out = text;
        auto cb = std::move(submitCb);
        brls::Application::popActivity(brls::TransitionAnimation::FADE, [cb, out]() {
            if (cb) cb(out);
        });
    }
};

class BuiltinImeManager : public brls::ImeManager
{
  public:
    bool openForText(std::function<void(std::string)> f,
                     std::string headerText = "",
                     std::string subText = "",
                     int maxStringLength = 32,
                     std::string initialText = "",
                     int kbdDisableBitmask = brls::KEYBOARD_DISABLE_NONE) override
    {
        auto* dlg = new OskDialog(OskDialog::Mode::Text,
                                  std::move(headerText),
                                  std::move(subText),
                                  maxStringLength,
                                  std::move(initialText),
                                  kbdDisableBitmask,
                                  "", "",
                                  std::move(f));
        brls::Application::pushActivity(new OskActivity(dlg));
        return true;
    }

    bool openForNumber(std::function<void(long)> f,
                       std::string headerText = "",
                       std::string subText = "",
                       int maxStringLength = 18,
                       std::string initialText = "",
                       std::string leftButton = "",
                       std::string rightButton = "",
                       int kbdDisableBitmask = brls::KEYBOARD_DISABLE_NONE) override
    {
        auto* dlg = new OskDialog(OskDialog::Mode::Number,
                                  std::move(headerText),
                                  std::move(subText),
                                  maxStringLength,
                                  std::move(initialText),
                                  kbdDisableBitmask,
                                  std::move(leftButton),
                                  std::move(rightButton),
                                  [f](std::string s) {
                                      if (!f || s.empty()) return;
                                      try
                                      {
                                          f(std::stoll(s));
                                      }
                                      catch (...)
                                      {
                                      }
                                  });
        brls::Application::pushActivity(new OskActivity(dlg));
        return true;
    }
};

BuiltinImeManager s_builtinIme;

} // namespace

namespace osk
{

void applyConfig()
{
    brls::Application::setImeOverride(config::get().builtinKeyboard ? &s_builtinIme : nullptr);
}

} // namespace osk
