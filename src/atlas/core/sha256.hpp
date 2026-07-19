#pragma once

#include <cstdint>
#include <filesystem>
#include <span>
#include <string>

namespace vulkax::atlas {

std::string sha256(std::span<const uint8_t> bytes);
std::string sha256File(const std::filesystem::path& path);

}  // namespace vulkax::atlas
