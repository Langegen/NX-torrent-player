/*
 * QR Code generator library (C++)
 *
 * Core algorithm based on Project Nayuki's open source QR Code generator (MIT License).
 * https://www.nayuki.io/page/qr-code-generator-library
 */

#include "qrcodegen.hpp"

#include <algorithm>
#include <climits>
#include <cstddef>
#include <cstdlib>
#include <cstring>
#include <sstream>
#include <stdexcept>
#include <utility>

namespace qrcodegen
{

namespace
{

constexpr int MIN_VERSION = 1;
constexpr int MAX_VERSION = 40;

// Returns the error correction codeword count per block for the given version and ECC level.
int getNumDataCodewords(int ver, Ecc ecl)
{
    static const int8_t NUM_ERROR_CORRECTION_CODEWORDS[4][41] = {
        // Version: 0, 1,  2,  3,  4,  5,   6,   7,   8,   9,  10,  11,  12,  13,  14,  15,  16,  17,  18,  19,  20,  21,  22,  23,  24,   25,   26,   27,   28,   29,   30,   31,   32,   33,   34,   35,   36,   37,   38,   39,   40
        { -1,  7, 10, 15, 20, 26,  18,  20,  24,  30,  18,  20,  24,  26,  30,  22,  24,  28,  30,  28,  28,  28,  28,  30,  30,   26,   28,   30,   30,   30,   30,   30,   30,   30,   30,   30,   30,   30,   30,   30,   30 }, // Low
        { -1, 10, 16, 26, 18, 24,  16,  18,  22,  22,  26,  30,  22,  22,  24,  24,  28,  28,  26,  26,  26,  26,  28,  28,  28,   28,   28,   28,   28,   28,   28,   28,   28,   28,   28,   28,   28,   28,   28,   28,   28 }, // Medium
        { -1, 13, 22, 18, 26, 18,  24,  18,  22,  20,  24,  28,  26,  24,  20,  30,  24,  28,  28,  26,  30,  28,  30,  30,  30,   30,   28,   30,   30,   30,   30,   30,   30,   30,   30,   30,   30,   30,   30,   30,   30 }, // Quartile
        { -1, 17, 28, 22, 16, 22,  28,  26,  26,  24,  28,  24,  28,  22,  24,  24,  30,  28,  28,  26,  28,  30,  24,  30,  30,   30,   30,   30,   30,   30,   30,   30,   30,   30,   30,   30,   30,   30,   30,   30,   30 }, // High
    };
    static const int8_t NUM_ERROR_CORRECTION_BLOCKS[4][41] = {
        { -1, 1, 1, 1, 1, 1, 2, 2, 2, 2, 4, 4, 4, 4, 4, 6, 6, 6, 6, 7, 8, 8,  9,  9, 10, 12, 12, 12, 13, 14, 15, 16, 17, 18, 20, 21, 23, 25, 26, 28, 29 },
        { -1, 1, 1, 1, 2, 2, 4, 4, 4, 5, 5, 5, 8, 9, 9, 10, 10, 11, 13, 14, 16, 17, 17, 18, 20, 21, 23, 25, 26, 28, 29, 31, 33, 35, 37, 38, 40, 43, 45, 47, 49 },
        { -1, 1, 1, 2, 2, 4, 4, 6, 6, 8, 8, 8, 10, 12, 16, 12, 17, 16, 18, 21, 20, 23, 23, 25, 27, 29, 34, 34, 35, 38, 40, 43, 45, 48, 51, 53, 56, 59, 62, 65, 68 },
        { -1, 1, 1, 2, 4, 4, 4, 5, 6, 8, 8, 11, 11, 16, 16, 18, 16, 19, 21, 25, 25, 25, 34, 30, 32, 35, 37, 40, 42, 45, 48, 51, 54, 57, 60, 63, 66, 70, 74, 77, 81 },
    };

    static const int16_t TOTAL_CODEWORDS[41] = {
        -1, 26, 44, 70, 100, 134, 172, 196, 242, 292, 346, 404, 466, 532, 581, 655, 733, 815, 901, 991, 1085, 1156, 1258, 1364, 1474, 1588, 1706, 1828, 1921, 2051, 2185, 2323, 2465, 2611, 2761, 2876, 3034, 3196, 3362, 3532, 3706
    };

    int eclIndex = static_cast<int>(ecl);
    int total = TOTAL_CODEWORDS[ver];
    int ecTotal = NUM_ERROR_CORRECTION_CODEWORDS[eclIndex][ver] * NUM_ERROR_CORRECTION_BLOCKS[eclIndex][ver];
    return total - ecTotal;
}

// Reed-Solomon generator polynomial
std::vector<uint8_t> reedSolomonComputeDivisor(int degree)
{
    std::vector<uint8_t> result(degree);
    result[degree - 1] = 1;

    uint8_t root = 1;
    for (int i = 0; i < degree; i++)
    {
        for (size_t j = 0; j < result.size(); j++)
        {
            uint8_t p = result[j];
            // multiply in GF(2^8)
            uint8_t term = 0;
            if (p != 0 && root != 0)
            {
                // Simple GF table-less multiplication
                uint8_t a = p, b = root;
                while (b > 0)
                {
                    if (b & 1) term ^= a;
                    a = (a << 1) ^ ((a & 0x80) ? 0x11D : 0);
                    b >>= 1;
                }
            }
            result[j] = term;
            if (j + 1 < result.size())
                result[j] ^= result[j + 1];
        }
        // root *= 2 in GF(2^8)
        root = (root << 1) ^ ((root & 0x80) ? 0x11D : 0);
    }
    return result;
}

std::vector<uint8_t> reedSolomonComputeRemainder(const std::vector<uint8_t>& data, const std::vector<uint8_t>& divisor)
{
    std::vector<uint8_t> result(divisor.size(), 0);
    for (uint8_t b : data)
    {
        uint8_t factor = b ^ result[0];
        result.erase(result.begin());
        result.push_back(0);
        for (size_t i = 0; i < divisor.size(); i++)
        {
            uint8_t term = 0;
            if (factor != 0 && divisor[i] != 0)
            {
                uint8_t a = factor, d = divisor[i];
                while (d > 0)
                {
                    if (d & 1) term ^= a;
                    a = (a << 1) ^ ((a & 0x80) ? 0x11D : 0);
                    d >>= 1;
                }
            }
            result[i] ^= term;
        }
    }
    return result;
}

struct BitBuffer
{
    std::vector<uint8_t> bytes;
    int bitLength = 0;

