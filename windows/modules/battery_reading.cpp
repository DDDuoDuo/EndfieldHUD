#include "modules/battery_reading.hpp"
#include <cmath>

namespace endfield::modules {
namespace {
// BatteryDiagnosticsParsing.integer (BatteryCapacity.swift:85-90).
std::optional<int>integer(double v)noexcept{if(!std::isfinite(v)||v<0||v>1000000||std::trunc(v)!=v)return {};return int(v);}
}
std::optional<int>batteryCycleCount(double value)noexcept{const auto n=integer(value);if(!n||*n>100000)return {};return n;}
std::optional<double>batteryTemperatureCelsius(double hundredths)noexcept{const double celsius=hundredths/100;if(!std::isfinite(celsius)||celsius<-40||celsius>150)return {};return celsius;}
std::optional<int>batteryAdapterWatts(double value)noexcept{const auto n=integer(value);if(!n||*n<1||*n>1000)return {};return n;}
std::optional<int>batteryMinutes(double value)noexcept{const auto n=integer(value);if(!n||*n>100*60)return {};return n;}
BatteryReading gatedBatteryReading(BatteryReading r)noexcept{
    if(!r.present){r.capacity.reset();r.health.reset();r.cycleCount.reset();r.temperatureCelsius.reset();r.minutesToFull.reset();r.minutesToEmpty.reset();}
    if(!r.pluggedIn)r.adapterWatts.reset();
    return r;
}
}
