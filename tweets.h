#pragma once

subjects::subject<string> chunkbus;
auto chunkout = chunkbus.get_subscriber();
auto sendchunk = [] (const string& chunk) {
    chunkout.on_next(chunk);
};
auto chunk$ = chunkbus.get_observable();

//
// recover lines of text from chunk stream
//

auto isEndOfTweet = [](const string& s){
    if (s.size() < 2) return false;
    auto it0 = s.begin() + (s.size() - 2);
    auto it1 = s.begin() + (s.size() - 1);
    return *it0 == '\r' && *it1 == '\n';
};

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

// create strings split on \r
auto string$ = chunk$ |
    concat_map([](const string& s){
        auto splits = split(s, "\r\n");
        return iterate(move(splits));
    }) |
    filter([](const string& s){
        return !s.empty();
    }) |
    publish() |
    ref_count();

#if 0
int group = 0;
auto linewindow$ = string$ |
    group_by([](const string& s) {
        return isEndOfTweet(s) ? group++ : group;
    });

// reduce the strings for a line into one string
auto line$ = linewindow$ |
    flat_map([](const grouped_observable<int, string>& w$){
        return w$ | start_with<string>("") | sum();
    });
#else
// filter to last string in each line
auto close$ = string$ |
    filter(isEndOfTweet) |
    rxo::map([](const string&){return 0;});

// group strings by line
auto linewindow$ = string$ |
    window_toggle(close$ | start_with(0), [](int){return close$;});

// reduce the strings for a line into one string
auto line$ = linewindow$ |
    flat_map([](const observable<string>& w) {
        return w | start_with<string>("") | sum();
    });
#endif

auto tweetthread = observe_on_new_thread();

auto tweet$ = line$ |
    filter([](const string& s){
        return s.size() > 2 && s.find_first_not_of("\r\n") != string::npos;
    }) | 
    observe_on(tweetthread) |
    rxo::map([](const string& line){
        // try {
            return make_shared<const json>(json::parse(line));
        // } catch(...) {
        //     cerr << line << endl;
        //     throw;
        // }
    });