    void appendBits(uint32_t val, int len)
    {
        for (int i = len - 1; i >= 0; i--)
        {
            int bit = (val >> i) & 1;
            if (bitLength % 8 == 0)
                bytes.push_back(0);
            bytes.back() |= (bit << (7 - (bitLength % 8)));
            bitLength++;
        }
    }
};

} // namespace

QrCode::QrCode(int size, std::vector<uint8_t> modules)
    : m_size(size), m_modules(std::move(modules))
{
}

bool QrCode::getModule(int x, int y) const
{
    if (x < 0 || x >= m_size || y < 0 || y >= m_size)
        return false;
    return m_modules[y * m_size + x] != 0;
}

QrCode QrCode::encodeText(const char* text, Ecc ecl)
{
    size_t textLen = std::strlen(text);
    BitBuffer bb;

    // Mode: Byte (0100)
    bb.appendBits(0x4, 4);

    // Determine version needed
    int version = 1;
    for (; version <= 10; version++)
    {
        int countBits = (version < 10) ? 8 : 16;
        int totalBits = 4 + countBits + textLen * 8;
        int dataCapacityCodewords = getNumDataCodewords(version, ecl);
        if (totalBits <= dataCapacityCodewords * 8)
            break;
    }
    if (version > 10)
        version = 10;

    int charCountBits = (version < 10) ? 8 : 16;
    bb.appendBits(static_cast<uint32_t>(textLen), charCountBits);
    for (size_t i = 0; i < textLen; i++)
        bb.appendBits(static_cast<uint8_t>(text[i]), 8);

    int dataCapacityCodewords = getNumDataCodewords(version, ecl);
    int dataCapacityBits = dataCapacityCodewords * 8;

    // Terminator
    int terminatorBits = std::min(4, dataCapacityBits - bb.bitLength);
    bb.appendBits(0, terminatorBits);

    // Pad to byte boundary
    if (bb.bitLength % 8 != 0)
        bb.appendBits(0, 8 - (bb.bitLength % 8));

    // Pad bytes 0xEC, 0x11
    while (bb.bytes.size() < static_cast<size_t>(dataCapacityCodewords))
    {
        bb.bytes.push_back((bb.bytes.size() % 2 == 0) ? 0xEC : 0x11);
    }

    int qrSize = version * 4 + 17;
    std::vector<uint8_t> grid(qrSize * qrSize, 0);
    std::vector<uint8_t> isFunction(qrSize * qrSize, 0);

    auto setFunc = [&](int x, int y, bool isDark) {
        grid[y * qrSize + x] = isDark ? 1 : 0;
        isFunction[y * qrSize + x] = 1;
    };

    // Finder patterns
    auto drawFinder = [&](int startX, int startY) {
        for (int y = -1; y <= 7; y++)
        {
            for (int x = -1; x <= 7; x++)
            {
                int gx = startX + x;
                int gy = startY + y;
                if (gx < 0 || gx >= qrSize || gy < 0 || gy >= qrSize) continue;
                bool dark = (x >= 0 && x <= 6 && (y == 0 || y == 6)) ||
                            (y >= 0 && y <= 6 && (x == 0 || x == 6)) ||
                            (x >= 2 && x <= 4 && y >= 2 && y <= 4);
                setFunc(gx, gy, dark);
            }
        }
    };
    drawFinder(0, 0);
    drawFinder(qrSize - 7, 0);
    drawFinder(0, qrSize - 7);

    // Timing patterns
    for (int i = 0; i < qrSize; i++)
    {
        if (!isFunction[6 * qrSize + i])
            setFunc(i, 6, (i % 2 == 0));
        if (!isFunction[i * qrSize + 6])
            setFunc(6, i, (i % 2 == 0));
    }

    // Dark module
    setFunc(8, 4 * version + 9, true);

    // Format info reserve
    for (int i = 0; i < 9; i++)
    {
        if (i < qrSize) {
            isFunction[8 * qrSize + i] = 1;
            isFunction[i * qrSize + 8] = 1;
        }
    }
    for (int i = 0; i < 8; i++)
    {
        if (qrSize - 1 - i >= 0) {
            isFunction[8 * qrSize + (qrSize - 1 - i)] = 1;
            isFunction[(qrSize - 1 - i) * qrSize + 8] = 1;
        }
    }

    // Error correction
    static const int8_t NUM_EC_BLOCKS[4][41] = {
        { -1, 1, 1, 1, 1, 1, 2, 2, 2, 2, 4 },
        { -1, 1, 1, 1, 2, 2, 4, 4, 4, 5, 5 },
        { -1, 1, 1, 2, 2, 4, 4, 6, 6, 8, 8 },
        { -1, 1, 1, 2, 4, 4, 4, 5, 6, 8, 8 }
    };
    static const int8_t NUM_EC_CODEWORDS[4][41] = {
        { -1,  7, 10, 15, 20, 26, 18, 20, 24, 30, 18 },
        { -1, 10, 16, 26, 18, 24, 16, 18, 22, 22, 26 },
        { -1, 13, 22, 18, 26, 18, 24, 18, 22, 20, 24 },
        { -1, 17, 28, 22, 16, 22, 28, 26, 26, 24, 28 }
    };

    int eclIdx = static_cast<int>(ecl);
    int numBlocks = NUM_EC_BLOCKS[eclIdx][version];
    int blockEcLen = NUM_EC_CODEWORDS[eclIdx][version];
    int shortBlockDataLen = dataCapacityCodewords / numBlocks;
    int numShortBlocks = numBlocks - (dataCapacityCodewords % numBlocks);

    std::vector<std::vector<uint8_t>> dataBlocks(numBlocks);
    std::vector<std::vector<uint8_t>> ecBlocks(numBlocks);
    auto divisor = reedSolomonComputeDivisor(blockEcLen);

    size_t k = 0;
    for (int b = 0; b < numBlocks; b++)
    {
        int datLen = (b < numShortBlocks) ? shortBlockDataLen : (shortBlockDataLen + 1);
        dataBlocks[b].assign(bb.bytes.begin() + k, bb.bytes.begin() + k + datLen);
        k += datLen;
        ecBlocks[b] = reedSolomonComputeRemainder(dataBlocks[b], divisor);
    }

    // Interleave codewords
    std::vector<uint8_t> allCodewords;
    for (int i = 0; i <= shortBlockDataLen; i++)
    {
        for (int b = 0; b < numBlocks; b++)
        {
            if (i < static_cast<int>(dataBlocks[b].size()))
                allCodewords.push_back(dataBlocks[b][i]);
        }
    }
    for (int i = 0; i < blockEcLen; i++)
    {
        for (int b = 0; b < numBlocks; b++)
            allCodewords.push_back(ecBlocks[b][i]);
    }

    // Place codewords into grid
    int bitIdx = 0;
    int totalCodewordBits = allCodewords.size() * 8;
    int right = qrSize - 1;
    while (right > 0)
    {
        if (right == 6) right--; // skip vertical timing line
        for (int vert = 0; vert < qrSize; vert++)
        {
            for (int j = 0; j < 2; j++)
            {
                int x = right - j;
                bool upwards = ((right + 1) & 2) == 0;
                int y = upwards ? (qrSize - 1 - vert) : vert;
                if (!isFunction[y * qrSize + x])
                {
                    bool dark = false;
                    if (bitIdx < totalCodewordBits)
                    {
                        dark = ((allCodewords[bitIdx >> 3] >> (7 - (bitIdx & 7))) & 1) != 0;
                        bitIdx++;
                    }
                    // Apply Mask 0: (x + y) % 2 == 0
                    if ((x + y) % 2 == 0)
                        dark = !dark;
                    grid[y * qrSize + x] = dark ? 1 : 0;
                }
            }
        }
        right -= 2;
    }

    // Format bits for Mask 0 and selected ECC
    // Format bits calculation (15 bits with BCH code):
    static const uint16_t FORMAT_BITS[4] = {
        0x77C4, // Low, mask 0
        0x5465, // Medium, mask 0
        0x355F, // Quartile, mask 0
        0x168B  // High, mask 0
    };
    uint16_t formatBits = FORMAT_BITS[eclIdx];

    // Draw format info
    for (int i = 0; i <= 5; i++)
        grid[8 * qrSize + i] = ((formatBits >> i) & 1);
    grid[8 * qrSize + 7] = ((formatBits >> 6) & 1);
    grid[8 * qrSize + 8] = ((formatBits >> 7) & 1);
    grid[7 * qrSize + 8] = ((formatBits >> 8) & 1);
    for (int i = 9; i < 15; i++)
        grid[(14 - i) * qrSize + 8] = ((formatBits >> i) & 1);

    for (int i = 0; i <= 7; i++)
        grid[(qrSize - 1 - i) * qrSize + 8] = ((formatBits >> i) & 1);
    for (int i = 8; i < 15; i++)
        grid[8 * qrSize + (qrSize - 15 + i)] = ((formatBits >> i) & 1);

    return QrCode(qrSize, grid);
}

} // namespace qrcodegen
