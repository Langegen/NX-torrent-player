#include "sync_dialog.hpp"

#include <algorithm>
#include <atomic>
#include <chrono>
#include <cmath>
#include <memory>

#include "../config.hpp"
#include "../i18n.hpp"
#include "../theme.hpp"
#include "../widgets.hpp"
#include "../workers.hpp"
#include "sync_manager.hpp"

namespace cloud_sync
{

namespace
{

struct AuthState
{
    std::atomic<bool> alive{ true };
    bool completed = false;
    size_t delayId = 0;
    std::string clientId;
    std::string clientSecret;
    DeviceAuthInfo info;
    std::chrono::steady_clock::time_point deadline;
    int intervalSec     = 5;
    int transientErrors = 0;
    std::function<void(bool)> onComplete;
};

class AuthContentBox : public brls::Box
{
public:
    explicit AuthContentBox(std::shared_ptr<AuthState> st)
        : brls::Box(brls::Axis::COLUMN), state(std::move(st))
    {
    }

    ~AuthContentBox() override
    {
        if (!state) return;
        state->alive.store(false);
        if (state->delayId != 0)
        {
            brls::cancelDelay(state->delayId);
            state->delayId = 0;
        }
        if (!state->completed)
        {
            state->completed = true;
            if (state->onComplete)
            {
                auto cb = std::move(state->onComplete);
                brls::sync([cb]() { cb(false); });
            }
        }
    }

private:
    std::shared_ptr<AuthState> state;
};

void schedulePoll(const std::shared_ptr<AuthState>& state, brls::Dialog* dialog, brls::Label* statusLabel);

void runPollOnce(const std::shared_ptr<AuthState>& state, brls::Dialog* dialog, brls::Label* statusLabel)
{
    if (!state->alive.load())
        return;

    if (std::chrono::steady_clock::now() >= state->deadline)
    {
        statusLabel->setText(tr("Authorization code expired. Close and try again."));
        statusLabel->setTextColor(nvgRGB(240, 80, 80));
        return;
    }

    std::string clientId     = state->clientId;
    std::string clientSecret = state->clientSecret;
    std::string deviceCode   = state->info.deviceCode;

    workers::run([state, dialog, statusLabel, clientId, clientSecret, deviceCode]() {
        auto* gdrive = Manager::instance().gdriveProvider();
        if (!gdrive || !state->alive.load())
            return;

        GdriveTokens tokens;
        std::string pollErr;
        PollStatus res = gdrive->pollDeviceAuth(clientId, clientSecret, deviceCode, tokens, pollErr);

        brls::sync([state, dialog, statusLabel, res, tokens, pollErr]() {
            if (!state->alive.load())
                return;

            if (res == PollStatus::Approved)
            {
                state->completed = true;
                state->alive.store(false);
                if (state->delayId != 0)
                {
                    brls::cancelDelay(state->delayId);
                    state->delayId = 0;
                }

                auto& cfg                    = config::get();
                cfg.syncGdriveRefreshToken   = tokens.refreshToken;
                cfg.syncGdriveUserEmail      = tokens.userEmail;
                config::save();

                auto cb = std::move(state->onComplete);
                dialog->close([cb]() {
                    if (cb) cb(true);
                });
                return;
            }

            if (res == PollStatus::Pending)
            {
                state->transientErrors = 0;
                statusLabel->setText(tr("Waiting for approval on your device..."));
                statusLabel->setTextColor(theme::textMuted());
                schedulePoll(state, dialog, statusLabel);
                return;
            }

            if (res == PollStatus::SlowDown)
            {
                state->transientErrors = 0;
                state->intervalSec     = std::min(state->intervalSec + 5, 30);
                schedulePoll(state, dialog, statusLabel);
                return;
            }

            if (res == PollStatus::TransientError)
            {
                state->transientErrors++;
                if (state->transientErrors <= 5)
                {
                    statusLabel->setText(tr("Network error, retrying..."));
                    statusLabel->setTextColor(theme::textWarn());
                    schedulePoll(state, dialog, statusLabel);
                    return;
                }
            }

            // Failed or repeated transient errors
            state->alive.store(false);
            statusLabel->setText(pollErr.empty() ? tr("Authorization expired or failed") : pollErr);
            statusLabel->setTextColor(nvgRGB(240, 80, 80));
        });
    }, workers::Priority::High);
}

void schedulePoll(const std::shared_ptr<AuthState>& state, brls::Dialog* dialog, brls::Label* statusLabel)
{
    if (!state->alive.load())
        return;

    long ms = static_cast<long>(std::max(5, state->intervalSec)) * 1000L;
    state->delayId = brls::delay(ms, [state, dialog, statusLabel]() {
        state->delayId = 0;
        runPollOnce(state, dialog, statusLabel);
    });
}

} // namespace

QrCodeView::QrCodeView(const std::string& text, float displaySize)
    : m_text(text),
      m_qr(qrcodegen::QrCode::encodeText(text.c_str(), qrcodegen::Ecc::LOW)),
      m_displaySize(displaySize)
{
    setWidth(displaySize);
    setHeight(displaySize);
}

void QrCodeView::setText(const std::string& text)
{
    m_text = text;
    m_qr   = qrcodegen::QrCode::encodeText(text.c_str(), qrcodegen::Ecc::LOW);
}

void QrCodeView::draw(NVGcontext* vg, float x, float y, float w, float h, brls::Style style, brls::FrameContext* ctx)
{
    (void)style;
    (void)ctx;
    float side = std::min(w, h);
    if (side <= 0 || m_qr.size() <= 0) return;

    float cx   = x + w / 2.0f;
    float cy   = y + h / 2.0f;
    float left = cx - side / 2.0f;
    float top  = cy - side / 2.0f;

    // White rounded background card
    nvgBeginPath(vg);
    nvgRoundedRect(vg, left, top, side, side, 12.0f);
    nvgFillColor(vg, nvgRGB(255, 255, 255));
    nvgFill(vg);

    // ISO/IEC 18004 requires a 4-module quiet zone around the symbol
    const int border  = 4;
    int totalModules  = m_qr.size() + border * 2;
    float modSize     = side / static_cast<float>(totalModules);

    // Draw dark modules aligned to integer coordinates to prevent hairline seams
    nvgBeginPath(vg);
    nvgFillColor(vg, nvgRGB(10, 10, 10));
    for (int qy = 0; qy < m_qr.size(); qy++)
    {
        for (int qx = 0; qx < m_qr.size(); qx++)
        {
            if (m_qr.getModule(qx, qy))
            {
                float x0 = std::floor(left + (qx + border) * modSize);
                float y0 = std::floor(top + (qy + border) * modSize);
                float x1 = std::floor(left + (qx + 1 + border) * modSize);
                float y1 = std::floor(top + (qy + 1 + border) * modSize);
                nvgRect(vg, x0, y0, x1 - x0, y1 - y0);
            }
        }
    }
    nvgFill(vg);
}

void showGoogleAuthDialog(std::function<void(bool success)> onComplete)
{
    auto* gdrive = Manager::instance().gdriveProvider();
    if (!gdrive) return;

    auto state          = std::make_shared<AuthState>();
    state->clientId     = GdriveProvider::defaultClientId();
    state->clientSecret = GdriveProvider::defaultClientSecret();
    state->onComplete   = std::move(onComplete);

    auto* box = new AuthContentBox(state);
    box->setAlignItems(brls::AlignItems::CENTER);
    box->setJustifyContent(brls::JustifyContent::CENTER);
    box->setPadding(24.0f, 32.0f, 20.0f, 32.0f);

    auto* title = new brls::Label();
    title->setText(tr("Sign in to Google Drive"));
    title->setFontSize(24.0f);
    title->setMarginBottom(12.0f);
    box->addView(title);

    auto* detailsBox = new brls::Box(brls::Axis::COLUMN);
    detailsBox->setAlignItems(brls::AlignItems::CENTER);
    detailsBox->setVisibility(brls::Visibility::GONE);
    box->addView(detailsBox);

    auto* statusLabel = new brls::Label();
    statusLabel->setText(tr("Requesting Google authorization code..."));
    statusLabel->setFontSize(15.0f);
    statusLabel->setTextColor(theme::textMuted());
    statusLabel->setHorizontalAlign(brls::HorizontalAlign::CENTER);
    statusLabel->setSingleLine(false);
    statusLabel->setMarginTop(4.0f);
    statusLabel->setMarginBottom(8.0f);
    box->addView(statusLabel);

    auto* dialog = new brls::Dialog(box);
    dialog->addButton(tr("Cancel"), [state]() {
        state->alive.store(false);
        if (state->delayId != 0)
        {
            brls::cancelDelay(state->delayId);
            state->delayId = 0;
        }
        if (!state->completed)
        {
            state->completed = true;
            if (state->onComplete)
            {
                auto cb = std::move(state->onComplete);
                cb(false);
            }
        }
    });
    dialog->open();

    std::string clientId = state->clientId;
    workers::run([state, dialog, detailsBox, statusLabel, clientId]() {
        auto* gd = Manager::instance().gdriveProvider();
        DeviceAuthInfo info;
        std::string err;
        bool ok = gd && gd->startDeviceAuth(clientId, info, err);

        brls::sync([state, dialog, detailsBox, statusLabel, ok, info, err]() {
            if (!state->alive.load())
                return;

            if (!ok)
            {
                state->alive.store(false);
                statusLabel->setText(tr("Google Drive Error: ") + err);
                statusLabel->setTextColor(nvgRGB(240, 80, 80));
                dialog->invalidate();
                return;
            }

            state->info        = info;
            state->intervalSec = std::max(5, info.interval);
            state->deadline    = std::chrono::steady_clock::now() + std::chrono::seconds(info.expiresIn > 0 ? info.expiresIn : 1800);

            auto* qr = new QrCodeView(info.verificationUrl, 200.0f);
            qr->setMarginBottom(14.0f);
            detailsBox->addView(qr);

            auto* step1 = new brls::Label();
            step1->setText(tr("1. Scan QR or open: ") + info.verificationUrl);
            step1->setFontSize(16.0f);
            step1->setTextColor(theme::textMuted());
            step1->setHorizontalAlign(brls::HorizontalAlign::CENTER);
            step1->setSingleLine(false);
            step1->setMarginBottom(8.0f);
            detailsBox->addView(step1);

            auto* step2 = new brls::Label();
            step2->setText(tr("2. Enter code: ") + info.userCode);
            step2->setFontSize(28.0f);
            step2->setTextColor(theme::accent());
            step2->setHorizontalAlign(brls::HorizontalAlign::CENTER);
            step2->setMarginBottom(12.0f);
            detailsBox->addView(step2);

#if !defined(__SWITCH__)
            auto* openBtn = ui::actionButton(tr("Open in Browser"), true, [info, statusLabel]() {
                brls::Application::getPlatform()->pasteToClipboard(info.userCode);
                brls::Application::getPlatform()->openBrowser(info.verificationUrl);
                statusLabel->setText(tr("Code copied to clipboard. Complete sign-in in your browser..."));
                statusLabel->setTextColor(theme::textMuted());
            });
            openBtn->setMarginRight(0.0f);
            openBtn->setMarginBottom(10.0f);
            detailsBox->addView(openBtn);
#endif

            detailsBox->setVisibility(brls::Visibility::VISIBLE);
            statusLabel->setText(tr("Waiting for approval on your device..."));
            statusLabel->setTextColor(theme::textMuted());
            dialog->invalidate();

            schedulePoll(state, dialog, statusLabel);
        });
    }, workers::Priority::High);
}

} // namespace cloud_sync
