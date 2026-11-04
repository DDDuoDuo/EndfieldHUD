// Notes/centre.json (Mac 1.3.0 NotesCentreDocument / NotesCentreStore,
// Sources/NotesCanvas.swift:6-78; NotesNoteStyle, NotesFormattingControls.swift:
// 33-71) and the dense centre/Archive capture (ArchiveCanvas.swift:1349-1358).
// The fixture is hand-written in the Mac's JSONEncoder(.sortedKeys) form, with
// "\/" escapes, from those lines; synthetic data in temporary roots only.
#include "core/data/notes_centre_store.hpp"
#include <filesystem>
#include <fstream>
#include <functional>
#include <iostream>
#include <iterator>

using namespace ehud::data;
namespace fs=std::filesystem;
namespace n=endfield::core::notes;
namespace {
unsigned checks{};
void check(bool value,const std::string& why){++checks;if(!value)throw std::runtime_error(why);}
struct Temporary {
    fs::path path=fs::canonical(fs::temp_directory_path())/("EndfieldHUD-notes-centre-"+makeUUID());
    Temporary(){check(fs::create_directory(path),"Create a temporary data root");}
    ~Temporary(){std::error_code ignored;fs::remove_all(path,ignored);}
};
void write(const fs::path& file,std::string_view bytes){fs::create_directories(file.parent_path());std::ofstream out(file,std::ios::binary);out.write(bytes.data(),static_cast<std::streamsize>(bytes.size()));check(bool(out),"Write a synthetic file");}
std::string read(const fs::path& file){std::ifstream in(file,std::ios::binary);return {std::istreambuf_iterator<char>(in),{}};}
bool rejected(std::string_view bytes){try{(void)validatedNotesCentre(bytes);}catch(const StoreError& e){return e.code()==StoreErrorCode::invalid;}return false;}
// A Mac 1.3.0 centre.json: lowercase task UUIDs (UUID(uuidString:) accepts
// them), a done task that keeps its dot, a dot for a task that no longer
// exists, a 12 pt default-styled run (NotesTextStyle() written in full) next
// to a coloured bold run, a "\/" escape, and unknown keys at every level.
constexpr std::string_view macFile=R"({"extra":1,"flagged":["0b5e8f3a-1c2d-4e5f-8a9b-0c1d2e3f4a5b","11111111-2222-4333-8444-555555555555","FFFFFFFF-2222-4333-8444-555555555555"],"isFolded":true,"richText":{"note":"x","runs":[{"length":3,"location":0,"style":{"bold":false,"fontSize":12,"italic":false,"strikethrough":false,"underline":false}},{"length":2,"location":3,"style":{"bold":true,"color":{"alpha":1,"blue":0.25,"green":0.5,"red":1},"fontName":"NotesansJP-Bold","fontSize":18,"italic":false,"strikethrough":true,"underline":false,"wider":2}}],"version":1},"style":{"bold":false,"color":"EEEEEE","family":"Noto Sans KR","italic":true,"size":16,"underline":false,"weight":"x"},"tab":"text","tasks":[{"id":"0b5e8f3a-1c2d-4e5f-8a9b-0c1d2e3f4a5b","isChecked":false,"text":"Ship 1\/2"},{"id":"11111111-2222-4333-8444-555555555555","isChecked":true,"pinned":true,"text":"Done task"}],"text":"Hello","version":1})";
}
int main(){try{
    // Decode exactly as NotesCentreDocument.init(from:) (NotesCanvas.swift:26-36).
    const auto doc=validatedNotesCentre(macFile);
    check(doc.version==1&&doc.tasks.size()==2&&doc.tasks[0].id=="0B5E8F3A-1C2D-4E5F-8A9B-0C1D2E3F4A5B"&&doc.tasks[0].text=="Ship 1/2"&&!doc.tasks[0].isChecked&&doc.tasks[1].isChecked,"Tasks keep order, text and state; UUIDs canonical");
    check(doc.text=="Hello"&&doc.tab==NotesCentreDocument::Tab::text&&doc.isFolded&&doc.flagged.size()==3&&doc.flagged.contains("0B5E8F3A-1C2D-4E5F-8A9B-0C1D2E3F4A5B"),"Text, tab, fold and the flagged set");
    check(doc.style.family==std::optional<std::string>("Noto Sans KR")&&doc.style.size==16&&doc.style.italic&&doc.style.color=="EEEEEE","Note style");
    check(doc.richText&&doc.richText->runs.size()==2&&doc.richText->runs[0].style==n::TextStyle{}&&doc.richText->runs[1].style.color==std::optional(n::RGBA{1,.5,.25,1})&&doc.richText->runs[1].style.strikethrough,"Runs decode structurally, the 12 pt default run included");
    // showsDot / hasDot / doneCount (NotesCanvas.swift:39-42).
    check(doc.showsDot(doc.tasks[0])&&!doc.showsDot(doc.tasks[1])&&doc.hasDot()&&doc.doneCount()==1,"A done task keeps its flag but shows no dot");
    // Encode: sorted keys, unknown keys dropped at every level, round trip equal.
    const auto encoded=encodeNotesCentre(doc);const auto json=Json::parse(encoded);
    check(!json.contains("extra")&&!json["richText"].contains("note")&&!json["richText"]["runs"].array()[1]["style"].contains("wider")&&!json["style"].contains("weight")&&!json["tasks"].array()[1].contains("pinned"),"Unknown keys are dropped like the Mac encoder");
    check(decodeNotesCentre(encoded)==doc&&json==Json::parse(encodeNotesCentre(decodeNotesCentre(encoded))),"Mac file in, Windows out, semantically equal and stable");
    check(json["tasks"].array()[0]["id"].string()=="0B5E8F3A-1C2D-4E5F-8A9B-0C1D2E3F4A5B"&&json["richText"]["runs"].array()[0]["style"]["fontSize"].number()==12&&!json["richText"]["runs"].array()[0]["style"].contains("fontName")&&!json["richText"]["runs"].array()[0]["style"].contains("color"),"Uppercase UUIDs; the default run stays a run; nil optionals omitted");
    {   const auto first=encoded.find('{');const auto keys=encoded.substr(first,40);check(keys.starts_with("{\"flagged\""),"Keys are written sorted");}
    // Defaults: absent and explicit null both decode as the defaults; the
    // encoder then writes every field except richText and style.family.
    for(const auto bytes:{std::string_view("{}"),std::string_view(R"({"version":null,"tasks":null,"text":null,"richText":null,"style":null,"tab":null,"isFolded":null,"flagged":null})")}){
        const auto d=validatedNotesCentre(bytes);check(d==NotesCentreDocument{}&&d.tab==NotesCentreDocument::Tab::todo&&d.style.size==14&&d.style.color=="EEEEEE"&&!d.style.family,"Missing keys take the defaults");
        const auto j=Json::parse(encodeNotesCentre(d));check(j["version"].integer()==1&&j["tasks"].array().empty()&&j["text"].string().empty()&&j["tab"].string()=="todo"&&!j["isFolded"].boolean()&&j["flagged"].array().empty()&&!j.contains("richText")&&!j["style"].contains("family")&&j["style"]["size"].number()==14,"Every non-optional field is written");}
    {   const auto d=validatedNotesCentre(R"({"style":{"size":20}})");check(d.style.size==20&&d.style.color=="EEEEEE"&&!d.style.bold,"Every style key is optional");}
    // Rejections (set aside by the store): undecodable or !isValid.
    for(const auto bytes:{R"({"version":2})",R"({"version":1.5})",R"({"tab":"notes"})",R"({"tasks":[{"id":"0B5E8F3A-1C2D-4E5F-8A9B-0C1D2E3F4A5B","text":"a"}]})",
            R"({"tasks":[{"id":"0b5e8f3a-1c2d-4e5f-8a9b-0c1d2e3f4a5b","text":"a","isChecked":false},{"id":"0B5E8F3A-1C2D-4E5F-8A9B-0C1D2E3F4A5B","text":"b","isChecked":true}]})",
            R"({"style":{"size":5}})",R"({"style":{"size":145}})",R"({"style":{"color":"GGGGGG"}})",R"({"style":{"color":"EEE"}})",R"({"style":{"family":""}})",R"({"isFolded":1})",R"({"flagged":["not-a-uuid"]})",
            R"({"richText":{"runs":[{"length":1,"location":0,"style":{"fontSize":12,"bold":false,"italic":false,"underline":false}}],"version":1}})",
            R"({"richText":{"runs":[{"length":1,"location":0,"style":{"fontSize":12,"bold":false,"italic":false,"underline":false,"strikethrough":false,"color":{"red":1,"green":1,"blue":1}}}],"version":1}})",
            R"([])","not json"})
        check(rejected(bytes),std::string("Rejected like the Mac: ")+bytes);
    {   std::string family(257,'a');check(rejected(R"({"style":{"family":")"+family+R"("}})"),"Family over 256 UTF-8 bytes");}
    // Swift UInt32(_, radix: 16) accepts a sign on a 6-Character string.
    {   NotesNoteStyle s;s.color="+EEEEE";check(s.rgb()==std::optional<std::uint32_t>(0xEEEEE),"Leading + parses");s.color="-00000";check(s.rgb()==std::optional<std::uint32_t>(0),"-0 parses");s.color="-0000F";check(!s.rgb(),"Negative fails");s.color="0x1234";check(!s.rgb(),"0x prefix fails");}
    // Not validated on load (NotesCanvas.swift:38): runs that do not fit the
    // text, a task longer than 60 Characters, and flags for absent tasks.
    {   const auto d=validatedNotesCentre(R"({"text":"ab","richText":{"runs":[{"length":9,"location":-1,"style":{"fontSize":500,"bold":false,"italic":false,"underline":false,"strikethrough":false}}],"version":3},"tasks":[{"id":"0B5E8F3A-1C2D-4E5F-8A9B-0C1D2E3F4A5B","text":")"+std::string(200,'x')+R"(","isChecked":false}]})");
        check(d.richText->runs[0].location==-1&&d.richText->version==3&&!d.richRuns(u"ab"),"Invalid runs load and are shown in the base style");
        const auto j=Json::parse(encodeNotesCentre(d));check(j["richText"]["version"].integer()==3&&j["richText"]["runs"].array()[0]["location"].integer()==-1&&j["richText"]["runs"].array()[0]["style"]["fontSize"].number()==500,"Invalid runs are written back unchanged");}
    check(doc.richRuns(u"Hello")&&doc.richRuns(u"Hello")->runs.size()==2&&!doc.richRuns(u"Hell"),"richRuns validates against the UTF-16 text");
    // NotesNoteStyle.textStyle.
    {   NotesNoteStyle s;s.size=200;s.bold=true;check(s.textStyle().fontSize==144&&s.textStyle().bold&&!s.textStyle().fontName&&!s.textStyle().color,"Default family and ink stay nil");
        s.family="HarmonyOS Sans";s.color="FF8000";const auto t=s.textStyle();check(!t.fontName&&t.color==std::optional(n::RGBA{1,128/255.,0,1}),"Only another family and colour are stored");
        s.family="Noto Sans JP";check(s.textStyle().fontName==std::optional<std::string>("Noto Sans JP"),"Other families name the run");}
    check(NotesCentreDocument::richTextDefaults==n::RichTextDefaults::centre()&&n::RichTextDefaults::centre().face==n::RichTextFace::hud&&n::RichTextDefaults::archive()==n::RichTextDefaults::centre()&&
          n::RichTextDefaults::desktopNotes().face==n::RichTextFace::system&&n::RichTextDefaults::desktopNotes().ink==n::RichTextInk::appearance,"Per-store meaning of a nil font and colour");
    // Dense capture keeps the 12 pt default run; the desktop capture is unchanged.
    {   const auto rich=*doc.richRuns(u"Hello");n::RichDocument editor(u"Hello",rich,1024);const auto dense=editor.coveringRichText();
        check(dense.runs.size()==2&&dense.runs[0].location==0&&dense.runs[0].length==3&&dense.runs[0].style==n::TextStyle{}&&dense.runs[1].length==2,"coveringRichText writes the default range as a run");
        check(editor.richText()&&editor.richText()->runs.size()==1,"richText() still drops default runs for desktop notes");
        auto edited=doc;edited.setText("Hello",dense);check(Json::parse(encodeNotesCentre(edited))==Json::parse(encodeNotesCentre(doc)),"Capture after load writes the same runs");
        edited.setText("",dense);check(!edited.richText,"Empty text stores no runs (NotesCanvas.swift:592)");
        n::RichDocument plain(u"abc",std::nullopt,64);const auto all=plain.coveringRichText();check(all.runs.size()==1&&all.runs[0].length==3,"A plain text captures one covering run");
        n::RichDocument empty(u"",std::nullopt,64);check(empty.coveringRichText().runs.empty(),"Empty text has no runs");
        n::RichText dotted;dotted.runs.push_back({0,3,n::TextStyle{.fontName=".SFNS-Bold",.fontSize=12,.bold=true},{}});n::RichDocument system(u"abc",dotted,64);check(!system.coveringRichText().runs[0].style.fontName,"System names are cleared");}
    // Store: missing file = default document, no write until an edit.
    {   Temporary t;NotesCentreStore store(t.path,[]{return 1700000000.;});
        check(store.loadIssue()==NotesCentreStore::LoadIssue::none&&store.document()==NotesCentreDocument{}&&!fs::exists(t.path/"Notes"/"centre.json"),"Missing file loads the default without writing");
        check(!store.update([](auto&){}),"Equal result is a no-op");check(!fs::exists(t.path/"Notes"),"No directory for a no-op");
        check(!store.update([](auto& d){d.style.size=4;})&&store.document().style.size==14,"Invalid result is refused");
        check(store.update([](auto& d){d.tasks.push_back({"0B5E8F3A-1C2D-4E5F-8A9B-0C1D2E3F4A5B","Task",false});d.flagged.insert("0B5E8F3A-1C2D-4E5F-8A9B-0C1D2E3F4A5B");}),"An edit writes");
        const auto bytes=read(t.path/"Notes"/"centre.json");check(bytes==encodeNotesCentre(store.document()),"The whole document is written");
        NotesCentreStore reopened(t.path);check(reopened.document()==store.document()&&reopened.loadIssue()==NotesCentreStore::LoadIssue::none,"Reopen reads the written document");
        for(const auto& entry:fs::directory_iterator(t.path/"Notes"))check(entry.path().filename()=="centre.json"||entry.path().filename()=="centre.json.lock","Atomic replacement leaves no temporary file");
        // Changed on disk: the write throws, the change stays in memory.
        write(t.path/"Notes"/"centre.json",encodeNotesCentre(NotesCentreDocument{}));bool threw{};
        try{store.update([](auto& d){d.isFolded=true;});}catch(const StoreError& e){threw=e.code()==StoreErrorCode::changedOnDisk;}
        check(threw&&store.document().isFolded,"A failed write is reported and the change is kept");}
    // Unreadable files are set aside with the Mac name and never overwritten.
    for(const auto bytes:{std::string(R"({"version":2})"),std::string("{oops"),std::string(R"({"tab":"notes","tasks":[]})")}){
        Temporary t;const auto file=t.path/"Notes"/"centre.json";write(file,bytes);NotesCentreStore store(t.path,[]{return 1700000000.;});
        const auto aside=t.path/"Notes"/"centre-unreadable-2023-11-14T22-13-20Z.json";
        check(store.loadIssue()==NotesCentreStore::LoadIssue::setAside&&store.setAsidePath()==std::optional(aside)&&!fs::exists(file)&&read(aside)==bytes&&store.document()==NotesCentreDocument{},"Set aside with ISO8601 name, original bytes kept");
        check(store.update([](auto& d){d.text="New";})&&Json::parse(read(file))["text"].string()=="New"&&read(aside)==bytes,"The next edit writes a fresh file beside the set-aside one");}
    {   Temporary t;const auto file=t.path/"Notes"/"centre.json";write(file,"{bad");write(t.path/"Notes"/"centre-unreadable-2023-11-14T22-13-20Z.json","older");NotesCentreStore store(t.path,[]{return 1700000000.;});
        check(store.loadIssue()==NotesCentreStore::LoadIssue::setAside&&!store.setAsidePath()&&read(file)=="{bad","A name clash keeps the file in place");
        check(store.update([](auto& d){d.text="x";})&&read(file)=="{bad","...and the store never overwrites it");}
    // Memory-only without a directory (NotesCentreStore(directory: nil)).
    {   NotesCentreStore store(std::nullopt);check(!store.path()&&store.update([](auto& d){d.tab=NotesCentreDocument::Tab::text;})&&store.document().tab==NotesCentreDocument::Tab::text,"Memory-only document");}
    std::cout<<"notes centre store contracts: "<<checks<<" checks passed\n";return 0;
}catch(const std::exception& e){std::cerr<<"notes centre store contracts failed: "<<e.what()<<"\n";return 1;}}
