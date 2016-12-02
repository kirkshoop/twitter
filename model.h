#pragma once

namespace model {

struct TimeRange
{
    using timestamp = milliseconds;

    timestamp begin;
    timestamp end;
};
bool operator<(const TimeRange& lhs, const TimeRange& rhs){
    return lhs.begin < rhs.begin && lhs.end < rhs.end;
}

struct TweetGroup
{
    vector<shared_ptr<const json>> tweets;
    std::map<string, int> words;
};

struct Model
{
    rxsc::scheduler::clock_type::time_point timestamp;
    int total = 0;
    deque<TimeRange> groups;
    std::map<TimeRange, shared_ptr<TweetGroup>> groupedtweets;
    seconds tweetsstart;
    deque<int> tweetsperminute;
    shared_ptr<deque<shared_ptr<const json>>> tail = make_shared<deque<shared_ptr<const json>>>();
};

using Reducer = function<Model(Model&)>;

auto noop = Reducer([](Model& m){return std::move(m);});

inline function<observable<Reducer>(observable<Reducer>)> nooponerror() {
    return [](observable<Reducer> s){
        return s | 
            on_error_resume_next([](std::exception_ptr ep){
                cerr << rxu::what(ep) << endl;
                return observable<>::empty<Reducer>();
            }) | 
            repeat(0);
    };
}

inline function<observable<Reducer>(observable<shared_ptr<const json>>)> noopandignore() {
    return [](observable<shared_ptr<const json>> s){
        return s.map([=](const shared_ptr<const json>&){return noop;}).op(nooponerror()).ignore_elements();
    };
}

}
