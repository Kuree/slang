//------------------------------------------------------------------------------
//! @file Time.h
//! @brief Contains various time-related utilities and functions
//
// File is under the MIT license; see LICENSE for details
//------------------------------------------------------------------------------
#pragma once

#include "slang/text/SourceLocation.h"
#include "slang/util/Util.h"

namespace slang {

class Scope;
struct TimeUnitsDeclarationSyntax;

/// Scale unit for a time value.
enum class TimeUnit : uint8_t {
    Seconds,
    Milliseconds,
    Microseconds,
    Nanoseconds,
    Picoseconds,
    Femtoseconds
};

bool suffixToTimeUnit(string_view timeSuffix, TimeUnit& unit);
string_view timeUnitToSuffix(TimeUnit unit);

/// As allowed by SystemVerilog, time scales can be expressed
/// in one of only a few magnitudes.
enum class TimeScaleMagnitude : uint8_t { One = 1, Ten = 10, Hundred = 100 };

/// A combination of a unit and magnitude for a time scale value.
struct TimeScaleValue {
    TimeUnit unit = TimeUnit::Seconds;
    TimeScaleMagnitude magnitude = TimeScaleMagnitude::One;

    TimeScaleValue() = default;
    TimeScaleValue(TimeUnit unit, TimeScaleMagnitude magnitude) :
        unit(unit), magnitude(magnitude) {}
    TimeScaleValue(string_view str);

    template<size_t N>
    TimeScaleValue(const char (&str)[N]) : TimeScaleValue(string_view(str)) {}

    std::string toString() const;

    static optional<TimeScaleValue> fromLiteral(double value, TimeUnit unit);

    bool operator>(const TimeScaleValue& rhs) const;
    bool operator==(const TimeScaleValue& rhs) const;
    bool operator!=(const TimeScaleValue& rhs) const { return !(*this == rhs); }

    friend std::ostream& operator<<(std::ostream& os, const TimeScaleValue& tv);
};

/// A collection of a base time and a precision value that
/// determines the scale of simulation time steps.
struct TimeScale {
    TimeScaleValue base;
    TimeScaleValue precision;

    TimeScale() = default;
    TimeScale(TimeScaleValue base, TimeScaleValue precision) : base(base), precision(precision) {}

    double apply(double value, TimeUnit unit) const;

    std::string toString() const;

    void setFromSyntax(const Scope& scope, const TimeUnitsDeclarationSyntax& syntax,
                       optional<SourceRange>& unitsRange, optional<SourceRange>& precisionRange,
                       bool isFirst);
    void setDefault(const Scope& scope, optional<TimeScale> directiveTimeScale, bool hasBase,
                    bool hasPrecision);

    bool operator==(const TimeScale& rhs) const;
    bool operator!=(const TimeScale& rhs) const { return !(*this == rhs); }

    friend std::ostream& operator<<(std::ostream& os, const TimeScale& ts);
};

} // namespace slang