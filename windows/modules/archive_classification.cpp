#include "modules/archive_classification.hpp"
#include "core/data/base64.hpp"
#include <algorithm>
#include <charconv>
#include <chrono>
#include <cmath>
#ifdef _WIN32
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <bcrypt.h>
#elif defined(__APPLE__)
#include <CommonCrypto/CommonKeyDerivation.h>
#include <CommonCrypto/CommonRandom.h>
#endif

namespace endfield::modules {
namespace {
using ehud::data::StoreError;using ehud::data::StoreErrorCode;using J=ArchiveJson;
void need(bool value,const char*message){if(!value)throw StoreError(StoreErrorCode::invalid,message);}
[[noreturn]]void unavailable(){throw StoreError(StoreErrorCode::unavailable,"The archive could not be saved.");}
std::string upper(std::string_view value){std::string out(value);for(auto&c:out)if(c>='a'&&c<='z')c=static_cast<char>(c-'a'+'A');return out;}
// Zeroes a password copy the optimizer may not elide (Mac: memset_s).
void wipe(std::string&value)noexcept{volatile char*p=value.data();for(std::size_t i=0;i<value.size();++i)p[i]=0;value.clear();}
bool randomBytes(std::string&out,std::size_t count){out.assign(count,'\0');
#ifdef _WIN32
    return BCRYPT_SUCCESS(BCryptGenRandom(nullptr,reinterpret_cast<PUCHAR>(out.data()),static_cast<ULONG>(count),BCRYPT_USE_SYSTEM_PREFERRED_RNG));
#elif defined(__APPLE__)
    return CCRandomGenerateBytes(out.data(),count)==kCCSuccess;
#else
    return false;
#endif
}
// Swift JSONDecoder UInt32: an integral number token within 0...UInt32.max.
std::optional<std::uint32_t>uint32(const J&value){if(!value.isNumber())return {};const auto token=value.encode(64);std::uint64_t n{};const auto r=std::from_chars(token.data(),token.data()+token.size(),n);if(r.ec==std::errc{}&&r.ptr==token.data()+token.size()&&n<=0xFFFFFFFFull)return static_cast<std::uint32_t>(n);
    const double d=value.number();if(std::isfinite(d)&&std::trunc(d)==d&&d>=0&&d<=4294967295.)return static_cast<std::uint32_t>(d);return {};}
}
ArchiveClassificationError::ArchiveClassificationError(ArchiveClassificationErrorCode code):std::runtime_error(code==ArchiveClassificationErrorCode::passwordMissing?
    "The document's password verifier is missing. It stays classified.":code==ArchiveClassificationErrorCode::vaultUnavailable?"The password store could not be read. The document stays classified.":"Recovery was cancelled."),code_(code){}
std::optional<ArchivePasswordIssue>archivePasswordIssue(std::string_view password,const std::optional<std::string>&confirmation,const ArchiveTextRules&rules){
    need(bool(rules.characters),"Archive passwords need the installed grapheme rules");
    if(!J::validUtf8(password)||rules.characters(password)<archivePasswordMinimumLength)return ArchivePasswordIssue::tooShort;
    if(password.size()>archivePasswordMaximumBytes)return ArchivePasswordIssue::tooLong;
    if(confirmation&&*confirmation!=password)return ArchivePasswordIssue::mismatch;
    return {};
}
std::string archivePasswordAccount(std::string_view document,std::string_view key){need(ehud::data::validUUID(document)&&ehud::data::validUUID(key),"Invalid Archive verifier account");return upper(document)+"/"+upper(key);}
std::string archiveBase64(std::string_view bytes){return ehud::data::base64Encode(bytes);}
// Foundation Data(base64Encoded:) (JSONDecoder's Data strategy): no
// whitespace, padding required, unused tail bits tolerated.
std::optional<std::string>archiveBase64Decoded(std::string_view text){return ehud::data::base64Decode(text,false,false);}
std::optional<std::string>archivePBKDF2(std::string_view password,std::string_view salt,std::uint32_t iterations,std::size_t length){
    std::string out(length,'\0');if(!iterations||!length)return {};
#ifdef _WIN32
    BCRYPT_ALG_HANDLE algorithm{};if(!BCRYPT_SUCCESS(BCryptOpenAlgorithmProvider(&algorithm,BCRYPT_SHA256_ALGORITHM,nullptr,BCRYPT_ALG_HANDLE_HMAC_FLAG)))return {};
    struct Close{BCRYPT_ALG_HANDLE value;~Close(){BCryptCloseAlgorithmProvider(value,0);}}close{algorithm};
    // BCrypt rejects a null buffer even for an empty password (none reach here: 4 Characters minimum).
    if(!BCRYPT_SUCCESS(BCryptDeriveKeyPBKDF2(algorithm,reinterpret_cast<PUCHAR>(const_cast<char*>(password.data())),static_cast<ULONG>(password.size()),
        reinterpret_cast<PUCHAR>(const_cast<char*>(salt.data())),static_cast<ULONG>(salt.size()),iterations,reinterpret_cast<PUCHAR>(out.data()),static_cast<ULONG>(length),0)))return {};
    return out;
#elif defined(__APPLE__)
    if(CCKeyDerivationPBKDF(kCCPBKDF2,password.data(),password.size(),reinterpret_cast<const std::uint8_t*>(salt.data()),salt.size(),kCCPRFHmacAlgSHA256,iterations,
        reinterpret_cast<std::uint8_t*>(out.data()),length)!=kCCSuccess)return {};
    return out;
#else
    (void)password;(void)salt;return {};
#endif
}
bool ArchivePasswordVerifier::valid()const noexcept{return version==1&&algorithm==algorithmName&&iterations>=minimumIterations&&iterations<=maximumIterations&&salt.size()==saltLength&&key.size()==keyLength;}
// JSONEncoder .sortedKeys; Data as standard base64. (Foundation also escapes
// "/" as "\/"; both spellings decode to the same record.)
std::string ArchivePasswordVerifier::encoded()const{need(valid(),"Invalid Archive password verifier");
    return J(J::Object{{"algorithm",algorithm},{"iterations",static_cast<std::int64_t>(iterations)},{"key",archiveBase64(key)},{"salt",archiveBase64(salt)},{"version",version}}).encode(maximumRecordBytes);}
std::optional<ArchivePasswordVerifier>ArchivePasswordVerifier::decode(std::string_view bytes){
    if(bytes.size()>maximumRecordBytes)return {};
    try{const auto j=J::parse(bytes,maximumRecordBytes);if(!j.isObject()||!j["version"].isNumber()||!j["algorithm"].isString()||!j["salt"].isString()||!j["key"].isString())return {};
        ArchivePasswordVerifier value;const auto iterations=uint32(j["iterations"]);const auto salt=archiveBase64Decoded(j["salt"].string()),key=archiveBase64Decoded(j["key"].string());
        // JSONDecoder Int/UInt32 accept integral number tokens such as 1.0.
        if(!iterations||!salt||!key||uint32(j["version"])!=1u)return {};
        value.algorithm=j["algorithm"].string();value.iterations=*iterations;value.salt=*salt;value.key=*key;
        if(!value.valid())return {};return value;
    }catch(const std::exception&){return {};}
}
ArchivePasswordVerifier ArchivePasswordVerifier::make(std::string_view password,std::uint32_t iterations){
    need(J::validUtf8(password)&&!password.empty()&&password.size()<=archivePasswordMaximumBytes&&iterations>=minimumIterations&&iterations<=maximumIterations,"Invalid Archive password");
    ArchivePasswordVerifier value;value.iterations=iterations;if(!randomBytes(value.salt,saltLength))unavailable();
    auto key=archivePBKDF2(password,value.salt,iterations,keyLength);if(!key)unavailable();value.key=std::move(*key);return value;
}
bool ArchivePasswordVerifier::matches(std::string_view password)const{
    if(!valid()||password.size()>archivePasswordMaximumBytes)return false;
    auto candidate=archivePBKDF2(password,salt,iterations,keyLength);if(!candidate||candidate->size()!=key.size())return false;
    unsigned char difference{};for(std::size_t i=0;i<key.size();++i)difference|=static_cast<unsigned char>((*candidate)[i]^key[i]);
    wipe(*candidate);return difference==0;
}
std::uint32_t ArchivePasswordVerifier::calibratedIterations(){
    static const std::uint32_t value=[]{
#ifdef __APPLE__
        const auto rounds=CCCalibratePBKDF(kCCPBKDF2,16,saltLength,kCCPRFHmacAlgSHA256,keyLength,targetMilliseconds);
        return std::clamp<std::uint32_t>(rounds,minimumIterations,maximumIterations);
#else
        // No calibrate API on Windows: time one derivation of a 16-byte
        // password (CCCalibratePBKDF's shape) and scale it to the target.
        constexpr std::uint32_t probe=100'000;const std::string password(16,'p'),salt(saltLength,'s');
        const auto start=std::chrono::steady_clock::now();const auto key=archivePBKDF2(password,salt,probe,keyLength);
        const auto seconds=std::chrono::duration<double>(std::chrono::steady_clock::now()-start).count();
        if(!key||!(seconds>0))return minimumIterations;
        const double scaled=probe*(targetMilliseconds/1000.)/seconds;
        return static_cast<std::uint32_t>(std::clamp(scaled,double(minimumIterations),double(maximumIterations)));
#endif
    }();
    return value;
}
std::optional<std::string>ArchiveMemoryPasswordVault::read(std::string_view account){std::lock_guard lock(mutex_);const auto found=items_.find(account);if(found==items_.end())return {};return found->second;}
void ArchiveMemoryPasswordVault::add(std::string_view account,std::string_view data){std::lock_guard lock(mutex_);if(!items_.emplace(std::string(account),std::string(data)).second)throw std::runtime_error("Duplicate Archive verifier item");}
void ArchiveMemoryPasswordVault::remove(std::string_view account){std::lock_guard lock(mutex_);const auto found=items_.find(account);if(found!=items_.end())items_.erase(found);}
std::vector<std::string>ArchiveMemoryPasswordVault::accounts(){std::lock_guard lock(mutex_);std::vector<std::string>out;for(const auto&[account,data]:items_){(void)data;out.push_back(account);}return out;}
} // namespace endfield::modules
