#include "native/id_card_captions.hpp"
#ifdef _WIN32
#include "native/layer_scene.hpp"
#include <cmath>
#include <stdexcept>

namespace endfield::native {
namespace {
using Json=ehud::data::Json;
void need(bool v,const char*m){if(!v)throw std::invalid_argument(m);}
// NSFont.systemFont(ofSize:weight:.medium) as the exporter encodes it.
Json mediumFont(double size){
    return Json::Object{{"familyName",".AppleSystemUIFont"},{"postScriptName",".AppleSystemUIFontMedium"},{"pointSize",size},
        {"symbolicTraits",0},{"ascender",size*.966796875},{"descender",size*-.2109375},{"leading",0}};
}
}
IdCardCaptionLeaves idCardCaptionLeaves(const Json&layers){
    need(layers.isObject()&&layers["children"].isArray()&&layers["children"].array().size()<=1024,"Invalid exported native layer root");
    IdCardCaptionLeaves result;std::array<bool,5>found{};
    for(const auto&container:layers["children"].array()){
        if(!container["name"].isString()||!container["name"].string().starts_with("desktop.profile."))continue;
        const auto binding=std::string_view(container["name"].string()).substr(std::string_view("desktop.profile.").size());
        std::size_t index=modules::idCardCaptionBindings.size();
        for(std::size_t i=0;i<modules::idCardCaptionBindings.size();++i)if(modules::idCardCaptionBindings[i]==binding)index=i;
        need(index<found.size()&&!found[index],"Unknown or repeated exported profile caption");
        need(container["children"].isArray()&&container["children"].array().size()==1,"Profile caption container needs exactly one text leaf");
        const auto&leaf=container["children"].array().front();
        need(leaf["kind"].isString()&&leaf["kind"].string()=="text"&&leaf["text"].isObject()&&leaf["id"].isString()&&!leaf["id"].string().empty(),"Exported profile caption is not a text leaf");
        need(leaf["children"].isNull()||(leaf["children"].isArray()&&leaf["children"].array().empty()),"Exported profile caption leaf has children");
        result.leaves[index]=leaf;result.surfaceIDs[index]=leaf["id"].string();found[index]=true;
    }
    for(const bool f:found)need(f,"Exported native layers lack a profile caption");
    return result;
}
modules::IdCardMeasure nativeIdCardMeasure(LayerRasterizer&raster,LayerRasterOptions options){
    return [&raster,options=std::move(options)](std::string_view text,double size){
        need(std::isfinite(size)&&size>0,"Invalid ID card caption size");
        const Json descriptor=Json::Object{{"string",std::string(text)},{"fontSize",size},{"font",mediumFont(size)},{"wrapped",false}};
        // Unbounded single line, like NSString.size(withAttributes:).
        return raster.measureSourceText("desktop.profile.measure",descriptor,1e9,options).width;
    };
}
NativeIdCardCaptions::NativeIdCardCaptions(LayerScene&labels,IdCardCaptionLeaves leaves,LayerRasterOptions options)
    :labels_(&labels),leaves_(std::move(leaves)),options_(std::move(options)),nextRevision_(labels.contentRevision()){
    for(const auto&id:leaves_.surfaceIDs)need(labels.surfaceIndex(id).has_value(),"Profile caption surface is absent from the label scene");
}
bool NativeIdCardCaptions::update(const modules::IdCardBinding&binding){
    ++stats_.updates;
    if(seen_&&binding.revision()==seenRevision_){++stats_.unchanged;return false;}
    need(binding.revision()>0,"Update the ID card binding before painting its captions");const auto captions=binding.captions();
    bool changed{};
    for(std::size_t i=0;i<captions.size();++i){
        if(paintedValid_[i]&&painted_[i]==captions[i])continue;
        const auto content=binding.captionLayer(leaves_.leaves[i],i);
        changed=labels_->updateLocalContent(leaves_.surfaceIDs[i],++nextRevision_,content,options_)||changed;
        painted_[i]=captions[i];paintedValid_[i]=true;++stats_.surfaceUpdates;
    }
    seen_=true;seenRevision_=binding.revision();
    if(!changed)++stats_.unchanged;
    return changed;
}
}
#endif
