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

using Reducer = function<shared_ptr<Model>(shared_ptr<Model>&)>;

auto noop = Reducer([](shared_ptr<Model>& m){return std::move(m);});

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

struct WordCount
{
    string word;
    int count;
    vector<float> all;
};

int idx = 0;

struct ViewModel
{
    ViewModel() {}
    explicit ViewModel(shared_ptr<Model>& m) : m(m) {
        if (idx >= 0 && idx < int(m->groups.size())) {
            auto& window = m->groups.at(idx);
            auto& group = m->groupedtweets.at(window);

            words = group->words |
                ranges::view::transform([&](const pair<string, int>& word){
                    return WordCount{word.first, word.second, {}};
                });

            words |=
                ranges::action::sort([](const WordCount& l, const WordCount& r){
                    return l.count > r.count;
                });
        }

        {
            vector<pair<milliseconds, float>> groups = m->groupedtweets |
                ranges::view::transform([&](const pair<TimeRange, shared_ptr<TweetGroup>>& group){
                    return make_pair(group.first.begin, static_cast<float>(group.second->tweets.size()));
                });

            groups |=
                ranges::action::sort([](const pair<milliseconds, float>& l, const pair<milliseconds, float>& r){
                    return l.first < r.first;
                });

            groupedtpm = groups |
                ranges::view::transform([&](const pair<milliseconds, float>& group){
                    return group.second;
                });
        }
    }

    shared_ptr<Model> m;

    vector<WordCount> words;
    vector<float> groupedtpm;
};


inline function<observable<ViewModel>(observable<ViewModel>)> reportandrepeat() {
    return [](observable<ViewModel> s){
        return s | 
            on_error_resume_next([](std::exception_ptr ep){
                cerr << rxu::what(ep) << endl;
                return observable<>::empty<ViewModel>();
            }) | 
            repeat(0);
    };
}

}
