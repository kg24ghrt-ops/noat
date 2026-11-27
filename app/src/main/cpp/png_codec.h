#pragma once

#include <cstddef>
#include <cstdint>
#include <vector>

namespace noat {

struct PngImage {
    uint32_t width = 0;
    uint32_t height = 0;
    std::vector<uint8_t> rgba;
};

std::vector<uint8_t> encodePng(uint32_t width, uint32_t height, const uint8_t* rgba);
PngImage decodePng(const uint8_t* data, std::size_t size);

}
