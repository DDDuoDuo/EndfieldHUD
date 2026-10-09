#pragma once
#include "modules/calendar_model.hpp"
#include <filesystem>
#include <memory>
#include <thread>
namespace endfield::modules {
class CalendarRepository {
public:virtual~CalendarRepository()=default;virtual bool exists()=0;virtual CalendarFile load()=0;virtual void save(const CalendarFile&)=0;
};
// Explicit injected Calendar directory; no default path or real user IO occurs
// in the constructor. One borrowed FIFO executor owns every subsequent call.
class CalendarJSONRepository final:public CalendarRepository {
public:CalendarJSONRepository(std::filesystem::path,CalendarTextRules);bool exists()override;CalendarFile load()override;void save(const CalendarFile&)override;
private:std::filesystem::path directory_,path_;CalendarTextRules rules_;CalendarFile value_;std::optional<std::string>persisted_;std::optional<std::thread::id>owner_;bool loaded_{};void onOwner();
};
}
