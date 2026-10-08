#pragma once
#include "modules/event_log.hpp"
#include "core/data/json.hpp"
namespace endfield::modules {
struct EventLogAppearance {bool dark{true};double scale{2};std::array<double,4>accent{250./255,212./255,31./255,1};bool operator==(const EventLogAppearance&)const=default;};
struct EventLogSurface {std::string id;core::Matrix4 local;float opacity{1};std::string action;bool rim{},framed{},outline{},toolbar{};};
struct EventLogPart {std::string rowID;core::Rect full;ehud::data::Json layers;std::vector<EventLogSurface>surfaces;};
struct EventLogArtwork {EventLogPart header,toolbar,empty,scrollbar;std::vector<EventLogPart>rows;};
// Source CALayer-shaped descriptors. Static artwork changes only on model/
// style events; independent feedback/registration surfaces support retained
// numeric transforms. No raster/image/OS services are created here.
EventLogArtwork prepareEventLogArtwork(const EventLogState&,const EventLogAppearance& = {});
ehud::data::Json eventLogOutlinePath(double strokeEnd=1);
}
