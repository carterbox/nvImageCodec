/*
 * SPDX-FileCopyrightText: Copyright (c) 2019-2023 NVIDIA CORPORATION & AFFILIATES. All rights reserved.
 * SPDX-License-Identifier: Apache-2.0
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 * http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

#pragma once

#include <cuda_runtime.h>
#include <cstdint>
#include <cmath>
#include <limits>
#include <type_traits>
#include "host_dev.h"

namespace nvimgcodec {

template <typename T>
struct const_limits;

// std::numeric_limits are not compatible with CUDA
template <typename T>
NVIMGCODEC_HOST_DEV constexpr T max_value() {
  return const_limits<std::remove_cv_t<T>>::max;
}
template <typename T>
NVIMGCODEC_HOST_DEV constexpr T min_value() {
  return const_limits<std::remove_cv_t<T>>::min;
}

#define DEFINE_TYPE_RANGE(type, min_, max_) template <>\
struct const_limits<type> { static constexpr type min = min_, max = max_; }

DEFINE_TYPE_RANGE(bool, false, true);
DEFINE_TYPE_RANGE(uint8_t,  0, 0xff);
DEFINE_TYPE_RANGE(int8_t,  -0x80, 0x7f);
DEFINE_TYPE_RANGE(uint16_t, 0, 0xffff);
DEFINE_TYPE_RANGE(int16_t, -0x8000, 0x7fff);
DEFINE_TYPE_RANGE(uint32_t, 0, 0xffffffff);
DEFINE_TYPE_RANGE(int32_t, -0x80000000, 0x7fffffff);
DEFINE_TYPE_RANGE(uint64_t, 0, 0xffffffffffffffffUL);
DEFINE_TYPE_RANGE(int64_t, -0x8000000000000000L, 0x7fffffffffffffffL);
DEFINE_TYPE_RANGE(float, -3.40282347e+38f, 3.40282347e+38f);
DEFINE_TYPE_RANGE(double, -1.7976931348623157e+308, 1.7976931348623157e+308);

template <typename From, typename To>
struct needs_clamp {
  static constexpr bool from_fp = std::is_floating_point<From>::value;
  static constexpr bool to_fp = std::is_floating_point<To>::value;
  static constexpr bool from_unsigned = std::is_unsigned<From>::value;
  static constexpr bool to_unsigned = std::is_unsigned<To>::value;

  static constexpr bool value =
    // to smaller type of same kind (fp, int)
    (from_fp == to_fp && sizeof(To) < sizeof(From)) ||
    // fp32 has range in excess of (u)int64
    (from_fp && !to_fp) ||
    // converting to unsigned requires clamping negatives to zero
    (!from_unsigned && to_unsigned) ||
    // zero-extending signed unsigned integers requires more bits
    (from_unsigned && !to_unsigned && sizeof(To) <= sizeof(From));
};

template <typename To>
struct needs_clamp<bool, To> {
  static constexpr bool value = false;
};

template <typename T>
struct ret_type {  // a placeholder for return type
  constexpr ret_type() = default;
};

template <typename T, typename U>
NVIMGCODEC_HOST_DEV constexpr std::enable_if_t<
    needs_clamp<U, T>::value && std::is_signed<U>::value && std::is_signed<T>::value,
    T>
clamp(U value, ret_type<T>) {
  return value <= static_cast<U>(min_value<T>()) ? min_value<T>() :
         value >= static_cast<U>(max_value<T>()) ? max_value<T>() :
         static_cast<T>(value);
}

template <typename T, typename U>
NVIMGCODEC_HOST_DEV constexpr std::enable_if_t<
    needs_clamp<U, T>::value && std::is_signed<U>::value && std::is_floating_point<U>::value
        && std::is_unsigned<T>::value,
    T>
clamp(U value, ret_type<T>) {
  return value <= static_cast<U>(min_value<T>()) ? min_value<T>() :
         value >= static_cast<U>(max_value<T>()) ? max_value<T>() :
         static_cast<T>(value);
}

template <typename T, typename U>
NVIMGCODEC_HOST_DEV constexpr std::enable_if_t<
    needs_clamp<U, T>::value && std::is_signed<U>::value && std::is_integral<U>::value
        && std::is_unsigned<T>::value,
    T>
clamp(U value, ret_type<T>) {
  return value <= 0 ? 0 :
         static_cast<std::make_unsigned_t<U>>(value) >= max_value<T>() ? max_value<T>() :
         static_cast<T>(value);
}


template <typename T, typename U>
NVIMGCODEC_HOST_DEV constexpr std::enable_if_t<
    needs_clamp<U, T>::value && std::is_unsigned<U>::value,
    T>
clamp(U value, ret_type<T>) {
  // Note: make the right hand side of the comparison unsigned, silencing warnings
  // Add U(0) - adding simply 0u is not enough if T is larger than `unsigned`.
  // U is guaranteed to be at least as large as T or the clamping wouldn't be necessary
  // and this function would have been disabled by `needs_clamp`.
  return value >= max_value<T>() + U(0) ? max_value<T>() : static_cast<T>(value);
}

template <typename T, typename U>
NVIMGCODEC_HOST_DEV constexpr std::enable_if_t<!needs_clamp<U, T>::value, T>
clamp(U value, ret_type<T>) { return value; }

NVIMGCODEC_HOST_DEV constexpr int32_t clamp(uint32_t value, ret_type<int32_t>) {
  return value & 0x80000000u ? 0x7fffffff : value;
}

NVIMGCODEC_HOST_DEV constexpr uint32_t clamp(int32_t value, ret_type<uint32_t>) {
  return value < 0 ? 0u : value;
}

NVIMGCODEC_HOST_DEV constexpr int32_t clamp(int64_t value, ret_type<int32_t>) {
  return value < static_cast<int64_t>(min_value<int32_t>()) ? min_value<int32_t>() :
         value > static_cast<int64_t>(max_value<int32_t>()) ? max_value<int32_t>() :
         static_cast<int32_t>(value);
}

template <>
NVIMGCODEC_HOST_DEV constexpr int32_t clamp(uint64_t value, ret_type<int32_t>) {
  return value > static_cast<uint64_t>(max_value<int32_t>()) ? max_value<int32_t>() :
         static_cast<int32_t>(value);
}

template <>
NVIMGCODEC_HOST_DEV constexpr uint32_t clamp(int64_t value, ret_type<uint32_t>) {
  return value < 0 ? 0 :
         value > static_cast<int64_t>(max_value<uint32_t>()) ? max_value<uint32_t>() :
         static_cast<uint32_t>(value);
}

template <>
NVIMGCODEC_HOST_DEV constexpr uint32_t clamp(uint64_t value, ret_type<uint32_t>) {
  return value > static_cast<uint64_t>(max_value<uint32_t>()) ? max_value<uint32_t>() :
         static_cast<uint32_t>(value);
}

template <typename T>
NVIMGCODEC_HOST_DEV constexpr bool clamp(T value, ret_type<bool>) {
  return static_cast<bool>(value);
}

template <typename T, typename U>
NVIMGCODEC_HOST_DEV constexpr T clamp(U value) {
  return clamp(value, ret_type<T>());
}

namespace detail {
#ifdef __CUDA_ARCH__

inline __device__ int cuda_round_helper(float f, int) {  // NOLINT
  return __float2int_rn(f);
}
inline __device__ unsigned cuda_round_helper(float f, unsigned) {  // NOLINT
  return __float2uint_rn(f);
}
inline __device__ long long  cuda_round_helper(float f, long long) {  // NOLINT
  return __float2ll_rd(f+0.5f);
}
inline __device__ unsigned long long cuda_round_helper(float f, unsigned long long) {  // NOLINT
  return __float2ull_rd(f+0.5f);
}
inline __device__ long cuda_round_helper(float f, long) {  // NOLINT
  return sizeof(long) == sizeof(int) ? __float2int_rn(f) : __float2ll_rd(f+0.5f);  // NOLINT
}
inline __device__ unsigned long cuda_round_helper(float f, unsigned long) {  // NOLINT
  return sizeof(unsigned long) == sizeof(unsigned int) ? __float2uint_rn(f) : __float2ull_rd(f+0.5f);  // NOLINT
}
inline __device__ int cuda_round_helper(double f, int) {  // NOLINT
  return __double2int_rn(f);
}
inline __device__ unsigned cuda_round_helper(double f, unsigned) {  // NOLINT
  return __double2uint_rn(f);
}
inline __device__ long long  cuda_round_helper(double f, long long) {  // NOLINT
  return __double2ll_rd(f+0.5f);
}
inline __device__ unsigned long long cuda_round_helper(double f, unsigned long long) {  // NOLINT
  return __double2ull_rd(f+0.5f);
}
inline __device__ long cuda_round_helper(double f, long) {  // NOLINT
  return sizeof(long) == sizeof(int) ? __double2int_rn(f) : __double2ll_rd(f+0.5f);  // NOLINT
}
inline __device__ unsigned long cuda_round_helper(double f, unsigned long) {  // NOLINT
  return sizeof(unsigned long) == sizeof(unsigned int) ? __double2uint_rn(f) : __double2ull_rd(f+0.5f);  // NOLINT
}
#endif

template <typename Out, typename In,
  bool OutIsFP = std::is_floating_point<Out>::value,
  bool InIsFP = std::is_floating_point<In>::value>
struct ConverterBase;

template <typename Out, typename In>
struct Converter : ConverterBase<Out, In> {
  static_assert(std::is_arithmetic<Out>::value && std::is_arithmetic<In>::value,
    "Default ConverterBase can only be used with arithmetic types. For custom types, "
    "specialize or overload nvimgcodec::Convert");
};

/// Converts between two FP types
template <typename Out, typename In>
struct ConverterBase<Out, In, true, true> {
  NVIMGCODEC_HOST_DEV
  static constexpr Out Convert(In value) { return value; }
  NVIMGCODEC_HOST_DEV
  static constexpr Out ConvertNorm(In value) { return value; }
  NVIMGCODEC_HOST_DEV
  static constexpr Out ConvertSat(In value) { return value; }
  NVIMGCODEC_HOST_DEV
  static constexpr Out ConvertSatNorm(In value) { return value; }
};

/// Converts integral to FP type
template <typename Out, typename In>
struct ConverterBase<Out, In, true, false> {
  NVIMGCODEC_HOST_DEV
  static constexpr Out Convert(In value) { return value; }
  NVIMGCODEC_HOST_DEV
  static constexpr Out ConvertSat(In value) { return value; }

  NVIMGCODEC_HOST_DEV
  static constexpr Out ConvertNorm(In value) { return value * (Out(1) / (max_value<In>())); }
  NVIMGCODEC_HOST_DEV
  static constexpr Out ConvertSatNorm(In value) { return value * (Out(1) / (max_value<In>())); }
};

/// Converts FP to integral type
template <typename Out, typename In>
struct ConverterBase<Out, In, false, true> {
  NVIMGCODEC_HOST_DEV
  static constexpr Out Convert(In value) {
#ifdef __CUDA_ARCH__
  return clamp<Out>(detail::cuda_round_helper(value, Out()));
#else
  return clamp<Out>(std::round(value));
#endif
  }

  NVIMGCODEC_HOST_DEV
  static constexpr Out ConvertSat(In value) {
#ifdef __CUDA_ARCH__
  return clamp<Out>(detail::cuda_round_helper(value, Out()));
#else
  return clamp<Out>(std::round(value));
#endif
  }

  NVIMGCODEC_HOST_DEV
  static constexpr Out ConvertNorm(In value) {
#ifdef __CUDA_ARCH__
    return detail::cuda_round_helper(value * max_value<Out>(), Out());
#else
    return std::round(value * max_value<Out>());
#endif
  }

  NVIMGCODEC_HOST_DEV
  static constexpr Out ConvertSatNorm(In value) {
#ifdef __CUDA_ARCH__
    return std::is_signed<Out>::value
      ? clamp<Out>(detail::cuda_round_helper(value * max_value<Out>(), Out()))
      : detail::cuda_round_helper(max_value<Out>() * __saturatef(value), Out());
#else
    return clamp<Out>(std::round(value * static_cast<In>(max_value<Out>())));
#endif
  }
};

/// Converts signed to signed, unsigned to unsigned
template <typename Out, typename In,
          bool IsOutSigned = std::is_signed<Out>::value,
          bool IsInSigned = std::is_signed<In>::value>
struct ConvertIntInt {
  NVIMGCODEC_HOST_DEV
  static constexpr Out Convert(In value) { return value; }
  NVIMGCODEC_HOST_DEV
  static constexpr Out ConvertNorm(In value) {
    return Converter<Out, float>::Convert(value * (1.0f * max_value<Out>() / max_value<In>()));
  }
  NVIMGCODEC_HOST_DEV
  static constexpr Out ConvertSat(In value) { return clamp<Out>(value); }
  NVIMGCODEC_HOST_DEV
  static constexpr Out ConvertSatNorm(In value) {
    return ConvertNorm(value);
  }
};

/// Converts signed to unsigned integer
template <typename Out, typename In>
struct ConvertIntInt<Out, In, false, true> {
  NVIMGCODEC_HOST_DEV
  static constexpr Out Convert(In value) { return value; }
  NVIMGCODEC_HOST_DEV
  static constexpr Out ConvertNorm(In value) {
    float value_norm_f = 0.5f * (1.0f + Converter<float, In>::ConvertNorm(value));
    return Converter<Out, float>::ConvertNorm(value_norm_f);
  }
  NVIMGCODEC_HOST_DEV
  static constexpr Out ConvertSat(In value) { return clamp<Out>(value); }
  NVIMGCODEC_HOST_DEV
  static constexpr Out ConvertSatNorm(In value) {
    float value_norm_f = 0.5f * (1.0f + Converter<float, In>::ConvertSatNorm(value));
    return Converter<Out, float>::ConvertSatNorm(value_norm_f);
  }
};

/// Converts unsigned to signed integer
template <typename Out, typename In>
struct ConvertIntInt<Out, In, true, false> {
  NVIMGCODEC_HOST_DEV
  static constexpr Out Convert(In value) { return value; }
  NVIMGCODEC_HOST_DEV
  static constexpr Out ConvertNorm(In value) {
    float value_f = -1.0f + 2.0f * Converter<float, In>::ConvertNorm(value);
    return Converter<Out, float>::ConvertNorm(value_f);
  }
  NVIMGCODEC_HOST_DEV
  static constexpr Out ConvertSat(In value) { return clamp<Out>(value); }
  NVIMGCODEC_HOST_DEV
  static constexpr Out ConvertSatNorm(In value) {
    float value_f = -1.0f + 2.0f * Converter<float, In>::ConvertSatNorm(value);
    return Converter<Out, float>::ConvertSatNorm(value_f);
  }
};

/// Converts between integral types
template <typename Out, typename In>
struct ConverterBase<Out, In, false, false> : ConvertIntInt<Out, In> {
  static_assert(std::is_arithmetic<Out>::value && std::is_arithmetic<In>::value,
    "Default ConverterBase can only be used with arithmetic types. For custom types, "
    "specialize or overload nvimgcodec::Convert");
};

/// Pass-through conversion
template <typename T>
struct Converter<T, T> {
  static NVIMGCODEC_HOST_DEV
  constexpr T Convert(T value) { return value; }

  static NVIMGCODEC_HOST_DEV
  constexpr T ConvertSat(T value) { return value; }

    static NVIMGCODEC_HOST_DEV
  constexpr T ConvertNorm(T value) { return value; }

  static NVIMGCODEC_HOST_DEV
  constexpr T ConvertSatNorm(T value) { return value; }
};

template <typename raw_out, typename raw_in>
using converter_t = Converter<
  std::remove_cv_t<raw_out>,
  std::remove_cv_t<std::remove_reference_t<raw_in>>>;;

}  // namespace detail

/// @brief Converts value to a specified `Out` type, rounding if necessary
///
/// Usage:
/// ```
///   Convert<uint8_t>(100.2f);   // == 100
///   Convert<uint8_t>(100.7f);   // == 101
///   Convert<uint8_t>(-5);       // usage discouraged, typically 250
///   Convert<uint8_t>(-5.0f);    // undefined
///   Convert<uint8_t>(100.0.0f); // undefined
/// ```
template <typename Out, typename In>
NVIMGCODEC_HOST_DEV constexpr Out Convert(In value) {
  return detail::converter_t<Out, In>::Convert(value);
}

/// @brief Converts value from `In` to `Out` keeping whole (positive) dynamic range
///
///   * When converting from signed to unsigned types, negative values produce undefined result
///   * When converting from floating point to integral types, the value is multiplied by
///     `max_value<Out>()`. Results are undefined for values where `value * max_value<Out>()`
///     cannot be represented by `Out`.
///   * When converting from integral type to floating point, the input value is normalized by
///     multiplying by reciprocal of `Out` type's maximum positive value.
///
/// Usage:
/// ```
///   ConvertNorm<uint8_t>(0.5f);               // == 127
///   ConvertNorm<uint8_t>(0.502);              // == 128
///   ConvertNorm<int8_t>(-1.0f);               // -127
///   ConvertNorm<int8_t, int16_t>(256 * 1/3);  // == 0
///   ConvertNorm<int8_t, int16_t>(256 * 2/3);  // == 1
///   ConvertNorm<int8_t, int16_t>(0x7fff);     // == 255
///   ConvertNorm<float, uint8_t>(255);         // == 1.0f
///   ConvertNorm<uint8_t, int8_t>(-1);         // undefined
///   ConvertNorm<uint8_t>(1000.0f);            // undefined
/// ```
template <typename Out, typename In>
NVIMGCODEC_HOST_DEV constexpr Out ConvertNorm(In value) {
  return detail::converter_t<Out, In>::ConvertNorm(value);
}

/// @brief Converts value to a specified `Out` type, rounding and clamping if necessary
///
/// Usage:
/// ```
///   ConvertSat<uint8_t>(-1);          // == 0
///   ConvertSat<uint8_t>(1000);        // == 255
///   ConvertSat<int8_t>(-1000.0f);     // == -128
///   ConvertSat<unsigned>(-1000.0f);   // == 0
/// ```
template <typename Out, typename In>
NVIMGCODEC_HOST_DEV constexpr Out ConvertSat(In value) {
  return detail::converter_t<Out, In>::ConvertSat(value);
}

/// @brief Converts value from `In` to `Out` keeping whole (positive) dynamic range and clamping
///
///   * When converting from signed to unsigned types, negative values produce 0
///   * When converting from floating point to integral types, the value is multiplied
///     by `max_value<Out>()` and then clamped to range representable in `Out`.
///   * When converting from integral type to floating point, the input value is divided
///     by `Out(max_value<In>())`
///
/// Usage:
/// ```
///   ConvertSatNorm<uint8_t>(0.5f);              // == 127
///   ConvertSatNorm<uint8_t>(0.502);             // == 128
///   ConvertSatNorm<int8_t>(-1.0f);              // == -127
///   ConvertSatNorm<int8_t, int16_t>(256 * 1/3); // == 0
///   ConvertSatNorm<int8_t, int16_t>(256 * 2/3); // == 1
///   ConvertSatNorm<int8_t, int16_t>(0x7fff);    // == 255
///   ConvertSatNorm<float, uint8_t>(255);        // == 1.0f
///   ConvertSatNorm<uint8_t, int8_t>(-1);        // == 0
/// ```
template <typename Out, typename In>
NVIMGCODEC_HOST_DEV constexpr Out ConvertSatNorm(In value) {
  return detail::converter_t<Out, In>::ConvertSatNorm(value);
}

}  // namespace nvimgcodec