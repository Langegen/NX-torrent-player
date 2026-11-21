#pragma once

#include <borealis.hpp>
#include <functional>
#include <string>
#include "qrcodegen.hpp"

namespace sync
{

class QrCodeView : public brls::View
{
public:
    QrCodeView(const std::string& text, float displaySize = 220.0f);
    void setText(const std::string& text);
    void draw(NVGcontext* vg, float x, float y, float w, float h, brls::Style style, brls::FrameContext* ctx) override;

private:
    std::string m_text;
    qrcodegen::QrCode m_qr;
    float m_displaySize;
};

void showGoogleAuthDialog(std::function<void(bool success)> onComplete);

} // namespace sync
