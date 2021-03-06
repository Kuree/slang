//------------------------------------------------------------------------------
// Time.cpp
// Contains various time-related utilities and functions
//
// File is under the MIT license; see LICENSE for details
//------------------------------------------------------------------------------
#include "slang/numeric/Time.h"

#include <fmt/format.h>

#include "slang/diagnostics/DeclarationsDiags.h"
#include "slang/diagnostics/PreprocessorDiags.h"
#include "slang/symbols/Scope.h"
#include "slang/syntax/AllSyntax.h"
#include "slang/util/String.h"
#include "slang/util/StringTable.h"

namespace slang {

const static StringTable<TimeUnit> strToUnit = {
    { "s", TimeUnit::Seconds },       { "ms", TimeUnit::Milliseconds },
    { "us", TimeUnit::Microseconds }, { "ns", TimeUnit::Nanoseconds },
    { "ps", TimeUnit::Picoseconds },  { "fs", TimeUnit::Femtoseconds }
};

bool suffixToTimeUnit(string_view timeSuffix, TimeUnit& unit) {
    return strToUnit.lookup(timeSuffix, unit);
}

string_view timeUnitToSuffix(TimeUnit unit) {
    switch (unit) {
        case TimeUnit::Seconds:
            return "s";
        case TimeUnit::Milliseconds:
            return "ms";
        case TimeUnit::Microseconds:
            return "us";
        case TimeUnit::Nanoseconds:
            return "ns";
        case TimeUnit::Picoseconds:
            return "ps";
        case TimeUnit::Femtoseconds:
            return "fs";
    }
    THROW_UNREACHABLE;
}

TimeScaleValue::TimeScaleValue(string_view str) {
    size_t idx;
    optional<int> i = strToInt(str, &idx);
    if (!i)
        throw std::invalid_argument("Not a valid timescale magnitude");

    while (idx < str.size() && str[idx] == ' ')
        idx++;

    TimeUnit u;
    if (idx >= str.size() || !suffixToTimeUnit(str.substr(idx), u))
        throw std::invalid_argument("Time value suffix is missing or invalid");

    auto tv = fromLiteral(double(*i), u);
    if (!tv)
        throw std::invalid_argument("Invalid time scale value");

    *this = *tv;
}

optional<TimeScaleValue> TimeScaleValue::fromLiteral(double value, TimeUnit unit) {
    if (value == 1)
        return TimeScaleValue(unit, TimeScaleMagnitude::One);
    if (value == 10)
        return TimeScaleValue(unit, TimeScaleMagnitude::Ten);
    if (value == 100)
        return TimeScaleValue(unit, TimeScaleMagnitude::Hundred);

    return std::nullopt;
}

std::string TimeScaleValue::toString() const {
    std::string result = std::to_string(int(magnitude));
    result.append(timeUnitToSuffix(unit));
    return result;
}

bool TimeScaleValue::operator>(const TimeScaleValue& rhs) const {
    // Unit enum is specified in reverse order, so check in the opposite direction.
    if (unit < rhs.unit)
        return true;
    if (unit > rhs.unit)
        return false;
    return magnitude > rhs.magnitude;
}

bool TimeScaleValue::operator==(const TimeScaleValue& rhs) const {
    return unit == rhs.unit && magnitude == rhs.magnitude;
}

std::ostream& operator<<(std::ostream& os, const TimeScaleValue& tv) {
    return os << tv.toString();
}

double TimeScale::apply(double value, TimeUnit unit) const {
    // First scale the value by the difference between our base and the provided unit.
    // TimeUnits are from 0-5, so we need 11 entries.
    static constexpr double scales[] = { 1e15, 1e12, 1e9,  1e6,   1e3,  1e0,
                                         1e-3, 1e-6, 1e-9, 1e-12, 1e-15 };
    int diff = int(unit) - int(base.unit);
    double scale = scales[diff + int(TimeUnit::Femtoseconds)] / int(base.magnitude);
    value *= scale;

    // Round the result to the number of decimals implied by the magnitude
    // difference between our base and our precision.
    diff = int(base.unit) - int(precision.unit);
    scale = scales[diff + int(TimeUnit::Femtoseconds)] * int(base.magnitude);
    scale /= int(precision.magnitude);
    value = std::round(value * scale) / scale;

    return value;
}

std::string TimeScale::toString() const {
    return fmt::format("{} / {}", base.toString(), precision.toString());
}

void TimeScale::setFromSyntax(const Scope& scope, const TimeUnitsDeclarationSyntax& syntax,
                              optional<SourceRange>& unitsRange,
                              optional<SourceRange>& precisionRange, bool isFirst) {
    bool errored = false;
    auto handle = [&](Token token, optional<SourceRange>& prevRange, TimeScaleValue& value) {
        // If there were syntax errors just bail out, diagnostics have already been issued.
        if (token.isMissing() || token.kind != TokenKind::TimeLiteral)
            return;

        auto val = TimeScaleValue::fromLiteral(token.realValue(), token.numericFlags().unit());
        if (!val) {
            scope.addDiag(diag::InvalidTimeScaleSpecifier, token.location());
            return;
        }

        if (prevRange) {
            // If the value was previously set, we need to make sure this new
            // value is exactly the same, otherwise we error.
            if (value != *val && !errored) {
                auto& diag = scope.addDiag(diag::MismatchedTimeScales, token.range());
                diag.addNote(diag::NotePreviousDefinition, prevRange->start()) << *prevRange;
                errored = true;
            }
        }
        else {
            // The first time scale declarations must be the first elements in the parent scope.
            if (!isFirst && !errored) {
                scope.addDiag(diag::TimeScaleFirstInScope, token.range());
                errored = true;
            }

            value = *val;
            prevRange = token.range();
        }
    };

    if (syntax.keyword.kind == TokenKind::TimeUnitKeyword) {
        handle(syntax.time, unitsRange, base);
        if (syntax.divider)
            handle(syntax.divider->value, precisionRange, precision);
    }
    else {
        handle(syntax.time, precisionRange, precision);
    }

    if (!errored && unitsRange && precisionRange && precision > base) {
        auto& diag = scope.addDiag(diag::InvalidTimeScalePrecision, *precisionRange);
        diag << *unitsRange;
    }
}

void TimeScale::setDefault(const Scope& scope, optional<TimeScale> directiveTimeScale, bool hasBase,
                           bool hasPrecision) {
    // If no time unit was set, infer one based on the following rules:
    // - If the scope is nested (inside another definition), inherit from that definition.
    // - Otherwise use a `timescale directive if there is one.
    // - Otherwise, look for a time unit in the compilation scope.
    // - Finally use the compilation default.
    if (hasBase && hasPrecision)
        return;

    optional<TimeScale> ts;
    if (scope.asSymbol().kind == SymbolKind::CompilationUnit)
        ts = directiveTimeScale;

    if (!ts)
        ts = scope.getTimeScale();

    if (!hasBase)
        base = ts->base;
    if (!hasPrecision)
        precision = ts->precision;

    // TODO: error if inferred timescale is invalid (because precision > units)
}

bool TimeScale::operator==(const TimeScale& rhs) const {
    return base == rhs.base && precision == rhs.precision;
}

std::ostream& operator<<(std::ostream& os, const TimeScale& ts) {
    return os << ts.toString();
}

} // namespace slang
