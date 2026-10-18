#pragma once
// The primitives of the PDF standard security handler (PDF 32000-1 7.6.3,
// ISO 32000-2 7.6.4) that the Reader's PDF title reader needs to read the
// /Title of an owner-password-only PDF, as PDFKit does: MD5, SHA-256/384/512,
// RC4 and AES-128/256 in CBC mode. Portable, allocation-bounded by their
// inputs; checked against the FIPS/RFC test vectors in reader_pdf_title_tests.
// Not a general cryptographic library: nothing here protects secrets.
#include <array>
#include <cstdint>
#include <span>
#include <vector>

namespace endfield::modules::pdfcrypto {
using Bytes = std::vector<std::uint8_t>;
std::array<std::uint8_t, 16> md5(std::span<const std::uint8_t>);
std::array<std::uint8_t, 32> sha256(std::span<const std::uint8_t>);
std::array<std::uint8_t, 48> sha384(std::span<const std::uint8_t>);
std::array<std::uint8_t, 64> sha512(std::span<const std::uint8_t>);
Bytes rc4(std::span<const std::uint8_t> key, std::span<const std::uint8_t> data);
// CBC without padding: `data` is a multiple of 16 bytes, the key 16 or 32 bytes.
Bytes aesCbcEncrypt(std::span<const std::uint8_t> key, std::span<const std::uint8_t, 16> iv, std::span<const std::uint8_t> data);
Bytes aesCbcDecrypt(std::span<const std::uint8_t> key, std::span<const std::uint8_t, 16> iv, std::span<const std::uint8_t> data);
} // namespace endfield::modules::pdfcrypto
