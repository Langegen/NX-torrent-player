#pragma once

#include <cstdint>
#include <string>
#include <vector>

namespace qrcodegen
{

enum class Ecc
{
    LOW = 0,
    MEDIUM,
    QUARTILE,
    HIGH
};

class QrCode
{
public:
    static QrCode encodeText(const char* text, Ecc ecc);

    int size() const { return m_size; }
    bool getModule(int x, int y) const;

private:
    QrCode(int size, std::vector<uint8_t> modules);

    int m_size = 0;
    std::vector<uint8_t> m_modules;
};

} // namespace qrcodegen
