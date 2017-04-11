#pragma once

namespace util {

const float fltmax = numeric_limits<float>::max();

inline string tolower(string s) {
    transform(s.begin(), s.end(), s.begin(), [=](char c){return std::tolower(c);});
    return s;
}

enum class Split {
    KeepDelimiter,
    RemoveDelimiter,
    OnlyDelimiter
};
auto split = [](string s, string d, Split m = Split::KeepDelimiter){
    regex delim(d);
    cregex_token_iterator cursor(&s[0], &s[0] + s.size(), delim, m == Split::KeepDelimiter ? initializer_list<int>({-1, 0}) : (m == Split::RemoveDelimiter ? initializer_list<int>({-1}) : initializer_list<int>({0})));
    cregex_token_iterator end;
    vector<string> splits(cursor, end);
    return splits;
};

inline string utctextfrom(seconds time) {
    stringstream buffer;
    time_t tb = time.count();
    struct tm* tmb = gmtime(&tb);
    buffer << put_time(tmb, "%a %b %d %H:%M:%S %Y");
    return buffer.str();
}
inline string utctextfrom(system_clock::time_point time = system_clock::now()) {
    return utctextfrom(time_point_cast<seconds>(time).time_since_epoch());
}

inline function<observable<string>(observable<long>)> stringandignore() {
    return [](observable<long> s){
        return s.map([](long){return string{};}).ignore_elements();
    };
}

template <class To, class Rep, class Period>
To floor(const duration<Rep, Period>& d)
{
    To t = std::chrono::duration_cast<To>(d);
    if (t > d)
        return t - To{1};
    return t;
}
    
}
