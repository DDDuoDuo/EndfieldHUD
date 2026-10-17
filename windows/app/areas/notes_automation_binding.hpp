#pragma once
// ModuleOwner UI Automation hooks (app/module_owner.hpp) for the owners whose
// projected controls come from native::NativeNotesAutomation: the built-in
// Notes and Archive adapters and the Projection area. The provider is created
// on the first refresh (a UIA client asked the HUD window for its root) and
// again for a new group root; refresh() runs on content events only.
#include "native/notes_automation.hpp"
#ifdef _WIN32
#include <functional>
#include <memory>

namespace endfield::notes {
class NotesAutomationBinding final {
public:
    using Factory=std::function<std::unique_ptr<native::NativeNotesAutomation>(IRawElementProviderFragmentRoot* group)>;
    explicit NotesAutomationBinding(Factory factory):factory_(std::move(factory)){}
    ~NotesAutomationBinding(){provider_.reset();} // peers disconnect before the owner they read
    NotesAutomationBinding(const NotesAutomationBinding&)=delete;
    NotesAutomationBinding&operator=(const NotesAutomationBinding&)=delete;
    // ModuleOwner::automationRefresh: true when the peer set or order changed.
    bool refresh(IRawElementProviderFragmentRoot* group){
        if(!factory_)return false;
        bool replaced=false;
        if(!provider_||group!=group_){const bool had=provider_&&provider_->size();provider_.reset();group_=group;provider_=factory_(group);replaced=had;}
        return provider_&&(provider_->refresh()||replaced);
    }
    std::size_t count()const noexcept{return provider_?provider_->size():0;}
    long child(std::size_t index,IRawElementProviderFragment** out)const{if(!provider_){if(out)*out=nullptr;return E_INVALIDARG;}return provider_->child(index,out);}
    long fromPoint(double x,double y,IRawElementProviderFragment** out)const{if(!provider_){if(out)*out=nullptr;return S_OK;}return provider_->fromPoint(x,y,out);}
    void disconnect()noexcept{if(provider_)provider_->disconnect();}
    native::NativeNotesAutomation* provider()const noexcept{return provider_.get();}
private:
    Factory factory_;std::unique_ptr<native::NativeNotesAutomation>provider_;IRawElementProviderFragmentRoot* group_{};
};
}
#endif
