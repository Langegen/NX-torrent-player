/*
 * QR Code generator library (C++)
 *
 * Core algorithm based on Project Nayuki's open source QR Code generator (MIT License).
 * https://www.nayuki.io/page/qr-code-generator-library
 */

#include "qrcodegen.hpp"

#include <algorithm>
#include <climits>
#include <cstdlib>
#include <cstring>
#include <vector>

namespace qrcodegen
{

namespace
{

constexpr int MIN_VERSION = 1;
constexpr int MAX_VERSION = 40;
constexpr int PENALTY_N1  = 3;
constexpr int PENALTY_N2  = 3;
constexpr int PENALTY_N3  = 40;
constexpr int PENALTY_N4  = 10;

// ISO/IEC 18004 Table 9: Error correction codewords per block.
const int8_t ECC_CODEWORDS_PER_BLOCK[4][41] = {
    //0,  1,  2,  3,  4,  5,  6,  7,  8,  9, 10, 11, 12, 13, 14, 15, 16, 17, 18, 19, 20, 21, 22, 23, 24, 25, 26, 27, 28, 29, 30, 31, 32, 33, 34, 35, 36, 37, 38, 39, 40
    {-1,  7, 10, 15, 20, 26, 18, 20, 24, 30, 18, 20, 24, 26, 30, 22, 24, 28, 30, 28, 28, 28, 28, 30, 30, 26, 28, 30, 30, 30, 30, 30, 30, 30, 30, 30, 30, 30, 30, 30, 30}, // Low
    {-1, 10, 16, 26, 18, 24, 16, 18, 22, 22, 26, 30, 22, 22, 24, 24, 28, 28, 26, 26, 26, 26, 28, 28, 28, 28, 28, 28, 28, 28, 28, 28, 28, 28, 28, 28, 28, 28, 28, 28, 28}, // Medium
    {-1, 13, 22, 18, 26, 18, 24, 18, 22, 20, 24, 28, 26, 24, 20, 30, 24, 28, 28, 26, 30, 28, 30, 30, 30, 30, 28, 30, 30, 30, 30, 30, 30, 30, 30, 30, 30, 30, 30, 30, 30}, // Quartile
    {-1, 17, 28, 22, 16, 22, 28, 26, 26, 24, 28, 24, 28, 22, 24, 24, 30, 28, 28, 26, 28, 30, 24, 30, 30, 30, 30, 30, 30, 30, 30, 30, 30, 30, 30, 30, 30, 30, 30, 30, 30}, // High
};

// ISO/IEC 18004 Table 9: Number of error correction blocks.
const int8_t NUM_ERROR_CORRECTION_BLOCKS[4][41] = {
    //0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15, 16, 17, 18, 19, 20, 21, 22, 23, 24, 25, 26, 27, 28, 29, 30, 31, 32, 33, 34, 35, 36, 37, 38, 39, 40
    {-1, 1, 1, 1, 1, 1, 2, 2, 2, 2,  4,  4,  4,  4,  4,  6,  6,  6,  6,  7,  8,  8,  9,  9, 10, 12, 12, 12, 13, 14, 15, 16, 17, 18, 19, 19, 20, 21, 22, 24, 25}, // Low
    {-1, 1, 1, 1, 2, 2, 4, 4, 4, 5,  5,  5,  8,  9,  9, 10, 10, 11, 13, 14, 16, 17, 17, 18, 20, 21, 23, 25, 26, 28, 29, 31, 33, 35, 37, 38, 40, 43, 45, 47, 49}, // Medium
    {-1, 1, 1, 2, 2, 4, 4, 6, 6, 8,  8,  8, 10, 12, 16, 12, 17, 16, 18, 21, 20, 23, 23, 25, 27, 29, 34, 34, 35, 38, 40, 43, 45, 48, 51, 53, 56, 59, 62, 65, 68}, // Quartile
    {-1, 1, 1, 2, 4, 4, 4, 5, 6, 8,  8, 11, 11, 16, 16, 18, 16, 19, 21, 25, 25, 25, 34, 30, 32, 35, 37, 40, 42, 45, 48, 51, 54, 57, 60, 63, 66, 70, 74, 77, 81}, // High
};

int getNumRawDataModules(int ver)
{
    int result = (16 * ver + 128) * ver + 64;
    if (ver >= 2)
    {
        int numAlign = ver / 7 + 2;
        result -= (25 * numAlign - 10) * numAlign - 55;
        if (ver >= 7)
            result -= 36;
    }
    return result;
}

int getNumDataCodewords(int ver, Ecc ecl)
{
    int eclIdx = static_cast<int>(ecl);
    return getNumRawDataModules(ver) / 8
           - ECC_CODEWORDS_PER_BLOCK[eclIdx][ver] * NUM_ERROR_CORRECTION_BLOCKS[eclIdx][ver];
}

int getFormatBits(Ecc ecl)
{
    switch (ecl)
    {
        case Ecc::LOW:      return 1;
        case Ecc::MEDIUM:   return 0;
        case Ecc::QUARTILE: return 3;
        case Ecc::HIGH:     return 2;
        default:            return 1;
    }
}

std::vector<int> getAlignmentPatternPositions(int ver)
{
    if (ver == 1)
        return {};
    int numAlign = ver / 7 + 2;
    int step = (ver == 32) ? 26 : (ver * 4 + numAlign * 2 + 1) / (numAlign * 2 - 2) * 2;
    std::vector<int> result;
    for (int i = 0, pos = ver * 4 + 10; i < numAlign - 1; i++, pos -= step)
        result.insert(result.begin(), pos);
    result.insert(result.begin(), 6);
    return result;
}

uint8_t reedSolomonMultiply(uint8_t x, uint8_t y)
{
    uint8_t z = 0;
    for (int i = 7; i >= 0; i--)
    {
        z = static_cast<uint8_t>((z << 1) ^ ((z >> 7) * 0x11D));
        z ^= static_cast<uint8_t>(((y >> i) & 1) * x);
    }
    return z;
}

std::vector<uint8_t> reedSolomonComputeDivisor(int degree)
{
    std::vector<uint8_t> result(static_cast<size_t>(degree), 0);
    result[static_cast<size_t>(degree - 1)] = 1;

    uint8_t root = 1;
    for (int i = 0; i < degree; i++)
    {
        for (size_t j = 0; j < result.size(); j++)
        {
            result[j] = reedSolomonMultiply(result[j], root);
            if (j + 1 < result.size())
                result[j] ^= result[j + 1];
        }
        root = reedSolomonMultiply(root, 0x02);
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
            result[i] ^= reedSolomonMultiply(divisor[i], factor);
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
            bytes.back() |= static_cast<uint8_t>(bit << (7 - (bitLength % 8)));
            bitLength++;
        }
    }
};

class QrBuilder
{
public:
    int version;
    int size;
    Ecc ecl;
    std::vector<uint8_t> modules;
    std::vector<uint8_t> isFunction;

