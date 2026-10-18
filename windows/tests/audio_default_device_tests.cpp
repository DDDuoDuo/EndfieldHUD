#include "native/audio_default_device.hpp"
#include <array>
#include <iostream>
#include <stdexcept>
#include <vector>
namespace {
namespace gpu=endfield::native;
std::size_t checks{};void check(bool v,const char*m){++checks;if(!v)throw std::runtime_error(m);}
using Flow=gpu::AudioDefaultFlow;using Role=gpu::AudioDefaultRole;
struct Graph {
    std::array<std::wstring,3>outputs{L"speakers",L"speakers",L"communications-output"},inputs{L"mic",L"mic",L"communications-input"};
    struct Write{std::wstring id;Role role;};std::vector<Write>writes;
    unsigned capabilities{},validations{},reads{},failAt{},validationFailAt{},throwAt{};bool supported{true},applyFailure{},externalChange{},badReadback{},rollbackFailure{},reenter{};
    gpu::AudioDefaultDeviceSelector*selector{};Flow selected{Flow::output};
    gpu::AudioDefaultDeviceAccess access(){gpu::AudioDefaultDeviceAccess a;
        a.capability=[this]{++capabilities;return gpu::AudioDefaultDeviceCapability{supported,10,0,26100,supported?0:-7};};
        a.validateEndpoint=[this](std::wstring_view id,Flow f){++validations;selected=f;if(validationFailAt==validations)return -10;if(id==L"headphones"||id==L"speakers")return f==Flow::output?0:-11;if(id==L"mic"||id==L"usb-mic")return f==Flow::input?0:-11;return -12;};
        a.current=[this](Flow f,Role r,std::wstring&result){++reads;const auto n=static_cast<std::size_t>(r);result=(f==Flow::output?outputs:inputs)[n];if(badReadback&&!writes.empty()&&reads==3)result=L"external";return 0;};
        a.select=[this](std::wstring_view id,Role role){writes.push_back({std::wstring(id),role});auto&values=selected==Flow::output?outputs:inputs;const auto n=static_cast<std::size_t>(role);
            if(reenter){reenter=false;check(!selector->select(selected,id).succeeded(),"Reentrant mixer mutation is rejected before another write");}
            if(rollbackFailure&&writes.size()>2)return -30;
            if(writes.size()==failAt){if(applyFailure)values[n]=id;if(externalChange)values[0]=L"external";return -20;}
            if(writes.size()==throwAt)throw std::runtime_error("Injected setter failure");
            values[n]=id;return 0;
        };return a;
    }
};
void success(){Graph g;gpu::AudioDefaultDeviceSelector selector(g.access());g.selector=&selector;
    check(g.capabilities==0&&g.reads==0&&g.writes.empty(),"Constructing selector is inert");
    const auto result=selector.select(Flow::output,L"headphones");
    check(result.succeeded()&&result.changed&&result.writes==2&&result.restored==0,"Two normal default roles are set and independently read back");
    check(g.outputs[0]==L"headphones"&&g.outputs[1]==L"headphones"&&g.outputs[2]==L"communications-output","Communications default is preserved");
    check(g.inputs[0]==L"mic"&&g.inputs[1]==L"mic","Output switch does not touch input defaults");
    check(!selector.select(Flow::output,L"headphones").changed&&g.writes.size()==2,"Already-selected defaults require no writes");
    check(selector.select(Flow::input,L"usb-mic").succeeded()&&g.inputs[0]==L"usb-mic"&&g.inputs[1]==L"usb-mic"&&g.inputs[2]==L"communications-input","Input selection validates capture flow and preserves communications role");
    g.reenter=true;check(selector.select(Flow::output,L"speakers").succeeded(),"Outer explicit selection completes after rejected reentrant call");
}
void failures(){Graph unsupported;unsupported.supported=false;gpu::AudioDefaultDeviceSelector unavailable(unsupported.access());check(!unavailable.select(Flow::output,L"headphones").succeeded()&&unsupported.writes.empty()&&unsupported.validations==0,"Missing version/interface capability prevents all endpoint accesses and writes");
    Graph g;gpu::AudioDefaultDeviceSelector s(g.access());check(!s.select(Flow::output,L"").succeeded()&&!s.select(Flow::input,L"headphones").succeeded()&&g.writes.empty(),"Empty ID and wrong data flow cannot mutate defaults");
    check(!s.select(Flow::output,std::wstring{L'a',L'\0',L'b'}).succeeded()&&!s.select(static_cast<Flow>(9),L"headphones").succeeded(),"Embedded terminators and invalid flow are rejected");
    Graph failed;failed.failAt=2;gpu::AudioDefaultDeviceSelector rollback(failed.access());const auto result=rollback.select(Flow::output,L"headphones");
    check(result.status==-20&&result.rollbackStatus==0&&result.restored==1,"Second-role native error restores first role and preserves original HRESULT");
    check(failed.outputs[0]==L"speakers"&&failed.outputs[1]==L"speakers"&&failed.writes.back().role==Role::console,"Rollback restores only actually changed defaults");
    Graph applied;applied.failAt=2;applied.applyFailure=true;gpu::AudioDefaultDeviceSelector reverse(applied.access());const auto reversed=reverse.select(Flow::output,L"headphones");
    check(reversed.restored==2&&applied.writes[2].role==Role::multimedia&&applied.writes[3].role==Role::console,"Setter that applies before failing is read back and restored in reverse order");
    Graph concurrent;concurrent.failAt=2;concurrent.externalChange=true;gpu::AudioDefaultDeviceSelector preserve(concurrent.access());const auto observed=preserve.select(Flow::output,L"headphones");
    check(observed.concurrentChange&&concurrent.outputs[0]==L"external"&&concurrent.writes.size()==2,"Rollback never overwrites an observed external mixer change");
    Graph broken;broken.failAt=2;broken.rollbackFailure=true;gpu::AudioDefaultDeviceSelector incomplete(broken.access());const auto partial=incomplete.select(Flow::output,L"headphones");
    check(partial.status==-20&&partial.rollbackStatus==-30&&!partial.succeeded(),"Rollback failure remains explicit rather than claiming restored defaults");
    Graph unplug;unplug.validationFailAt=3;gpu::AudioDefaultDeviceSelector removed(unplug.access());const auto unplugged=removed.select(Flow::output,L"headphones");
    check(unplugged.status==-10&&unplugged.restored==1&&unplug.outputs[0]==L"speakers","Hot-unplug before second write cancels selection and restores prior role");
    Graph throws;throws.throwAt=2;gpu::AudioDefaultDeviceSelector exception(throws.access());const auto thrown=exception.select(Flow::output,L"headphones");
    check(!thrown.succeeded()&&thrown.restored==1&&throws.outputs[0]==L"speakers","Exceptional backend failure still follows bounded rollback");
    Graph read;read.badReadback=true;gpu::AudioDefaultDeviceSelector mismatch(read.access());const auto wrong=mismatch.select(Flow::output,L"headphones");
    check(!wrong.succeeded()&&wrong.concurrentChange&&wrong.restored==1,"Success HRESULT with wrong default readback is not reported as successful selection");
}
}
int main(int argc,char**argv){try{success();failures();
#ifdef _WIN32
    if(argc==2&&std::string_view(argv[1])=="--native-capability"){
        // The only optional native call is OS version + COM interface probing.
        // Do not call current()/validateEndpoint()/select(): no user's audio
        // endpoint, volume, stream, or routing is read or changed by this test.
        gpu::AudioDefaultDeviceSelector native(gpu::nativeAudioDefaultDeviceAccess());const auto result=native.capability();
        check(result.supported?(result.windowsMajor==10&&result.windowsBuild>=14393&&result.status>=0):result.status<0,"Native capability reports guarded OS/interface availability honestly");
        std::cout<<"Native PolicyConfig capability: supported="<<result.supported<<" Windows="<<result.windowsMajor<<'.'<<result.windowsMinor<<" build="<<result.windowsBuild<<" HRESULT="<<result.status<<"; no endpoint read or mutation\n";
    }else
#endif
    {check(argc==1,"No test arguments, or Windows-only --native-capability");(void)argv;}
    std::cout<<"PASS "<<checks<<" isolated audio default-device checks\n";return 0;
}catch(const std::exception&e){std::cerr<<"FAIL after "<<checks<<": "<<e.what()<<'\n';return 1;}}
