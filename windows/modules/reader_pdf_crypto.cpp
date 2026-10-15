#include "modules/reader_pdf_crypto.hpp"
#include <algorithm>
#include <stdexcept>

namespace endfield::modules::pdfcrypto {
namespace {
constexpr std::uint32_t rotl(std::uint32_t x, unsigned n) noexcept { return x << n | x >> (32 - n); }
constexpr std::uint32_t rotr(std::uint32_t x, unsigned n) noexcept { return x >> n | x << (32 - n); }
constexpr std::uint64_t rotr64(std::uint64_t x, unsigned n) noexcept { return x >> n | x << (64 - n); }

// Merkle-Damgard padding: 0x80, zeros, then the bit length in `lengthBytes`
// bytes (little-endian for MD5, big-endian for SHA-2).
Bytes padded(std::span<const std::uint8_t> data, std::size_t block, std::size_t lengthBytes, bool littleEndian) {
    Bytes message(data.begin(), data.end());
    message.push_back(0x80);
    while (message.size() % block != block - lengthBytes) message.push_back(0);
    const std::uint64_t bits = static_cast<std::uint64_t>(data.size()) * 8;
    for (std::size_t n = 0; n < lengthBytes; ++n) {
        const std::size_t shift = littleEndian ? n * 8 : (lengthBytes - 1 - n) * 8;
        message.push_back(shift < 64 ? static_cast<std::uint8_t>(bits >> shift) : 0);
    }
    return message;
}

constexpr std::array<std::uint32_t, 64> md5K{
    0xd76aa478, 0xe8c7b756, 0x242070db, 0xc1bdceee, 0xf57c0faf, 0x4787c62a, 0xa8304613, 0xfd469501, 0x698098d8, 0x8b44f7af, 0xffff5bb1,
    0x895cd7be, 0x6b901122, 0xfd987193, 0xa679438e, 0x49b40821, 0xf61e2562, 0xc040b340, 0x265e5a51, 0xe9b6c7aa, 0xd62f105d, 0x02441453,
    0xd8a1e681, 0xe7d3fbc8, 0x21e1cde6, 0xc33707d6, 0xf4d50d87, 0x455a14ed, 0xa9e3e905, 0xfcefa3f8, 0x676f02d9, 0x8d2a4c8a, 0xfffa3942,
    0x8771f681, 0x6d9d6122, 0xfde5380c, 0xa4beea44, 0x4bdecfa9, 0xf6bb4b60, 0xbebfbc70, 0x289b7ec6, 0xeaa127fa, 0xd4ef3085, 0x04881d05,
    0xd9d4d039, 0xe6db99e5, 0x1fa27cf8, 0xc4ac5665, 0xf4292244, 0x432aff97, 0xab9423a7, 0xfc93a039, 0x655b59c3, 0x8f0ccc92, 0xffeff47d,
    0x85845dd1, 0x6fa87e4f, 0xfe2ce6e0, 0xa3014314, 0x4e0811a1, 0xf7537e82, 0xbd3af235, 0x2ad7d2bb, 0xeb86d391};
constexpr std::array<unsigned, 64> md5S{7, 12, 17, 22, 7, 12, 17, 22, 7, 12, 17, 22, 7, 12, 17, 22, 5, 9, 14, 20, 5, 9, 14, 20, 5, 9,
                                        14, 20, 5, 9, 14, 20, 4, 11, 16, 23, 4, 11, 16, 23, 4, 11, 16, 23, 4, 11, 16, 23, 6, 10, 15, 21,
                                        6, 10, 15, 21, 6, 10, 15, 21, 6, 10, 15, 21};

constexpr std::array<std::uint32_t, 64> sha256K{
    0x428a2f98, 0x71374491, 0xb5c0fbcf, 0xe9b5dba5, 0x3956c25b, 0x59f111f1, 0x923f82a4, 0xab1c5ed5, 0xd807aa98, 0x12835b01, 0x243185be,
    0x550c7dc3, 0x72be5d74, 0x80deb1fe, 0x9bdc06a7, 0xc19bf174, 0xe49b69c1, 0xefbe4786, 0x0fc19dc6, 0x240ca1cc, 0x2de92c6f, 0x4a7484aa,
    0x5cb0a9dc, 0x76f988da, 0x983e5152, 0xa831c66d, 0xb00327c8, 0xbf597fc7, 0xc6e00bf3, 0xd5a79147, 0x06ca6351, 0x14292967, 0x27b70a85,
    0x2e1b2138, 0x4d2c6dfc, 0x53380d13, 0x650a7354, 0x766a0abb, 0x81c2c92e, 0x92722c85, 0xa2bfe8a1, 0xa81a664b, 0xc24b8b70, 0xc76c51a3,
    0xd192e819, 0xd6990624, 0xf40e3585, 0x106aa070, 0x19a4c116, 0x1e376c08, 0x2748774c, 0x34b0bcb5, 0x391c0cb3, 0x4ed8aa4a, 0x5b9cca4f,
    0x682e6ff3, 0x748f82ee, 0x78a5636f, 0x84c87814, 0x8cc70208, 0x90befffa, 0xa4506ceb, 0xbef9a3f7, 0xc67178f2};

constexpr std::array<std::uint64_t, 80> sha512K{
    0x428a2f98d728ae22, 0x7137449123ef65cd, 0xb5c0fbcfec4d3b2f, 0xe9b5dba58189dbbc, 0x3956c25bf348b538, 0x59f111f1b605d019,
    0x923f82a4af194f9b, 0xab1c5ed5da6d8118, 0xd807aa98a3030242, 0x12835b0145706fbe, 0x243185be4ee4b28c, 0x550c7dc3d5ffb4e2,
    0x72be5d74f27b896f, 0x80deb1fe3b1696b1, 0x9bdc06a725c71235, 0xc19bf174cf692694, 0xe49b69c19ef14ad2, 0xefbe4786384f25e3,
    0x0fc19dc68b8cd5b5, 0x240ca1cc77ac9c65, 0x2de92c6f592b0275, 0x4a7484aa6ea6e483, 0x5cb0a9dcbd41fbd4, 0x76f988da831153b5,
    0x983e5152ee66dfab, 0xa831c66d2db43210, 0xb00327c898fb213f, 0xbf597fc7beef0ee4, 0xc6e00bf33da88fc2, 0xd5a79147930aa725,
    0x06ca6351e003826f, 0x142929670a0e6e70, 0x27b70a8546d22ffc, 0x2e1b21385c26c926, 0x4d2c6dfc5ac42aed, 0x53380d139d95b3df,
    0x650a73548baf63de, 0x766a0abb3c77b2a8, 0x81c2c92e47edaee6, 0x92722c851482353b, 0xa2bfe8a14cf10364, 0xa81a664bbc423001,
    0xc24b8b70d0f89791, 0xc76c51a30654be30, 0xd192e819d6ef5218, 0xd69906245565a910, 0xf40e35855771202a, 0x106aa07032bbd1b8,
    0x19a4c116b8d2d0c8, 0x1e376c085141ab53, 0x2748774cdf8eeb99, 0x34b0bcb5e19b48a8, 0x391c0cb3c5c95a63, 0x4ed8aa4ae3418acb,
    0x5b9cca4f7763e373, 0x682e6ff3d6b2b8a3, 0x748f82ee5defb2fc, 0x78a5636f43172f60, 0x84c87814a1f0ab72, 0x8cc702081a6439ec,
    0x90befffa23631e28, 0xa4506cebde82bde9, 0xbef9a3f7b2c67915, 0xc67178f2e372532b, 0xca273eceea26619c, 0xd186b8c721c0c207,
    0xeada7dd6cde0eb1e, 0xf57d4f7fee6ed178, 0x06f067aa72176fba, 0x0a637dc5a2c898a6, 0x113f9804bef90dae, 0x1b710b35131c471b,
    0x28db77f523047d84, 0x32caab7b40c72493, 0x3c9ebe0a15c9bebc, 0x431d67c49c100d4c, 0x4cc5d4becb3e42b6, 0x597f299cfc657e2a,
    0x5fcb6fab3ad6faec, 0x6c44198c4a475817};

std::array<std::uint64_t, 8> sha512State(std::span<const std::uint8_t> data, std::array<std::uint64_t, 8> h) {
    const auto message = padded(data, 128, 16, false);
    for (std::size_t at = 0; at < message.size(); at += 128) {
        std::array<std::uint64_t, 80> w{};
        for (unsigned i = 0; i < 16; ++i)
            for (unsigned b = 0; b < 8; ++b) w[i] = w[i] << 8 | message[at + i * 8 + b];
        for (unsigned i = 16; i < 80; ++i) {
            const auto s0 = rotr64(w[i - 15], 1) ^ rotr64(w[i - 15], 8) ^ (w[i - 15] >> 7);
            const auto s1 = rotr64(w[i - 2], 19) ^ rotr64(w[i - 2], 61) ^ (w[i - 2] >> 6);
            w[i] = w[i - 16] + s0 + w[i - 7] + s1;
        }
        auto [a, b, c, d, e, f, g, hh] = h;
        for (unsigned i = 0; i < 80; ++i) {
            const auto t1 = hh + (rotr64(e, 14) ^ rotr64(e, 18) ^ rotr64(e, 41)) + ((e & f) ^ (~e & g)) + sha512K[i] + w[i];
            const auto t2 = (rotr64(a, 28) ^ rotr64(a, 34) ^ rotr64(a, 39)) + ((a & b) ^ (a & c) ^ (b & c));
            hh = g; g = f; f = e; e = d + t1; d = c; c = b; b = a; a = t1 + t2;
        }
        h[0] += a; h[1] += b; h[2] += c; h[3] += d; h[4] += e; h[5] += f; h[6] += g; h[7] += hh;
    }
    return h;
}

// AES (FIPS-197).
constexpr std::array<std::uint8_t, 256> sbox = [] {
    std::array<std::uint8_t, 256> s{};
    std::uint8_t p = 1, q = 1;
    do {
        // p *= 3 and q /= 3 in GF(2^8): q walks the multiplicative inverses.
        p = static_cast<std::uint8_t>(p ^ (p << 1) ^ (p & 0x80 ? 0x1B : 0));
        q ^= static_cast<std::uint8_t>(q << 1);
        q ^= static_cast<std::uint8_t>(q << 2);
        q ^= static_cast<std::uint8_t>(q << 4);
        if (q & 0x80) q ^= 0x09;
        const auto rot = [](std::uint8_t x, unsigned n) { return static_cast<std::uint8_t>(x << n | x >> (8 - n)); };
        s[p] = static_cast<std::uint8_t>(q ^ rot(q, 1) ^ rot(q, 2) ^ rot(q, 3) ^ rot(q, 4) ^ 0x63);
    } while (p != 1);
    s[0] = 0x63;
    return s;
}();
constexpr std::array<std::uint8_t, 256> inverseSbox = [] {
    std::array<std::uint8_t, 256> s{};
    for (unsigned n = 0; n < 256; ++n) s[sbox[n]] = static_cast<std::uint8_t>(n);
    return s;
}();
constexpr std::uint8_t xtime(std::uint8_t x) noexcept { return static_cast<std::uint8_t>(x << 1 ^ (x & 0x80 ? 0x1B : 0)); }
constexpr std::uint8_t multiply(std::uint8_t a, std::uint8_t b) noexcept {
    std::uint8_t out{};
    while (b) {
        if (b & 1) out ^= a;
        a = xtime(a);
        b >>= 1;
    }
    return out;
}

struct Aes {
    std::array<std::uint8_t, 240> keys{};
    unsigned rounds{};
    explicit Aes(std::span<const std::uint8_t> key) {
        if (key.size() != 16 && key.size() != 32) throw std::invalid_argument("AES key must be 16 or 32 bytes");
        const unsigned nk = static_cast<unsigned>(key.size() / 4);
        rounds = nk + 6;
        std::copy(key.begin(), key.end(), keys.begin());
        std::uint8_t rcon = 1;
        for (unsigned i = nk; i < 4 * (rounds + 1); ++i) {
            std::array<std::uint8_t, 4> t{keys[(i - 1) * 4], keys[(i - 1) * 4 + 1], keys[(i - 1) * 4 + 2], keys[(i - 1) * 4 + 3]};
            if (i % nk == 0) {
                t = {static_cast<std::uint8_t>(sbox[t[1]] ^ rcon), sbox[t[2]], sbox[t[3]], sbox[t[0]]};
                rcon = xtime(rcon);
            } else if (nk > 6 && i % nk == 4) {
                for (auto& v : t) v = sbox[v];
            }
            for (unsigned b = 0; b < 4; ++b) keys[i * 4 + b] = static_cast<std::uint8_t>(keys[(i - nk) * 4 + b] ^ t[b]);
        }
    }
    void add(std::array<std::uint8_t, 16>& s, unsigned round) const noexcept {
        for (unsigned n = 0; n < 16; ++n) s[n] ^= keys[round * 16 + n];
    }
    void encrypt(std::array<std::uint8_t, 16>& s) const noexcept {
        add(s, 0);
        for (unsigned round = 1; round <= rounds; ++round) {
            for (auto& v : s) v = sbox[v];
            const auto t = s; // ShiftRows: column-major state, row r rotates by r
            for (unsigned c = 0; c < 4; ++c)
                for (unsigned r = 0; r < 4; ++r) s[c * 4 + r] = t[((c + r) % 4) * 4 + r];
            if (round != rounds)
                for (unsigned c = 0; c < 4; ++c) {
                    const std::uint8_t a0 = s[c * 4], a1 = s[c * 4 + 1], a2 = s[c * 4 + 2], a3 = s[c * 4 + 3];
                    s[c * 4] = static_cast<std::uint8_t>(xtime(a0) ^ xtime(a1) ^ a1 ^ a2 ^ a3);
                    s[c * 4 + 1] = static_cast<std::uint8_t>(a0 ^ xtime(a1) ^ xtime(a2) ^ a2 ^ a3);
                    s[c * 4 + 2] = static_cast<std::uint8_t>(a0 ^ a1 ^ xtime(a2) ^ xtime(a3) ^ a3);
                    s[c * 4 + 3] = static_cast<std::uint8_t>(xtime(a0) ^ a0 ^ a1 ^ a2 ^ xtime(a3));
                }
            add(s, round);
        }
    }
    void decrypt(std::array<std::uint8_t, 16>& s) const noexcept {
        add(s, rounds);
        for (unsigned round = rounds; round-- > 0;) {
            const auto t = s; // inverse ShiftRows
            for (unsigned c = 0; c < 4; ++c)
                for (unsigned r = 0; r < 4; ++r) s[((c + r) % 4) * 4 + r] = t[c * 4 + r];
            for (auto& v : s) v = inverseSbox[v];
            add(s, round);
            if (round != 0)
                for (unsigned c = 0; c < 4; ++c) {
                    const std::uint8_t a0 = s[c * 4], a1 = s[c * 4 + 1], a2 = s[c * 4 + 2], a3 = s[c * 4 + 3];
                    s[c * 4] = static_cast<std::uint8_t>(multiply(a0, 14) ^ multiply(a1, 11) ^ multiply(a2, 13) ^ multiply(a3, 9));
                    s[c * 4 + 1] = static_cast<std::uint8_t>(multiply(a0, 9) ^ multiply(a1, 14) ^ multiply(a2, 11) ^ multiply(a3, 13));
                    s[c * 4 + 2] = static_cast<std::uint8_t>(multiply(a0, 13) ^ multiply(a1, 9) ^ multiply(a2, 14) ^ multiply(a3, 11));
                    s[c * 4 + 3] = static_cast<std::uint8_t>(multiply(a0, 11) ^ multiply(a1, 13) ^ multiply(a2, 9) ^ multiply(a3, 14));
                }
        }
    }
};
} // namespace

std::array<std::uint8_t, 16> md5(std::span<const std::uint8_t> data) {
    std::uint32_t a0 = 0x67452301, b0 = 0xefcdab89, c0 = 0x98badcfe, d0 = 0x10325476;
    const auto message = padded(data, 64, 8, true);
    for (std::size_t at = 0; at < message.size(); at += 64) {
        std::array<std::uint32_t, 16> m{};
        for (unsigned i = 0; i < 16; ++i)
            m[i] = static_cast<std::uint32_t>(message[at + i * 4]) | static_cast<std::uint32_t>(message[at + i * 4 + 1]) << 8 |
                   static_cast<std::uint32_t>(message[at + i * 4 + 2]) << 16 | static_cast<std::uint32_t>(message[at + i * 4 + 3]) << 24;
        std::uint32_t a = a0, b = b0, c = c0, d = d0;
        for (unsigned i = 0; i < 64; ++i) {
            std::uint32_t f{};
            unsigned g{};
            if (i < 16) { f = (b & c) | (~b & d); g = i; }
            else if (i < 32) { f = (d & b) | (~d & c); g = (5 * i + 1) % 16; }
            else if (i < 48) { f = b ^ c ^ d; g = (3 * i + 5) % 16; }
            else { f = c ^ (b | ~d); g = (7 * i) % 16; }
            f += a + md5K[i] + m[g];
            a = d; d = c; c = b;
            b += rotl(f, md5S[i]);
        }
        a0 += a; b0 += b; c0 += c; d0 += d;
    }
    std::array<std::uint8_t, 16> out{};
    const std::array<std::uint32_t, 4> words{a0, b0, c0, d0};
    for (unsigned i = 0; i < 16; ++i) out[i] = static_cast<std::uint8_t>(words[i / 4] >> (8 * (i % 4)));
    return out;
}

std::array<std::uint8_t, 32> sha256(std::span<const std::uint8_t> data) {
    std::array<std::uint32_t, 8> h{0x6a09e667, 0xbb67ae85, 0x3c6ef372, 0xa54ff53a, 0x510e527f, 0x9b05688c, 0x1f83d9ab, 0x5be0cd19};
    const auto message = padded(data, 64, 8, false);
    for (std::size_t at = 0; at < message.size(); at += 64) {
        std::array<std::uint32_t, 64> w{};
        for (unsigned i = 0; i < 16; ++i)
            w[i] = static_cast<std::uint32_t>(message[at + i * 4]) << 24 | static_cast<std::uint32_t>(message[at + i * 4 + 1]) << 16 |
                   static_cast<std::uint32_t>(message[at + i * 4 + 2]) << 8 | static_cast<std::uint32_t>(message[at + i * 4 + 3]);
        for (unsigned i = 16; i < 64; ++i) {
            const auto s0 = rotr(w[i - 15], 7) ^ rotr(w[i - 15], 18) ^ (w[i - 15] >> 3);
            const auto s1 = rotr(w[i - 2], 17) ^ rotr(w[i - 2], 19) ^ (w[i - 2] >> 10);
            w[i] = w[i - 16] + s0 + w[i - 7] + s1;
        }
        auto [a, b, c, d, e, f, g, hh] = h;
        for (unsigned i = 0; i < 64; ++i) {
            const auto t1 = hh + (rotr(e, 6) ^ rotr(e, 11) ^ rotr(e, 25)) + ((e & f) ^ (~e & g)) + sha256K[i] + w[i];
            const auto t2 = (rotr(a, 2) ^ rotr(a, 13) ^ rotr(a, 22)) + ((a & b) ^ (a & c) ^ (b & c));
            hh = g; g = f; f = e; e = d + t1; d = c; c = b; b = a; a = t1 + t2;
        }
        h[0] += a; h[1] += b; h[2] += c; h[3] += d; h[4] += e; h[5] += f; h[6] += g; h[7] += hh;
    }
    std::array<std::uint8_t, 32> out{};
    for (unsigned i = 0; i < 32; ++i) out[i] = static_cast<std::uint8_t>(h[i / 4] >> (24 - 8 * (i % 4)));
    return out;
}

std::array<std::uint8_t, 64> sha512(std::span<const std::uint8_t> data) {
    const auto h = sha512State(data, {0x6a09e667f3bcc908, 0xbb67ae8584caa73b, 0x3c6ef372fe94f82b, 0xa54ff53a5f1d36f1, 0x510e527fade682d1,
                                      0x9b05688c2b3e6c1f, 0x1f83d9abfb41bd6b, 0x5be0cd19137e2179});
    std::array<std::uint8_t, 64> out{};
    for (unsigned i = 0; i < 64; ++i) out[i] = static_cast<std::uint8_t>(h[i / 8] >> (56 - 8 * (i % 8)));
    return out;
}

std::array<std::uint8_t, 48> sha384(std::span<const std::uint8_t> data) {
    const auto h = sha512State(data, {0xcbbb9d5dc1059ed8, 0x629a292a367cd507, 0x9159015a3070dd17, 0x152fecd8f70e5939, 0x67332667ffc00b31,
                                      0x8eb44a8768581511, 0xdb0c2e0d64f98fa7, 0x47b5481dbefa4fa4});
    std::array<std::uint8_t, 48> out{};
    for (unsigned i = 0; i < 48; ++i) out[i] = static_cast<std::uint8_t>(h[i / 8] >> (56 - 8 * (i % 8)));
    return out;
}

Bytes rc4(std::span<const std::uint8_t> key, std::span<const std::uint8_t> data) {
    if (key.empty() || key.size() > 256) throw std::invalid_argument("RC4 key must be 1-256 bytes");
    std::array<std::uint8_t, 256> s{};
    for (unsigned n = 0; n < 256; ++n) s[n] = static_cast<std::uint8_t>(n);
    for (unsigned i = 0, j = 0; i < 256; ++i) {
        j = (j + s[i] + key[i % key.size()]) & 0xFF;
        std::swap(s[i], s[j]);
    }
    Bytes out(data.size());
    for (std::size_t n = 0, i = 0, j = 0; n < data.size(); ++n) {
        i = (i + 1) & 0xFF;
        j = (j + s[i]) & 0xFF;
        std::swap(s[i], s[j]);
        out[n] = static_cast<std::uint8_t>(data[n] ^ s[(s[i] + s[j]) & 0xFF]);
    }
    return out;
}

Bytes aesCbcEncrypt(std::span<const std::uint8_t> key, std::span<const std::uint8_t, 16> iv, std::span<const std::uint8_t> data) {
    if (data.size() % 16) throw std::invalid_argument("AES-CBC data must be whole blocks");
    const Aes aes(key);
    Bytes out(data.size());
    std::array<std::uint8_t, 16> chain{};
    std::copy(iv.begin(), iv.end(), chain.begin());
    for (std::size_t at = 0; at < data.size(); at += 16) {
        for (unsigned n = 0; n < 16; ++n) chain[n] ^= data[at + n];
        aes.encrypt(chain);
        std::copy(chain.begin(), chain.end(), out.begin() + static_cast<std::ptrdiff_t>(at));
    }
    return out;
}

Bytes aesCbcDecrypt(std::span<const std::uint8_t> key, std::span<const std::uint8_t, 16> iv, std::span<const std::uint8_t> data) {
    if (data.size() % 16) throw std::invalid_argument("AES-CBC data must be whole blocks");
    const Aes aes(key);
    Bytes out(data.size());
    std::array<std::uint8_t, 16> previous{};
    std::copy(iv.begin(), iv.end(), previous.begin());
    for (std::size_t at = 0; at < data.size(); at += 16) {
        std::array<std::uint8_t, 16> block{};
        std::copy_n(data.begin() + static_cast<std::ptrdiff_t>(at), 16, block.begin());
        const auto cipher = block;
        aes.decrypt(block);
        for (unsigned n = 0; n < 16; ++n) out[at + n] = static_cast<std::uint8_t>(block[n] ^ previous[n]);
        previous = cipher;
    }
    return out;
}
} // namespace endfield::modules::pdfcrypto