    QrBuilder(int ver, Ecc eclLevel)
        : version(ver),
          size(ver * 4 + 17),
          ecl(eclLevel),
          modules(static_cast<size_t>(size * size), 0),
          isFunction(static_cast<size_t>(size * size), 0)
    {
        drawFunctionPatterns();
    }

    bool module(int x, int y) const
    {
        return modules[static_cast<size_t>(y * size + x)] != 0;
    }

    void setFunctionModule(int x, int y, bool isDark)
    {
        size_t idx       = static_cast<size_t>(y * size + x);
        modules[idx]     = isDark ? 1 : 0;
        isFunction[idx]  = 1;
    }

    void drawFinderPattern(int x, int y)
    {
        for (int dy = -4; dy <= 4; dy++)
        {
            for (int dx = -4; dx <= 4; dx++)
            {
                int dist = std::max(std::abs(dx), std::abs(dy));
                int xx = x + dx;
                int yy = y + dy;
                if (xx >= 0 && xx < size && yy >= 0 && yy < size)
                    setFunctionModule(xx, yy, dist != 2 && dist != 4);
            }
        }
    }

    void drawAlignmentPattern(int x, int y)
    {
        for (int dy = -2; dy <= 2; dy++)
        {
            for (int dx = -2; dx <= 2; dx++)
                setFunctionModule(x + dx, y + dy, std::max(std::abs(dx), std::abs(dy)) != 1);
        }
    }

