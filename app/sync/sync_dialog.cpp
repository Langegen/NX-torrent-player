#include "sync_dialog.hpp"

#include <atomic>
#include <chrono>
#include <memory>
#include <thread>

#include "../i18n.hpp"
#include "../theme.hpp"
#include "sync_manager.hpp"

namespace sync
{

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
    m_qr = qrcodegen::QrCode::encodeText(text.c_str(), qrcodegen::Ecc::LOW);
}

void QrCodeView::draw(NVGcontext* vg, float x, float y, float w, float h, brls::Style style, brls::FrameContext* ctx)
{
    float side = std::min(w, h);
    if (side <= 0 || m_qr.size() <= 0) return;

    float cx = x + w / 2.0f;
    float cy = y + h / 2.0f;
    float left = cx - side / 2.0f;
    float top = cy - side / 2.0f;

    // White rounded background card
    nvgBeginPath(vg);
    nvgRoundedRect(vg, left, top, side, side, 12.0f);
    nvgFillColor(vg, nvgRGB(255, 255, 255));
    nvgFill(vg);

    // Border padding in modules
    const int border = 2;
    int totalModules = m_qr.size() + border * 2;
    float modSize = side / totalModules;

    // Draw dark modules
    nvgBeginPath(vg);
    nvgFillColor(vg, nvgRGB(10, 10, 10));
    for (int qy = 0; qy < m_qr.size(); qy++)
    {
        for (int qx = 0; qx < m_qr.size(); qx++)
        {
            if (m_qr.getModule(qx, qy))
            {
                float mx = left + (qx + border) * modSize;
                float my = top + (qy + border) * modSize;
                nvgRect(vg, mx, my, modSize, modSize);
            }
        }
    }
    nvgFill(vg);
}

void showGoogleAuthDialog(std::function<void(bool success)> onComplete)
{
    auto* gdrive = Manager::instance().gdriveProvider();
    if (!gdrive) return;

    DeviceAuthInfo info;
    std::string err;
    if (!gdrive->startDeviceAuth(info, err))
    {
        auto* errDialog = new brls::Dialog(tr("Google Drive Error: ") + err);
        errDialog->addButton(tr("OK"), [onComplete]() {
            if (onComplete) onComplete(false);
        });
        errDialog->open();
        return;
    }

    auto* box = new brls::Box();
    box->setAxis(brls::Axis::COLUMN);
    box->setAlignment(brls::Alignment::CENTER);
    box->setPadding(24.0f, 32.0f, 24.0f, 32.0f);

    auto* title = new brls::Label();
    title->setText(tr("Sign in to Google Drive"));
    title->setFontSize(24.0f);
    title->setMarginBottom(12.0f);
    box->addView(title);

    // QR code with direct link (or verification URL)
    std::string qrTarget = info.verificationUrl;
    auto* qr = new QrCodeView(qrTarget, 200.0f);
    qr->setMarginBottom(14.0f);
    box->addView(qr);

    auto* step1 = new brls::Label();
    step1->setText(tr("1. Scan QR or open: ") + info.verificationUrl);
    step1->setFontSize(16.0f);
    step1->setTextColor(theme::textMuted());
    step1->setMarginBottom(8.0f);
    box->addView(step1);

    auto* step2 = new brls::Label();
    step2->setText(tr("2. Enter code: ") + info.userCode);
    step2->setFontSize(28.0f);
    step2->setTextColor(theme::accent());
    step2->setMarginBottom(16.0f);
    box->addView(step2);

    auto* statusLabel = new brls::Label();
    statusLabel->setText(tr("Waiting for approval on your device..."));
    statusLabel->setFontSize(14.0f);
    statusLabel->setTextColor(theme::textMuted());
    statusLabel->setMarginBottom(16.0f);
    box->addView(statusLabel);

    auto* dialog = new brls::Dialog(box);
    auto alive = std::make_shared<std::atomic<bool>>(true);

    dialog->addButton(tr("Cancel"), [alive, onComplete]() {
        *alive = false;
        if (onComplete) onComplete(false);
    });

    std::thread([alive, info, dialog, statusLabel, onComplete]() {
        auto* gdrive = Manager::instance().gdriveProvider();
        int interval = info.interval > 0 ? info.interval : 5;

        while (*alive)
        {
            std::this_thread::sleep_for(std::chrono::seconds(interval));
            if (!*alive) break;

            std::string pollErr;
            int res = gdrive->pollDeviceAuth(info.deviceCode, pollErr);

            if (res == 1) // Approved
            {
                *alive = false;
                brls::async([dialog, onComplete]() {
                    dialog->close([onComplete]() {
                        if (onComplete) onComplete(true);
                    });
                });
                break;
            }
            else if (res == -1) // Error / expired
            {
                std::string msg = pollErr;
                brls::async([statusLabel, msg]() {
                    statusLabel->setText(msg);
                    statusLabel->setTextColor(nvgRGB(240, 80, 80));
                });
                break;
            }
        }
    }).detach();

    dialog->open();
}

} // namespace sync
