#include "modules/hypergryph_account_identity_sync.hpp"
#include "modules/hypergryph_account_unicode.hpp"
#include <algorithm>
#include <limits>
#include <stdexcept>

namespace endfield::modules::hypergryph {
namespace {
bool hasControl(std::string_view text) {
    for(std::size_t at=0;at<text.size();) {const auto s=decodeUTF8(text,at);if(!s) return true;if(unicodeContains(UnicodeSet::controlCharacters,s->value)) return true;at+=s->length;}
    return false;
}
bool hasTagForbidden(std::string_view text) {
    for(std::size_t at=0;at<text.size();) {
        const auto s=decodeUTF8(text,at);if(!s) return true;
        if(s->value=='#'||unicodeContains(UnicodeSet::controlCharacters,s->value)||unicodeContains(UnicodeSet::whitespacesAndNewlines,s->value)) return true;
        at+=s->length;
    }
    return false;
}
}
void applyGameProfileUpdate(ehud::data::Profile& p,const GameProfileUpdate& u,const design::CharacterRules& characters) {
    auto next=p;
    if(u.gamePlayerID) {next.gamePlayerID=*u.gamePlayerID;next.playerIDOverride.reset();}
    if(u.name) next.name=*u.name;
    if(u.tag) next.tag=*u.tag;
    if(u.awakeningDate) {next.awakeningDate=*u.awakeningDate;next.hasManualAwakeningDate=false;}
    if(u.permissionLevel) next.permissionLevel=static_cast<int>(std::clamp<std::int64_t>(*u.permissionLevel,std::numeric_limits<int>::min(),std::numeric_limits<int>::max()));
    if(u.explorationLevel) next.explorationLevel=static_cast<int>(std::clamp<std::int64_t>(*u.explorationLevel,std::numeric_limits<int>::min(),std::numeric_limits<int>::max()));
    if(u.operatorsCount) next.operatorsCount=*u.operatorsCount;
    if(u.weaponsCount) next.weaponsCount=*u.weaponsCount;
    if(u.archivesCount) next.archivesCount=*u.archivesCount;
    // UserProfile.normalizedEditableValues (fields this update can change).
    next.name=characters.prefix(trimScalars(next.name,UnicodeSet::whitespacesAndNewlines),20);
    next.tag=characters.prefix(trimScalars(next.tag,UnicodeSet::whitespacesAndNewlines),10);
    next.permissionLevel=std::min(60,std::max(1,next.permissionLevel));
    next.explorationLevel=std::min(7,std::max(1,next.explorationLevel));
    // UserProfileStore.validate for those fields.
    if(next.name.empty()||characters.count(next.name)>20||hasControl(next.name)) throw std::invalid_argument("invalid synced profile name");
    if(next.tag.empty()||characters.count(next.tag)>10||hasTagForbidden(next.tag)) throw std::invalid_argument("invalid synced profile tag");
    if(next.operatorsCount<0||next.weaponsCount<0||next.archivesCount<0) throw std::invalid_argument("invalid synced profile counts");
    p=std::move(next);
}
ProfileStoreAccountSink::ProfileStoreAccountSink(ehud::data::ProfileStore& store,design::CharacterRules characters,ImportAvatar import,Changed changed)
    :store_(&store),characters_(std::move(characters)),import_(std::move(import)),changed_(std::move(changed)) {}
void ProfileStoreAccountSink::applyGameProfile(const GameProfileUpdate& update) {
    auto profile=store_->value();applyGameProfileUpdate(profile,update,characters_);
    if(store_->updateFromGame(profile)&&changed_) changed_();
}
void ProfileStoreAccountSink::setProfileSyncLocked(ehud::data::ProfileSyncFields locked) {
    if(store_->profileSyncLocked()==locked) return;
    store_->setProfileSyncLocked(locked);if(changed_) changed_();
}
std::optional<std::string> ProfileStoreAccountSink::importGameAvatar(const std::vector<std::uint8_t>& png) {
    if(!import_) throw std::runtime_error("Profile avatar import is not connected");
    auto filename=import_(png);if(changed_) changed_();return filename;
}
}