    void drawFormatBits(int mask)
    {
        int data = (getFormatBits(ecl) << 3) | mask;
        int rem  = data;
        for (int i = 0; i < 10; i++)
            rem = (rem << 1) ^ ((rem >> 9) * 0x537);
        int bits = ((data << 10) | rem) ^ 0x5412;

        // Top-left copy
        for (int i = 0; i <= 5; i++)
            setFunctionModule(8, i, ((bits >> i) & 1) != 0);
        setFunctionModule(8, 7, ((bits >> 6) & 1) != 0);
        setFunctionModule(8, 8, ((bits >> 7) & 1) != 0);
        setFunctionModule(7, 8, ((bits >> 8) & 1) != 0);
        for (int i = 9; i < 15; i++)
            setFunctionModule(14 - i, 8, ((bits >> i) & 1) != 0);

        // Top-right and bottom-left copy
        for (int i = 0; i < 8; i++)
            setFunctionModule(size - 1 - i, 8, ((bits >> i) & 1) != 0);
        for (int i = 8; i < 15; i++)
            setFunctionModule(8, size - 15 + i, ((bits >> i) & 1) != 0);
        setFunctionModule(8, size - 8, true); // Fixed dark module
    }

    void drawVersion()
    {
        if (version < 7)
            return;
        int rem = version;
        for (int i = 0; i < 12; i++)
            rem = (rem << 1) ^ ((rem >> 11) * 0x1F25);
        long bits = (static_cast<long>(version) << 12) | rem;

        for (int i = 0; i < 18; i++)
        {
            bool bit = ((bits >> i) & 1) != 0;
            int a = size - 11 + i % 3;
            int b = i / 3;
            setFunctionModule(a, b, bit);
            setFunctionModule(b, a, bit);
        }
    }

    void drawFunctionPatterns()
    {
        // Timing patterns
        for (int i = 0; i < size; i++)
        {
            setFunctionModule(6, i, i % 2 == 0);
            setFunctionModule(i, 6, i % 2 == 0);
        }

        // Finder patterns
        drawFinderPattern(3, 3);
        drawFinderPattern(size - 4, 3);
        drawFinderPattern(3, size - 4);

        // Alignment patterns
        std::vector<int> alignPatPos = getAlignmentPatternPositions(version);
        size_t numAlign = alignPatPos.size();
        for (size_t i = 0; i < numAlign; i++)
        {
            for (size_t j = 0; j < numAlign; j++)
            {
                if (!((i == 0 && j == 0) || (i == 0 && j == numAlign - 1) || (i == numAlign - 1 && j == 0)))
                    drawAlignmentPattern(alignPatPos[i], alignPatPos[j]);
            }
        }

        // Reserve format bits (mask 0 placeholder) and version bits
        drawFormatBits(0);
        drawVersion();
    }

    void drawCodewords(const std::vector<uint8_t>& data)
    {
        size_t i = 0;
        size_t totalBits = data.size() * 8;
        for (int right = size - 1; right >= 1; right -= 2)
        {
            if (right == 6)
                right = 5;
            for (int vert = 0; vert < size; vert++)
            {
                for (int j = 0; j < 2; j++)
                {
                    int x = right - j;
                    bool upward = ((right + 1) & 2) == 0;
                    int y = upward ? (size - 1 - vert) : vert;
                    size_t idx = static_cast<size_t>(y * size + x);
                    if (!isFunction[idx] && i < totalBits)
                    {
                        modules[idx] = ((data[i >> 3] >> (7 - (i & 7))) & 1) != 0 ? 1 : 0;
                        i++;
                    }
                }
            }
        }
    }

    void applyMask(int mask)
    {
        for (int y = 0; y < size; y++)
        {
            for (int x = 0; x < size; x++)
            {
                size_t idx = static_cast<size_t>(y * size + x);
                if (isFunction[idx])
                    continue;
                bool invert = false;
                switch (mask)
                {
                    case 0: invert = (x + y) % 2 == 0; break;
                    case 1: invert = y % 2 == 0; break;
                    case 2: invert = x % 3 == 0; break;
                    case 3: invert = (x + y) % 3 == 0; break;
                    case 4: invert = (x / 3 + y / 2) % 2 == 0; break;
                    case 5: invert = x * y % 2 + x * y % 3 == 0; break;
                    case 6: invert = (x * y % 2 + x * y % 3) % 2 == 0; break;
                    case 7: invert = ((x + y) % 2 + x * y % 3) % 2 == 0; break;
                    default: break;
                }
                if (invert)
                    modules[idx] ^= 1;
            }
        }
    }

