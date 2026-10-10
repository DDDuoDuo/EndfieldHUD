#include "modules/hypergryph_account_crypto.hpp"
#include <algorithm>
#include <cstring>

namespace endfield::modules::hypergryph {
namespace {
constexpr std::uint32_t K256[64]{
    0x428a2f98,0x71374491,0xb5c0fbcf,0xe9b5dba5,0x3956c25b,0x59f111f1,0x923f82a4,0xab1c5ed5,
    0xd807aa98,0x12835b01,0x243185be,0x550c7dc3,0x72be5d74,0x80deb1fe,0x9bdc06a7,0xc19bf174,
    0xe49b69c1,0xefbe4786,0x0fc19dc6,0x240ca1cc,0x2de92c6f,0x4a7484aa,0x5cb0a9dc,0x76f988da,
    0x983e5152,0xa831c66d,0xb00327c8,0xbf597fc7,0xc6e00bf3,0xd5a79147,0x06ca6351,0x14292967,
    0x27b70a85,0x2e1b2138,0x4d2c6dfc,0x53380d13,0x650a7354,0x766a0abb,0x81c2c92e,0x92722c85,
    0xa2bfe8a1,0xa81a664b,0xc24b8b70,0xc76c51a3,0xd192e819,0xd6990624,0xf40e3585,0x106aa070,
    0x19a4c116,0x1e376c08,0x2748774c,0x34b0bcb5,0x391c0cb3,0x4ed8aa4a,0x5b9cca4f,0x682e6ff3,
    0x748f82ee,0x78a5636f,0x84c87814,0x8cc70208,0x90befffa,0xa4506ceb,0xbef9a3f7,0xc67178f2};
constexpr std::uint32_t rotr(std::uint32_t x,unsigned n) noexcept {return (x>>n)|(x<<(32-n));}
constexpr std::uint32_t rotl(std::uint32_t x,unsigned n) noexcept {return (x<<n)|(x>>(32-n));}

class Sha256 {
public:
    void update(const std::uint8_t* data,std::size_t size) noexcept {
        length_+=static_cast<std::uint64_t>(size);
        while(size) {
            const std::size_t take=std::min<std::size_t>(64-used_,size);
            std::memcpy(block_+used_,data,take);used_+=take;data+=take;size-=take;
            if(used_==64) {compress();used_=0;}
        }
    }
    Sha256Digest finish() noexcept {
        const std::uint64_t bits=length_*8;std::uint8_t pad=0x80;update(&pad,1);
        const std::uint8_t zero=0;while(used_!=56) update(&zero,1);
        std::uint8_t tail[8];for(int i=0;i<8;++i) tail[i]=static_cast<std::uint8_t>(bits>>(56-8*i));
        update(tail,8);Sha256Digest out{};
        for(int i=0;i<8;++i) for(int j=0;j<4;++j) out[std::size_t(i*4+j)]=static_cast<std::uint8_t>(state_[i]>>(24-8*j));
        return out;
    }
private:
    std::uint32_t state_[8]{0x6a09e667,0xbb67ae85,0x3c6ef372,0xa54ff53a,0x510e527f,0x9b05688c,0x1f83d9ab,0x5be0cd19};
    std::uint8_t block_[64]{};std::size_t used_{};std::uint64_t length_{};
    void compress() noexcept {
        std::uint32_t w[64];
        for(int i=0;i<16;++i) w[i]=(std::uint32_t(block_[i*4])<<24)|(std::uint32_t(block_[i*4+1])<<16)|(std::uint32_t(block_[i*4+2])<<8)|block_[i*4+3];
        for(int i=16;i<64;++i) {
            const auto s0=rotr(w[i-15],7)^rotr(w[i-15],18)^(w[i-15]>>3),s1=rotr(w[i-2],17)^rotr(w[i-2],19)^(w[i-2]>>10);
            w[i]=w[i-16]+s0+w[i-7]+s1;
        }
        auto a=state_[0],b=state_[1],c=state_[2],d=state_[3],e=state_[4],f=state_[5],g=state_[6],h=state_[7];
        for(int i=0;i<64;++i) {
            const auto s1=rotr(e,6)^rotr(e,11)^rotr(e,25),ch=(e&f)^(~e&g),t1=h+s1+ch+K256[i]+w[i];
            const auto s0=rotr(a,2)^rotr(a,13)^rotr(a,22),maj=(a&b)^(a&c)^(b&c),t2=s0+maj;
            h=g;g=f;f=e;e=d+t1;d=c;c=b;b=a;a=t1+t2;
        }
        state_[0]+=a;state_[1]+=b;state_[2]+=c;state_[3]+=d;state_[4]+=e;state_[5]+=f;state_[6]+=g;state_[7]+=h;
    }
};

constexpr std::uint32_t MD5K[64]{
    0xd76aa478,0xe8c7b756,0x242070db,0xc1bdceee,0xf57c0faf,0x4787c62a,0xa8304613,0xfd469501,
    0x698098d8,0x8b44f7af,0xffff5bb1,0x895cd7be,0x6b901122,0xfd987193,0xa679438e,0x49b40821,
    0xf61e2562,0xc040b340,0x265e5a51,0xe9b6c7aa,0xd62f105d,0x02441453,0xd8a1e681,0xe7d3fbc8,
    0x21e1cde6,0xc33707d6,0xf4d50d87,0x455a14ed,0xa9e3e905,0xfcefa3f8,0x676f02d9,0x8d2a4c8a,
    0xfffa3942,0x8771f681,0x6d9d6122,0xfde5380c,0xa4beea44,0x4bdecfa9,0xf6bb4b60,0xbebfbc70,
    0x289b7ec6,0xeaa127fa,0xd4ef3085,0x04881d05,0xd9d4d039,0xe6db99e5,0x1fa27cf8,0xc4ac5665,
    0xf4292244,0x432aff97,0xab9423a7,0xfc93a039,0x655b59c3,0x8f0ccc92,0xffeff47d,0x85845dd1,
    0x6fa87e4f,0xfe2ce6e0,0xa3014314,0x4e0811a1,0xf7537e82,0xbd3af235,0x2ad7d2bb,0xeb86d391};
constexpr unsigned MD5S[64]{7,12,17,22,7,12,17,22,7,12,17,22,7,12,17,22,5,9,14,20,5,9,14,20,5,9,14,20,5,9,14,20,
    4,11,16,23,4,11,16,23,4,11,16,23,4,11,16,23,6,10,15,21,6,10,15,21,6,10,15,21,6,10,15,21};

class Md5 {
public:
    void update(const std::uint8_t* data,std::size_t size) noexcept {
        length_+=static_cast<std::uint64_t>(size);
        while(size) {
            const std::size_t take=std::min<std::size_t>(64-used_,size);
            std::memcpy(block_+used_,data,take);used_+=take;data+=take;size-=take;
            if(used_==64) {compress();used_=0;}
        }
    }
    Md5Digest finish() noexcept {
        const std::uint64_t bits=length_*8;std::uint8_t pad=0x80;update(&pad,1);
        const std::uint8_t zero=0;while(used_!=56) update(&zero,1);
        std::uint8_t tail[8];for(int i=0;i<8;++i) tail[i]=static_cast<std::uint8_t>(bits>>(8*i));
        update(tail,8);Md5Digest out{};
        for(int i=0;i<4;++i) for(int j=0;j<4;++j) out[std::size_t(i*4+j)]=static_cast<std::uint8_t>(state_[i]>>(8*j));
        return out;
    }
private:
    std::uint32_t state_[4]{0x67452301,0xefcdab89,0x98badcfe,0x10325476};
    std::uint8_t block_[64]{};std::size_t used_{};std::uint64_t length_{};
    void compress() noexcept {
        std::uint32_t m[16];
        for(int i=0;i<16;++i) m[i]=std::uint32_t(block_[i*4])|(std::uint32_t(block_[i*4+1])<<8)|(std::uint32_t(block_[i*4+2])<<16)|(std::uint32_t(block_[i*4+3])<<24);
        auto a=state_[0],b=state_[1],c=state_[2],d=state_[3];
        for(unsigned i=0;i<64;++i) {
            std::uint32_t f;unsigned g;
            if(i<16) {f=(b&c)|(~b&d);g=i;}
            else if(i<32) {f=(d&b)|(~d&c);g=(5*i+1)%16;}
            else if(i<48) {f=b^c^d;g=(3*i+5)%16;}
            else {f=c^(b|~d);g=(7*i)%16;}
            const auto next=d;d=c;c=b;b=b+rotl(a+f+MD5K[i]+m[g],MD5S[i]);a=next;
        }
        state_[0]+=a;state_[1]+=b;state_[2]+=c;state_[3]+=d;
    }
};
const std::uint8_t* bytes(std::string_view value) noexcept {return reinterpret_cast<const std::uint8_t*>(value.data());}
}
Sha256Digest sha256(std::span<const std::uint8_t> value) noexcept {Sha256 h;h.update(value.data(),value.size());return h.finish();}
Sha256Digest sha256(std::string_view value) noexcept {Sha256 h;h.update(bytes(value),value.size());return h.finish();}
Sha256Digest hmacSha256(std::string_view key,std::string_view message) noexcept {
    std::uint8_t block[64]{};
    if(key.size()>64) {const auto digest=sha256(key);std::memcpy(block,digest.data(),digest.size());}
    else if(!key.empty()) std::memcpy(block,key.data(),key.size());
    std::uint8_t inner[64],outer[64];
    for(int i=0;i<64;++i) {inner[i]=block[i]^0x36;outer[i]=block[i]^0x5c;}
    Sha256 a;a.update(inner,64);a.update(bytes(message),message.size());const auto first=a.finish();
    Sha256 b;b.update(outer,64);b.update(first.data(),first.size());
    std::memset(block,0,sizeof block);std::memset(inner,0,sizeof inner);std::memset(outer,0,sizeof outer);
    return b.finish();
}
Md5Digest md5(std::string_view value) noexcept {Md5 h;h.update(bytes(value),value.size());return h.finish();}
std::string lowercaseHex(std::span<const std::uint8_t> value) {
    static constexpr char digits[]="0123456789abcdef";std::string out;out.reserve(value.size()*2);
    for(const auto b:value) {out.push_back(digits[b>>4]);out.push_back(digits[b&15]);}
    return out;
}
std::string communitySignatureDigest(std::string_view token,std::string_view message) {
    const auto mac=hmacSha256(token,message);auto hex=lowercaseHex(mac);const auto digest=md5(hex);wipe(hex);
    return lowercaseHex(digest);
}
void wipe(std::string& value) noexcept {
    volatile char* p=value.data();for(std::size_t i=0;i<value.size();++i) p[i]=0;value.clear();
}
}
