#include "atlas/core/sha256.hpp"

#include <array>
#include <fstream>
#include <iomanip>
#include <sstream>
#include <stdexcept>
#include <vector>

namespace vulkax::atlas {
namespace {

constexpr std::array<uint32_t, 64> roundConstants{
    0x428a2f98u, 0x71374491u, 0xb5c0fbcfu, 0xe9b5dba5u,
    0x3956c25bu, 0x59f111f1u, 0x923f82a4u, 0xab1c5ed5u,
    0xd807aa98u, 0x12835b01u, 0x243185beu, 0x550c7dc3u,
    0x72be5d74u, 0x80deb1feu, 0x9bdc06a7u, 0xc19bf174u,
    0xe49b69c1u, 0xefbe4786u, 0x0fc19dc6u, 0x240ca1ccu,
    0x2de92c6fu, 0x4a7484aau, 0x5cb0a9dcu, 0x76f988dau,
    0x983e5152u, 0xa831c66du, 0xb00327c8u, 0xbf597fc7u,
    0xc6e00bf3u, 0xd5a79147u, 0x06ca6351u, 0x14292967u,
    0x27b70a85u, 0x2e1b2138u, 0x4d2c6dfcu, 0x53380d13u,
    0x650a7354u, 0x766a0abbu, 0x81c2c92eu, 0x92722c85u,
    0xa2bfe8a1u, 0xa81a664bu, 0xc24b8b70u, 0xc76c51a3u,
    0xd192e819u, 0xd6990624u, 0xf40e3585u, 0x106aa070u,
    0x19a4c116u, 0x1e376c08u, 0x2748774cu, 0x34b0bcb5u,
    0x391c0cb3u, 0x4ed8aa4au, 0x5b9cca4fu, 0x682e6ff3u,
    0x748f82eeu, 0x78a5636fu, 0x84c87814u, 0x8cc70208u,
    0x90befffau, 0xa4506cebu, 0xbef9a3f7u, 0xc67178f2u,
};

uint32_t rotateRight(uint32_t value, uint32_t count) {
  return (value >> count) | (value << (32u - count));
}

std::vector<uint8_t> padded(std::span<const uint8_t> bytes) {
  std::vector<uint8_t> message{bytes.begin(), bytes.end()};
  const uint64_t bitCount = static_cast<uint64_t>(bytes.size()) * 8u;
  message.push_back(0x80u);
  while ((message.size() + 8u) % 64u != 0u) message.push_back(0u);
  for (int shift = 56; shift >= 0; shift -= 8) {
    message.push_back(static_cast<uint8_t>(bitCount >> shift));
  }
  return message;
}

}  // namespace

std::string sha256(std::span<const uint8_t> bytes) {
  std::array<uint32_t, 8> hash{
      0x6a09e667u,
      0xbb67ae85u,
      0x3c6ef372u,
      0xa54ff53au,
      0x510e527fu,
      0x9b05688cu,
      0x1f83d9abu,
      0x5be0cd19u,
  };
  const auto message = padded(bytes);
  for (size_t offset = 0; offset < message.size(); offset += 64) {
    std::array<uint32_t, 64> words{};
    for (size_t index = 0; index < 16; ++index) {
      const size_t byte = offset + index * 4;
      words[index] =
          (static_cast<uint32_t>(message[byte]) << 24u) |
          (static_cast<uint32_t>(message[byte + 1]) << 16u) |
          (static_cast<uint32_t>(message[byte + 2]) << 8u) |
          static_cast<uint32_t>(message[byte + 3]);
    }
    for (size_t index = 16; index < words.size(); ++index) {
      const uint32_t sigma0 =
          rotateRight(words[index - 15], 7u) ^
          rotateRight(words[index - 15], 18u) ^
          (words[index - 15] >> 3u);
      const uint32_t sigma1 =
          rotateRight(words[index - 2], 17u) ^
          rotateRight(words[index - 2], 19u) ^
          (words[index - 2] >> 10u);
      words[index] =
          words[index - 16] + sigma0 + words[index - 7] + sigma1;
    }

    uint32_t a = hash[0];
    uint32_t b = hash[1];
    uint32_t c = hash[2];
    uint32_t d = hash[3];
    uint32_t e = hash[4];
    uint32_t f = hash[5];
    uint32_t g = hash[6];
    uint32_t h = hash[7];
    for (size_t index = 0; index < words.size(); ++index) {
      const uint32_t choice = (e & f) ^ (~e & g);
      const uint32_t majority = (a & b) ^ (a & c) ^ (b & c);
      const uint32_t sum0 =
          rotateRight(a, 2u) ^ rotateRight(a, 13u) ^
          rotateRight(a, 22u);
      const uint32_t sum1 =
          rotateRight(e, 6u) ^ rotateRight(e, 11u) ^
          rotateRight(e, 25u);
      const uint32_t temporary1 =
          h + sum1 + choice + roundConstants[index] + words[index];
      const uint32_t temporary2 = sum0 + majority;
      h = g;
      g = f;
      f = e;
      e = d + temporary1;
      d = c;
      c = b;
      b = a;
      a = temporary1 + temporary2;
    }
    hash[0] += a;
    hash[1] += b;
    hash[2] += c;
    hash[3] += d;
    hash[4] += e;
    hash[5] += f;
    hash[6] += g;
    hash[7] += h;
  }

  std::ostringstream output;
  output << std::hex << std::setfill('0');
  for (uint32_t value : hash) output << std::setw(8) << value;
  return output.str();
}

std::string sha256File(const std::filesystem::path& path) {
  std::ifstream input{path, std::ios::binary | std::ios::ate};
  if (!input) {
    throw std::runtime_error("failed to open file for SHA-256: " + path.string());
  }
  const auto end = input.tellg();
  if (end < 0) {
    throw std::runtime_error("failed to size file for SHA-256: " + path.string());
  }
  std::vector<uint8_t> bytes(static_cast<size_t>(end));
  input.seekg(0);
  input.read(
      reinterpret_cast<char*>(bytes.data()),
      static_cast<std::streamsize>(bytes.size()));
  if (!input && !bytes.empty()) {
    throw std::runtime_error("failed to read file for SHA-256: " + path.string());
  }
  return sha256(bytes);
}

}  // namespace vulkax::atlas
