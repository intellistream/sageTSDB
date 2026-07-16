#pragma once

/**
 * @file value_codec.h
 * @brief Reference codec for the `stsb1` value blob format (contract §3.6).
 *
 * TimeSeriesData::value is a std::variant<double, std::vector<double>>. When a
 * storage backend keeps values as an opaque blob (e.g. a DM VARBINARY/BLOB
 * column), it MUST use this encoding so that all backends round-trip a value
 * identically and query results match core::MemoryBackend.
 *
 * Byte layout `stsb1` (all little-endian):
 *   off size field    meaning
 *     0    4 magic    ASCII "STSB" (0x53 0x54 0x53 0x42)
 *     4    1 version  0x01
 *     5    1 kind     0x01 = scalar double, 0x02 = vector double[]
 *     6    2 reserved 0, for alignment
 *     8    4 count    element count (uint32); scalar => 1, vector => dim
 *    12  8*n data     `count` IEEE-754 doubles (8 bytes each, little-endian)
 *   Total length = 12 + 8*count. Scalar => 20 bytes, 52-dim vector => 428 bytes.
 *
 * Header-only so both core and the DM adapter share one implementation.
 */

#include "time_series_data.h"

#include <cstdint>
#include <cstring>
#include <stdexcept>
#include <vector>

namespace sage_tsdb {
namespace core {
namespace stsb1 {

/// Format constants.
inline constexpr uint8_t  kMagic0 = 'S', kMagic1 = 'T', kMagic2 = 'S', kMagic3 = 'B';
inline constexpr uint8_t  kVersion = 0x01;
inline constexpr uint8_t  kKindScalar = 0x01;
inline constexpr uint8_t  kKindVector = 0x02;
inline constexpr size_t   kHeaderBytes = 12;

/// Append a little-endian double to a byte buffer.
inline void putDoubleLE(std::vector<uint8_t>& out, double v) {
    uint64_t bits;
    std::memcpy(&bits, &v, sizeof(bits));
    for (int i = 0; i < 8; ++i) out.push_back(static_cast<uint8_t>((bits >> (8 * i)) & 0xFF));
}

/// Read a little-endian double from raw bytes at offset `off`.
inline double getDoubleLE(const uint8_t* p, size_t off) {
    uint64_t bits = 0;
    for (int i = 0; i < 8; ++i) bits |= static_cast<uint64_t>(p[off + i]) << (8 * i);
    double v;
    std::memcpy(&v, &bits, sizeof(v));
    return v;
}

/**
 * @brief Encode a TimeSeriesValue into an `stsb1` byte blob.
 * @param value Scalar or vector value.
 * @return Encoded bytes (length 12 + 8*count).
 */
inline std::vector<uint8_t> encode(const TimeSeriesValue& value) {
    std::vector<uint8_t> out;
    const bool is_vec = std::holds_alternative<std::vector<double>>(value);
    std::vector<double> data =
        is_vec ? std::get<std::vector<double>>(value)
               : std::vector<double>{std::get<double>(value)};
    const uint32_t count = static_cast<uint32_t>(data.size());

    out.reserve(kHeaderBytes + 8ULL * count);
    out.push_back(kMagic0); out.push_back(kMagic1);
    out.push_back(kMagic2); out.push_back(kMagic3);
    out.push_back(kVersion);
    out.push_back(is_vec ? kKindVector : kKindScalar);
    out.push_back(0); out.push_back(0);                       // reserved
    for (int i = 0; i < 4; ++i)                                // count LE
        out.push_back(static_cast<uint8_t>((count >> (8 * i)) & 0xFF));
    for (double d : data) putDoubleLE(out, d);
    return out;
}

/**
 * @brief Decode an `stsb1` byte blob back into a TimeSeriesValue.
 * @param blob Bytes previously produced by encode().
 * @return Scalar (kind=0x01) or vector (kind=0x02) value.
 * @throws std::runtime_error if the header/length is malformed.
 */
inline TimeSeriesValue decode(const std::vector<uint8_t>& blob) {
    if (blob.size() < kHeaderBytes)
        throw std::runtime_error("stsb1: blob too short");
    if (blob[0] != kMagic0 || blob[1] != kMagic1 ||
        blob[2] != kMagic2 || blob[3] != kMagic3)
        throw std::runtime_error("stsb1: bad magic");
    if (blob[4] != kVersion)
        throw std::runtime_error("stsb1: unsupported version");
    const uint8_t kind = blob[5];
    uint32_t count = 0;
    for (int i = 0; i < 4; ++i) count |= static_cast<uint32_t>(blob[8 + i]) << (8 * i);
    if (blob.size() != kHeaderBytes + 8ULL * count)
        throw std::runtime_error("stsb1: length/count mismatch");
    if (kind == kKindScalar) {
        if (count != 1) throw std::runtime_error("stsb1: scalar count must be 1");
        return getDoubleLE(blob.data(), kHeaderBytes);
    }
    if (kind == kKindVector) {
        std::vector<double> v;
        v.reserve(count);
        for (uint32_t i = 0; i < count; ++i)
            v.push_back(getDoubleLE(blob.data(), kHeaderBytes + 8ULL * i));
        return v;
    }
    throw std::runtime_error("stsb1: unknown kind");
}

}  // namespace stsb1
}  // namespace core
}  // namespace sage_tsdb
