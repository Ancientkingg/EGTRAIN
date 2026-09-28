#include "util/TimeFormat.h"
#include <cstdio>
#include <cmath>
#include <iomanip>
#include <limits>
#include <locale>
#include <regex>
#include <sstream>

std::string formatSimTime(long long simSeconds, long long baseOffsetSeconds) {
    long long total = simSeconds + baseOffsetSeconds;
    if (total < 0) total = 0;
    total %= 86400; // wrap to a single day
    long long h = total / 3600;
    long long m = (total % 3600) / 60;
    long long s = total % 60;
    char buf[16];
    std::snprintf(buf, sizeof(buf), "%02lld:%02lld:%02lld", h, m, s);
    return std::string(buf);
}

long long parseClockToSeconds(const std::string& hhmm) {
    int h = 0, m = 0;
    char extra = 0;
    // require exactly HH:MM with no trailing characters
    if (std::sscanf(hhmm.c_str(), "%d:%d%c", &h, &m, &extra) != 2)
        return -1;
    if (h < 0 || h > 23 || m < 0 || m > 59)
        return -1;
    return static_cast<long long>(h) * 3600 + static_cast<long long>(m) * 60;
}

std::string formatPlannedTime(std::optional<double> value, bool clock, long long base) {
    if (!value) return {};
    if (!std::isfinite(*value) || base < 0 || base >= 86400) return "Invalid";
    std::ostringstream out;
    out.imbue(std::locale::classic());
    out << std::setprecision(std::numeric_limits<double>::max_digits10);
    if (!clock) {
        out << *value;
        return out.str();
    }
    const double total = *value + base;
    const double days = std::floor(total / 86400.0);
    double daySeconds = std::fmod(total, 86400.0);
    if (daySeconds < 0) daySeconds += 86400.0;
    const int hours = static_cast<int>(daySeconds / 3600);
    const int minutes = static_cast<int>(daySeconds / 60) % 60;
    const double seconds = std::abs(std::fmod(daySeconds, 60.0));
    if (days != 0)
        out << (days > 0 ? "+" : "") << std::fixed << std::setprecision(0) << days << "d ";
    out << std::setfill('0') << std::setw(2) << hours << ':' << std::setw(2) << minutes << ':';
    if (seconds < 10) out << '0';
    out << std::fixed << std::setprecision(std::numeric_limits<double>::max_digits10) << seconds;
    std::string formatted = out.str();
    formatted.erase(formatted.find_last_not_of('0') + 1);
    if (formatted.back() == '.') formatted.pop_back();
    return formatted;
}

bool parsePlannedTime(const std::string& text, bool clock, long long base,
        std::optional<double>& value) {
    if (base < 0 || base >= 86400) return false;
    if (text.empty()) { value.reset(); return true; }
    if (value && std::isfinite(*value) && text == formatPlannedTime(value, clock, base)) return true;
    const auto number = [](const std::string& token, double& result) {
        std::istringstream input(token);
        input.imbue(std::locale::classic());
        input >> std::noskipws >> result;
        return input && input.peek() == std::char_traits<char>::eof() && std::isfinite(result);
    };
    double result = 0;
    if (clock) {
        static const std::regex pattern(R"(^(?:\+([0-9]+)d )?([0-9]{2}):([0-9]{2}):([0-9]{2}(?:\.[0-9]+)?)$)");
        std::smatch match;
        if (!std::regex_match(text, match, pattern)) return false;
        double days = 0, hours = 0, minutes = 0, seconds = 0;
        if ((match[1].matched && !number(match[1].str(), days))
                || !number(match[2].str(), hours) || !number(match[3].str(), minutes)
                || !number(match[4].str(), seconds) || hours > 23 || minutes > 59 || seconds >= 60)
            return false;
        result = days * 86400 + hours * 3600 + minutes * 60 + seconds - base;
    } else if (!number(text, result)) return false;
    if (!std::isfinite(result) || result < 0.0) return false;
    value = result;
    return true;
}
