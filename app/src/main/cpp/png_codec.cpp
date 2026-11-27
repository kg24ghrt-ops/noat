#include "png_codec.h"

#include <zlib.h>

#include <cstring>
#include <stdexcept>
#include <vector>

namespace noat {

namespace {

constexpr std::size_t kPngSignatureSize = 8;
constexpr uint8_t kPngSignature[kPngSignatureSize] = {0x89, 0x50, 0x4e, 0x47, 0x0d, 0x0a, 0x1a, 0x0a};

void writeBe32(std::vector<uint8_t>& out, uint32_t value) {
    out.push_back(static_cast<uint8_t>((value >> 24) & 0xff));
    out.push_back(static_cast<uint8_t>((value >> 16) & 0xff));
    out.push_back(static_cast<uint8_t>((value >> 8) & 0xff));
    out.push_back(static_cast<uint8_t>(value & 0xff));
}

uint32_t readBe32(const uint8_t* data) {
    return (static_cast<uint32_t>(data[0]) << 24) |
           (static_cast<uint32_t>(data[1]) << 16) |
           (static_cast<uint32_t>(data[2]) << 8) |
           static_cast<uint32_t>(data[3]);
}

void writeChunk(std::vector<uint8_t>& out, const char type[4], const uint8_t* data, std::size_t length) {
    writeBe32(out, static_cast<uint32_t>(length));
    out.insert(out.end(), type, type + 4);
    out.insert(out.end(), data, data + length);
    uint32_t crc = crc32(0L, Z_NULL, 0);
    crc = crc32(crc, reinterpret_cast<const Bytef*>(type), 4);
    crc = crc32(crc, reinterpret_cast<const Bytef*>(data), static_cast<uInt>(length));
    writeBe32(out, crc);
}

} // namespace

std::vector<uint8_t> encodePng(uint32_t width, uint32_t height, const uint8_t* rgba) {
    std::vector<uint8_t> out;
    out.insert(out.end(), kPngSignature, kPngSignature + kPngSignatureSize);

    uint8_t ihdr[13];
    ihdr[0] = static_cast<uint8_t>((width >> 24) & 0xff);
    ihdr[1] = static_cast<uint8_t>((width >> 16) & 0xff);
    ihdr[2] = static_cast<uint8_t>((width >> 8) & 0xff);
    ihdr[3] = static_cast<uint8_t>(width & 0xff);
    ihdr[4] = static_cast<uint8_t>((height >> 24) & 0xff);
    ihdr[5] = static_cast<uint8_t>((height >> 16) & 0xff);
    ihdr[6] = static_cast<uint8_t>((height >> 8) & 0xff);
    ihdr[7] = static_cast<uint8_t>(height & 0xff);
    ihdr[8] = 8;   // bit depth
    ihdr[9] = 6;   // color type RGBA
    ihdr[10] = 0;  // compression
    ihdr[11] = 0;  // filter
    ihdr[12] = 0;  // interlace
    const char ihdrType[4] = {'I', 'H', 'D', 'R'};
    writeChunk(out, ihdrType, ihdr, sizeof(ihdr));

    const std::size_t rowBytes = static_cast<std::size_t>(width) * 4u;
    const std::size_t scanlineBytes = rowBytes + 1u;
    std::vector<uint8_t> raw;
    raw.reserve(static_cast<std::size_t>(height) * scanlineBytes);
    for (uint32_t y = 0; y < height; ++y) {
        raw.push_back(0); // filter: none
        const uint8_t* row = rgba + static_cast<std::size_t>(y) * rowBytes;
        raw.insert(raw.end(), row, row + rowBytes);
    }

    uLongf bound = compressBound(static_cast<uLong>(raw.size()));
    std::vector<uint8_t> compressed(bound);
    uLongf compressedSize = bound;
    int result = compress2(compressed.data(), &compressedSize,
                           reinterpret_cast<const Bytef*>(raw.data()), static_cast<uLong>(raw.size()),
                           Z_DEFAULT_COMPRESSION);
    if (result != Z_OK) {
        throw std::runtime_error("PNG compression failed");
    }
    compressed.resize(static_cast<std::size_t>(compressedSize));

    const char idatType[4] = {'I', 'D', 'A', 'T'};
    writeChunk(out, idatType, compressed.data(), compressed.size());

    const char iendType[4] = {'I', 'E', 'N', 'D'};
    writeChunk(out, iendType, nullptr, 0);

    return out;
}

PngImage decodePng(const uint8_t* data, std::size_t size) {
    PngImage image;
    if (size < kPngSignatureSize ||
        std::memcmp(data, kPngSignature, kPngSignatureSize) != 0) {
        throw std::runtime_error("Not a PNG file");
    }

    std::size_t offset = kPngSignatureSize;
    uint32_t width = 0;
    uint32_t height = 0;
    std::vector<uint8_t> idat;
    bool sawIhdr = false;
    bool sawIend = false;

    while (offset < size && !sawIend) {
        if (offset + 8 > size) {
            throw std::runtime_error("Truncated PNG chunk header");
        }
        uint32_t length = readBe32(data + offset);
        const char* type = reinterpret_cast<const char*>(data + offset + 4);
        offset += 8;
        if (offset + length + 4 > size) {
            throw std::runtime_error("Truncated PNG chunk data");
        }
        const uint8_t* chunkData = data + offset;
        uint32_t crc = crc32(0L, Z_NULL, 0);
        crc = crc32(crc, reinterpret_cast<const Bytef*>(type), 4);
        crc = crc32(crc, chunkData, static_cast<uInt>(length));
        uint32_t expectedCrc = readBe32(data + offset + length);
        if (crc != expectedCrc) {
            throw std::runtime_error("PNG CRC mismatch");
        }
        offset += length + 4;

        if (std::memcmp(type, "IHDR", 4) == 0) {
            if (length < 13) {
                throw std::runtime_error("Invalid IHDR");
            }
            width = readBe32(chunkData);
            height = readBe32(chunkData + 4);
            uint8_t bitDepth = chunkData[8];
            uint8_t colorType = chunkData[9];
            if (bitDepth != 8 || colorType != 6) {
                throw std::runtime_error("Only 8-bit RGBA PNG supported");
            }
            sawIhdr = true;
        } else if (std::memcmp(type, "IDAT", 4) == 0) {
            idat.insert(idat.end(), chunkData, chunkData + length);
        } else if (std::memcmp(type, "IEND", 4) == 0) {
            sawIend = true;
        }
    }

    if (!sawIhdr || idat.empty()) {
        throw std::runtime_error("PNG missing image data");
    }

    uLongf rawBound = static_cast<uLongf>(static_cast<uint64_t>(height) * (static_cast<uint64_t>(width) * 4u + 1u) * 2u);
    std::vector<uint8_t> raw(rawBound);
    uLongf rawSize = rawBound;
    int result = uncompress(raw.data(), &rawSize,
                            reinterpret_cast<const Bytef*>(idat.data()), static_cast<uLong>(idat.size()));
    if (result != Z_OK) {
        throw std::runtime_error("PNG decompression failed");
    }
    raw.resize(static_cast<std::size_t>(rawSize));

    const std::size_t rowBytes = static_cast<std::size_t>(width) * 4u;
    const std::size_t scanlineBytes = rowBytes + 1u;
    if (raw.size() < static_cast<std::size_t>(height) * scanlineBytes) {
        throw std::runtime_error("PNG scanline data too short");
    }

    image.width = width;
    image.height = height;
    image.rgba.resize(static_cast<std::size_t>(width) * static_cast<std::size_t>(height) * 4u);
    for (uint32_t y = 0; y < height; ++y) {
        const uint8_t* row = raw.data() + static_cast<std::size_t>(y) * scanlineBytes;
        if (row[0] != 0) {
            throw std::runtime_error("Unsupported PNG filter");
        }
        std::memcpy(image.rgba.data() + static_cast<std::size_t>(y) * rowBytes, row + 1, rowBytes);
    }

    return image;
}

} // namespace noat
