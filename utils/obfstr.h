#pragma once
#include <array>
#include <cstddef>
#include <string>
#include <utility>

namespace obf {
inline constexpr unsigned char KEY = 0x5A;
inline volatile unsigned char keyRead = KEY; //a volatile read: the compiler cannot fold the decoding into plain text

template <std::size_t N, std::size_t... I>
constexpr std::array<unsigned char, N> encode(const char (&s)[N], std::index_sequence<I...>) {
    return {static_cast<unsigned char>(static_cast<unsigned char>(s[I]) ^ static_cast<unsigned char>(KEY + I))...};
}

template <std::size_t N>
std::string decode(const std::array<unsigned char, N>& bytes) {
    std::string out(N - 1, '\0');
    for (std::size_t i = 0; i + 1 < N; i++) out[i] = static_cast<char>(bytes[i] ^ static_cast<unsigned char>(keyRead + i));
    return out;
}
} // namespace obf

#define OBF(literal) ([] { constexpr auto enc_ = obf::encode(literal, std::make_index_sequence<sizeof(literal)>{}); return obf::decode(enc_); }())
