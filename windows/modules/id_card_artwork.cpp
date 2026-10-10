#include "modules/id_card_artwork.hpp"
#include <stdexcept>

namespace endfield::modules {
IdCardArtwork::IdCardArtwork(ProfileImage card):card_(std::move(card)){
    if(card_.width!=530||card_.height!=204||card_.rgba.size()!=std::size_t(530)*204*4||card_.premultiplied)
        throw std::invalid_argument("The ID card artwork needs the straight 530x204 business card sprite");
}
ProfileImage IdCardArtwork::background(std::array<double,3>accent,const ProfileImage*photo,double zoom,core::Point offset)const{
    const auto themed=idCardThemedArtwork(card_,accent);
    if(!photo)return idCardTexturePixels(themed);
    // renderedImage(original, 412x158, thumbnail zoom/offset, scale 2), then the
    // artwork drawn into the photo's bounds before compositing.
    const auto crop=profileRenderedImage(*photo,1,{412,158},zoom,offset,2);
    return idCardTexturePixels(idCardCompositedBackground(crop,profileResampled(themed,crop.width,crop.height)));
}
ProfileImage IdCardArtwork::hover(std::array<double,3>accent)const{
    auto plate=idCardTexturePixels(idCardHoverArtwork(card_,accent));
    idCardMaskHover(plate,idCardTexturePixels(idCardThemedArtwork(card_,accent)));
    return plate;
}
ProfileImage IdCardArtwork::avatar(const ProfileImage&avatar,int orientation,double zoom,core::Point offset){
    return idCardTexturePixels(profileRenderedImage(avatar,orientation,{136,136},zoom,offset,2));
}
}
