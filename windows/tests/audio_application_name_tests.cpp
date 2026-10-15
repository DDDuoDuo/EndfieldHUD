// Packaged audio application names: the ms-resource reference rule (portable)
// and the AppxManifest.xml reader (Windows, synthetic manifests in a private
// temporary directory only; no installed package, process or audio session).
#include "native/audio_application_name.hpp"
#include <chrono>
#include <cstdlib>
#include <fstream>
#include <iostream>
#include <random>
#include <stdexcept>
#include <string>

namespace {
namespace gpu=endfield::native;
unsigned checks{};
void check(bool v,const char*s){++checks;if(!v)throw std::runtime_error(s);}
void references(){
    const std::wstring full=L"Synthetic.Player_1.2.3.0_x64__8wekyb3d8bbwe",name=L"Synthetic.Player";
    auto r=gpu::audioPackageResourceReferences(full,name,L"ms-resource:AppDisplayName");
    check(r.size()==2&&r[0]==L"@{Synthetic.Player_1.2.3.0_x64__8wekyb3d8bbwe?ms-resource://Synthetic.Player/resources/AppDisplayName}"&&r[1]==L"@{Synthetic.Player_1.2.3.0_x64__8wekyb3d8bbwe?ms-resource:///resources/AppDisplayName}","A bare key resolves in the package's resources map, then in the current package");
    r=gpu::audioPackageResourceReferences(full,name,L"ms-resource:/Resources/AppName");
    check(r.size()==1&&r[0]==L"@{"+full+L"?ms-resource://Synthetic.Player/Resources/AppName}","A rooted path keeps its own resource map");
    r=gpu::audioPackageResourceReferences(full,name,L"ms-resource:Resources/AppName");
    check(r.size()==1&&r[0]==L"@{"+full+L"?ms-resource://Synthetic.Player/Resources/AppName}","A relative path naming its resource map is rooted in the package");
    r=gpu::audioPackageResourceReferences(full,name,L"ms-resource://Other.Package/Resources/Title");
    check(r.size()==1&&r[0]==L"@{"+full+L"?ms-resource://Other.Package/Resources/Title}","An absolute reference is kept as written");
    r=gpu::audioPackageResourceReferences(full,name,L"  ms-resource:Strings/Title ");
    check(r.size()==2&&r[0]==L"@{"+full+L"?ms-resource://Synthetic.Player/resources/Strings/Title}","Surrounding blanks are ignored and a map is added when none is named");
    check(gpu::audioPackageResourceReferences(full,name,L"Synthetic Player").empty(),"A literal DisplayName is not a reference");
    for(const auto*bad:{L"ms-resource:",L"ms-resource:/",L"ms-resource:a}b"})check(gpu::audioPackageResourceReferences(full,name,bad).empty(),"An empty or unbalanced key yields no reference");
    check(gpu::audioPackageResourceReferences(L"",name,L"ms-resource:Key").empty()&&gpu::audioPackageResourceReferences(full,L"",L"ms-resource:Key").empty()&&gpu::audioPackageResourceReferences(L"a?b",name,L"ms-resource:Key").empty()&&gpu::audioPackageResourceReferences(full,L"x y",L"ms-resource:Key").empty(),"Invalid package identities yield no reference (nothing is guessed)");
    check(gpu::audioPackageApplicationID(L"Synthetic.Player_8wekyb3d8bbwe!App")==L"App","The application id follows the family name");
    check(gpu::audioPackageApplicationID(L"Synthetic.Player_8wekyb3d8bbwe").empty()&&gpu::audioPackageApplicationID(L"!App").empty()&&gpu::audioPackageApplicationID(L"Family!").empty()&&gpu::audioPackageApplicationID(L"a!b!c").empty(),"Malformed AppUserModelIDs name no application");
}
#ifdef _WIN32
struct TemporaryDirectory {
    std::filesystem::path path;
    TemporaryDirectory(){std::random_device random;path=std::filesystem::temp_directory_path()/("endfield-audio-names-"+std::to_string(random())+"-"+std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));check(std::filesystem::create_directory(path),"Create a private temporary directory");}
    ~TemporaryDirectory(){std::error_code e;std::filesystem::remove_all(path,e);}
};
std::filesystem::path write(const TemporaryDirectory&d,const char*name,const std::string&text){const auto file=d.path/name;std::ofstream out(file,std::ios::binary);out<<text;out.close();check(out.good(),"Write a synthetic manifest");return file;}
void manifests(){
    TemporaryDirectory d;
    const auto modern=write(d,"modern.xml",R"(<?xml version="1.0" encoding="utf-8"?>
<Package xmlns="http://schemas.microsoft.com/appx/manifest/foundation/windows10" xmlns:uap="http://schemas.microsoft.com/appx/manifest/uap/windows10" IgnorableNamespaces="uap">
  <Identity Name="Synthetic.Player" Publisher="CN=Synthetic" Version="1.2.3.0" ProcessorArchitecture="x64"/>
  <Properties>
    <DisplayName>  ms-resource:StoreName </DisplayName>
    <PublisherDisplayName>Synthetic</PublisherDisplayName>
    <Logo>Assets\Logo.png</Logo>
  </Properties>
  <Applications>
    <Application Id="Helper" Executable="Helper.exe" EntryPoint="Helper.App">
      <uap:VisualElements DisplayName="Synthetic Helper" Description="h" BackgroundColor="transparent" Square150x150Logo="a.png" Square44x44Logo="b.png"/>
    </Application>
    <Application Id="App" Executable="Player.exe" EntryPoint="Player.App">
      <uap:VisualElements DisplayName="ms-resource:AppDisplayName" Description="p" BackgroundColor="transparent" Square150x150Logo="a.png" Square44x44Logo="b.png">
        <uap:DefaultTile/>
      </uap:VisualElements>
      <Extensions><uap:Extension Category="windows.protocol"><uap:Protocol Name="synthetic"><uap:DisplayName>Not the app</uap:DisplayName></uap:Protocol></uap:Extension></Extensions>
    </Application>
  </Applications>
</Package>)");
    auto names=gpu::readAudioPackageManifest(modern,L"App");
    check(names&&names->packageName==L"Synthetic.Player"&&names->applicationDisplayName==L"ms-resource:AppDisplayName"&&names->packageDisplayName==L"ms-resource:StoreName","The matching application's VisualElements name and the package name are read across namespaces");
    names=gpu::readAudioPackageManifest(modern,L"Helper");check(names&&names->applicationDisplayName==L"Synthetic Helper","Each application id selects its own VisualElements");
    names=gpu::readAudioPackageManifest(modern,L"Missing");check(names&&names->applicationDisplayName.empty()&&names->packageDisplayName==L"ms-resource:StoreName","An unknown application keeps only the package display name");
    names=gpu::readAudioPackageManifest(modern,L"");check(names&&names->applicationDisplayName.empty(),"Without an AppUserModelID no application is matched");
    // Windows 8.1 manifest: VisualElements in the m2 namespace, literal names.
    const auto legacy=write(d,"legacy.xml",R"(<?xml version="1.0" encoding="utf-8"?>
<Package xmlns="http://schemas.microsoft.com/appx/2010/manifest" xmlns:m2="http://schemas.microsoft.com/appx/2013/manifest">
  <Identity Name="Synthetic.Radio" Publisher="CN=Synthetic" Version="1.0.0.0"/>
  <Properties><DisplayName><![CDATA[Synthetic Radio]]></DisplayName></Properties>
  <Applications><Application Id="Radio"><m2:VisualElements DisplayName="Synthetic Radio App" Description="r"/></Application></Applications>
</Package>)");
    names=gpu::readAudioPackageManifest(legacy,L"Radio");check(names&&names->packageName==L"Synthetic.Radio"&&names->applicationDisplayName==L"Synthetic Radio App"&&names->packageDisplayName==L"Synthetic Radio","Older manifests and CDATA names are read");
    check(gpu::audioPackageResourceReferences(L"Synthetic.Radio_1.0.0.0_neutral__x",names->packageName,names->applicationDisplayName).empty(),"A literal application name is used as written");
    // Refused input: a DTD, malformed XML, no identity, oversized, missing.
    check(!gpu::readAudioPackageManifest(write(d,"dtd.xml",R"(<?xml version="1.0"?><!DOCTYPE Package [<!ENTITY x "Injected">]><Package><Identity Name="&x;"/></Package>)"),L"App"),"A manifest with a DTD is refused (no entity expansion)");
    check(!gpu::readAudioPackageManifest(write(d,"broken.xml","<Package><Identity Name=\"Synthetic\"><Properties></Package>"),L"App"),"A malformed manifest is refused");
    check(!gpu::readAudioPackageManifest(write(d,"anonymous.xml","<Package><Properties><DisplayName>Anonymous</DisplayName></Properties></Package>"),L"App"),"A manifest without a package identity is refused");
    check(!gpu::readAudioPackageManifest(write(d,"nested.xml","<Package><Extra><Identity Name=\"Nested\"/></Extra></Package>"),L"App"),"Only the package's own Identity names it");
    check(!gpu::readAudioPackageManifest(write(d,"large.xml","<Package><Identity Name=\"Large\"/><!--"+std::string(4*1024*1024,'x')+"--></Package>"),L"App"),"A manifest over 4 MiB is refused unread");
    check(!gpu::readAudioPackageManifest(d.path/"missing.xml",L"App")&&!gpu::readAudioPackageManifest(d.path,L"App"),"A missing manifest or a directory yields nothing");
    std::string deep="<Package><Identity Name=\"Deep\"/>";for(int n=0;n<80;++n)deep+="<a>";for(int n=0;n<80;++n)deep+="</a>";deep+="</Package>";
    check(!gpu::readAudioPackageManifest(write(d,"deep.xml",deep),L"App"),"Element depth is bounded");
    check(!gpu::readAudioPackageManifest(write(d,"longname.xml","<Package><Identity Name=\"Long\"/><Properties><DisplayName>"+std::string(5000,'n')+"</DisplayName></Properties></Package>"),L"App"),"A display name over 4096 characters is refused");
}
#endif
}
int main(){
    try{references();
#ifdef _WIN32
        manifests();
#endif
        std::cout<<"PASS "<<checks<<" audio application name checks\n";return 0;}
    catch(const std::exception&e){std::cerr<<"FAIL after "<<checks<<": "<<e.what()<<'\n';return 1;}
}