    static int finderPenaltyCountPatterns(const int runHistory[7], int size)
    {
        int n = runHistory[1];
        bool core = n > 0 && runHistory[2] == n && runHistory[3] == n * 3 &&
                    runHistory[4] == n && runHistory[5] == n;
        int result = 0;
        if (core && runHistory[0] >= n * 4 && runHistory[6] >= n) result++;
        if (core && runHistory[6] >= n * 4 && runHistory[0] >= n) result++;
        (void)size;
        return result;
    }

    static int finderPenaltyTerminateAndCount(bool currentRunColor, int currentRunLength,
                                              int runHistory[7], int size)
    {
        if (currentRunColor)
        {
            for (int i = 6; i > 0; i--) runHistory[i] = runHistory[i - 1];
            runHistory[0] = currentRunLength;
            currentRunLength = 0;
        }
        currentRunLength += size;
        for (int i = 6; i > 0; i--) runHistory[i] = runHistory[i - 1];
        runHistory[0] = currentRunLength;
        return finderPenaltyCountPatterns(runHistory, size);
    }

    static void finderPenaltyAddHistory(int currentRunLength, int runHistory[7])
    {
        for (int i = 6; i > 0; i--) runHistory[i] = runHistory[i - 1];
        runHistory[0] = currentRunLength;
    }

