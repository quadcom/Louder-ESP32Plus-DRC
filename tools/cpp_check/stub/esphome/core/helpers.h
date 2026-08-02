#pragma once
#include <cstdint>
#include <type_traits>

// Minimal stand-in for esphome/core/helpers.h, just the pieces
// tas58xx_helpers.cpp uses.
namespace esphome {

template<typename T> constexpr T byteswap(T n) {
  using U = typename std::make_unsigned<T>::type;
  U v = static_cast<U>(n);
  U r = 0;
  for (std::size_t i = 0; i < sizeof(U); i++) {
    r = static_cast<U>((r << 8) | (v & 0xFF));
    v = static_cast<U>(v >> 8);
  }
  return static_cast<T>(r);
}

}  // namespace esphome