    int getPenaltyScore() const
    {
        int result = 0;

        // Adjacent modules in row having same color, and finder-like patterns
        for (int y = 0; y < size; y++)
        {
            bool runColor = false;
            int runX = 0;
            int runHistory[7] = {};
            for (int x = 0; x < size; x++)
            {
                if (module(x, y) == runColor)
                {
                    runX++;
                    if (runX == 5)
                        result += PENALTY_N1;
                    else if (runX > 5)
                        result++;
                }
                else
                {
                    finderPenaltyAddHistory(runX, runHistory);
                    if (!runColor)
                        result += finderPenaltyCountPatterns(runHistory, size) * PENALTY_N3;
                    runColor = module(x, y);
                    runX = 1;
                }
            }
            result += finderPenaltyTerminateAndCount(runColor, runX, runHistory, size) * PENALTY_N3;
        }

        // Adjacent modules in column having same color, and finder-like patterns
        for (int x = 0; x < size; x++)
        {
            bool runColor = false;
            int runY = 0;
            int runHistory[7] = {};
            for (int y = 0; y < size; y++)
            {
                if (module(x, y) == runColor)
                {
                    runY++;
                    if (runY == 5)
                        result += PENALTY_N1;
                    else if (runY > 5)
                        result++;
                }
                else
                {
                    finderPenaltyAddHistory(runY, runHistory);
                    if (!runColor)
                        result += finderPenaltyCountPatterns(runHistory, size) * PENALTY_N3;
                    runColor = module(x, y);
                    runY = 1;
                }
            }
            result += finderPenaltyTerminateAndCount(runColor, runY, runHistory, size) * PENALTY_N3;
        }

        // 2x2 blocks of modules having same color
        for (int y = 0; y < size - 1; y++)
        {
            for (int x = 0; x < size - 1; x++)
            {
                bool color = module(x, y);
                if (color == module(x + 1, y) &&
                    color == module(x, y + 1) &&
                    color == module(x + 1, y + 1))
                {
                    result += PENALTY_N2;
                }
            }
        }

        // Balance of dark and light modules
        int dark = 0;
        for (uint8_t m : modules)
            if (m != 0) dark++;
        int total = size * size;
        int k = (std::abs(dark * 20 - total * 10) + total - 1) / total - 1;
        if (k > 0)
            result += k * PENALTY_N4;

        return result;
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
    return m_modules[static_cast<size_t>(y * m_size + x)] != 0;
}

QrCode QrCode::encodeText(const char* text, Ecc ecl)
{
    size_t textLen = text ? std::strlen(text) : 0;

    int version = MIN_VERSION;
    for (; version <= MAX_VERSION; version++)
    {
        int countBits = (version <= 9) ? 8 : 16;
        size_t totalBits = 4 + static_cast<size_t>(countBits) + textLen * 8;
        size_t capBits   = static_cast<size_t>(getNumDataCodewords(version, ecl)) * 8;
        if (totalBits <= capBits)
            break;
    }
    if (version > MAX_VERSION)
        version = MAX_VERSION;

    int dataCapacityCodewords = getNumDataCodewords(version, ecl);
    int dataCapacityBits      = dataCapacityCodewords * 8;
    int charCountBits         = (version <= 9) ? 8 : 16;
    size_t maxBytes           = static_cast<size_t>((dataCapacityBits - 4 - charCountBits) / 8);
    if (textLen > maxBytes)
        textLen = maxBytes;

    BitBuffer bb;
    bb.appendBits(0x4, 4); // Byte mode
    bb.appendBits(static_cast<uint32_t>(textLen), charCountBits);
    for (size_t i = 0; i < textLen; i++)
        bb.appendBits(static_cast<uint8_t>(text[i]), 8);

    // Terminator + byte boundary padding
    int terminatorBits = std::min(4, dataCapacityBits - bb.bitLength);
    bb.appendBits(0, terminatorBits);
    if (bb.bitLength % 8 != 0)
        bb.appendBits(0, 8 - (bb.bitLength % 8));

    // Pad codewords: strictly alternating 0xEC, 0x11 starting with 0xEC
    for (uint8_t padByte = 0xEC; bb.bytes.size() < static_cast<size_t>(dataCapacityCodewords); padByte ^= (0xEC ^ 0x11))
        bb.bytes.push_back(padByte);

    // Split into blocks and compute Reed-Solomon error correction
    int eclIdx         = static_cast<int>(ecl);
    int numBlocks      = NUM_ERROR_CORRECTION_BLOCKS[eclIdx][version];
    int blockEcLen     = ECC_CODEWORDS_PER_BLOCK[eclIdx][version];
    int rawCodewords   = getNumRawDataModules(version) / 8;
    int numShortBlocks = numBlocks - rawCodewords % numBlocks;
    int shortBlockLen  = rawCodewords / numBlocks;

    std::vector<std::vector<uint8_t>> blocks(static_cast<size_t>(numBlocks));
    std::vector<uint8_t> rsDiv = reedSolomonComputeDivisor(blockEcLen);

    size_t k = 0;
    for (int i = 0; i < numBlocks; i++)
    {
        int datLen = shortBlockLen - blockEcLen + (i < numShortBlocks ? 0 : 1);
        std::vector<uint8_t> dat(bb.bytes.begin() + static_cast<ptrdiff_t>(k),
                                 bb.bytes.begin() + static_cast<ptrdiff_t>(k + static_cast<size_t>(datLen)));
        k += static_cast<size_t>(datLen);
        std::vector<uint8_t> ecc = reedSolomonComputeRemainder(dat, rsDiv);
        if (i < numShortBlocks)
            dat.push_back(0);
        dat.insert(dat.end(), ecc.begin(), ecc.end());
        blocks[static_cast<size_t>(i)] = std::move(dat);
    }

    // Interleave codewords
    std::vector<uint8_t> allCodewords;
    allCodewords.reserve(static_cast<size_t>(rawCodewords));
    for (size_t i = 0; i < blocks[0].size(); i++)
    {
        for (int j = 0; j < numBlocks; j++)
        {
            if (i != static_cast<size_t>(shortBlockLen - blockEcLen) || j >= numShortBlocks)
                allCodewords.push_back(blocks[static_cast<size_t>(j)][i]);
        }
    }

    QrBuilder qr(version, ecl);
    qr.drawCodewords(allCodewords);

    // Pick mask pattern with minimum ISO penalty score
    int minPenalty = INT_MAX;
    int bestMask   = 0;
    for (int mask = 0; mask < 8; mask++)
    {
        qr.applyMask(mask);
        qr.drawFormatBits(mask);
        int penalty = qr.getPenaltyScore();
        if (penalty < minPenalty)
        {
            minPenalty = penalty;
            bestMask   = mask;
        }
        qr.applyMask(mask); // Undo mask
    }

    qr.applyMask(bestMask);
    qr.drawFormatBits(bestMask);

    return QrCode(qr.size, std::move(qr.modules));
}

} // namespace qrcodegen
