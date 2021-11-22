#pragma once

namespace model {

inline string tweettext(const json& tweet) {
    if (!!tweet.count("extended_tweet")) {
        auto ex = tweet["extended_tweet"];
        if (!!ex.count("full_text") && ex["full_text"].is_string()) {
            return ex["full_text"];
        }
    }
    if (!!tweet.count("text") && tweet["text"].is_string()) {
        return tweet["text"];
    }
    return {};
}

inline vector<string> splitwords(const string& text) {

    static const unordered_set<string> ignoredWords{
    // added
    "rt", "like", "just", "tomorrow", "new", "year", "month", "day", "today", "make", "let", "want", "did", "going", "good", "really", "know", "people", "got", "life", "need", "say", "doing", "great", "right", "time", "best", "happy", "stop", "think", "world", "watch", "gonna", "remember", "way",
    "better", "team", "check", "feel", "talk", "hurry", "look", "live", "home", "game", "run", "i'm", "you're", "person", "house", "real", "thing", "lol", "has", "things", "that's", "thats", "fine", "i've", "you've", "y'all", "didn't", "said", "come", "coming", "haven't", "won't", "can't", "don't", 
    "shouldn't", "hasn't", "doesn't", "i'd", "it's", "i'll", "what's", "we're", "you'll", "let's'", "lets", "vs", "win", "says", "tell", "follow", "comes", "look", "looks", "post", "join", "add", "does", "went", "sure", "wait", "seen", "told", "yes", "video", "lot", "looks", "long",
    "e280a6", "\xe2\x80\xa6",
    // http://xpo6.com/list-of-english-stop-words/
    "a", "about", "above", "above", "across", "after", "afterwards", "again", "against", "all", "almost", "alone", "along", "already", "also","although","always","am","among", "amongst", "amoungst", "amount",  "an", "and", "another", "any","anyhow","anyone","anything","anyway", "anywhere", "are", "around", "as",  "at", "back","be","became", "because","become","becomes", "becoming", "been", "before", "beforehand", "behind", "being", "below", "beside", "besides", "between", "beyond", "bill", "both", "bottom","but", "by", "call", "can", "cannot", "cant", "co", "con", "could", "couldnt", "cry", "de", "describe", "detail", "do", "done", "down", "due", "during", "each", "eg", "eight", "either", "eleven","else", "elsewhere", "empty", "enough", "etc", "even", "ever", "every", "everyone", "everything", "everywhere", "except", "few", "fifteen", "fify", "fill", "find", "fire", "first", "five", "for", "former", "formerly", "forty", "found", "four", "from", "front", "full", "further", "get", "give", "go", "had", "has", "hasnt", "have", "he", "hence", "her", "here", "hereafter", "hereby", "herein", "hereupon", "hers", "herself", "him", "himself", "his", "how", "however", "hundred", "ie", "if", "in", "inc", "indeed", "interest", "into", "is", "it", "its", "itself", "keep", "last", "latter", "latterly", "least", "less", "ltd", "made", "many", "may", "me", "meanwhile", "might", "mill", "mine", "more", "moreover", "most", "mostly", "move", "much", "must", "my", "myself", "name", "namely", "neither", "never", "nevertheless", "next", "nine", "no", "nobody", "none", "noone", "nor", "not", "nothing", "now", "nowhere", "of", "off", "often", "on", "once", "one", "only", "onto", "or", "other", "others", "otherwise", "our", "ours", "ourselves", "out", "over", "own","part", "per", "perhaps", "please", "put", "rather", "re", "same", "see", "seem", "seemed", "seeming", "seems", "serious", "several", "she", "should", "show", "side", "since", "sincere", "six", "sixty", "so", "some", "somehow", "someone", "something", "sometime", "sometimes", "somewhere", "still", "such", "system", "take", "ten", "than", "that", "the", "their", "them", "themselves", "then", "thence", "there", "thereafter", "thereby", "therefore", "therein", "thereupon", "these", "they", "thickv", "thin", "third", "this", "those", "though", "three", "through", "throughout", "thru", "thus", "to", "together", "too", "top", "toward", "towards", "twelve", "twenty", "two", "un", "under", "until", "up", "upon", "us", "very", "via", "was", "we", "well", "were", "what", "whatever", "when", "whence", "whenever", "where", "whereafter", "whereas", "whereby", "wherein", "whereupon", "wherever", "whether", "which", "while", "whither", "who", "whoever", "whole", "whom", "whose", "why", "will", "with", "within", "without", "would", "yet", "you", "your", "yours", "yourself", "yourselves", "the"};

    static const string delimiters = R"(\s+)";
    auto words = split(text, delimiters, Split::RemoveDelimiter);

    // exclude entities, urls and some punct from this words list

    static const regex ignore(R"((\xe2\x80\xa6)|(&[\w]+;)|((http|ftp|https)://[\w-]+(.[\w-]+)+([\w.,@?^=%&:/~+#-]*[\w@?^=%&/~+#-])?))");
    static const regex expletives(R"(\x66\x75\x63\x6B|\x73\x68\x69\x74|\x64\x61\x6D\x6E)");

    for (auto& word: words) {
        while (!word.empty() && (word.front() == '.' || word.front() == '(' || word.front() == '\'' || word.front() == '\"')) word.erase(word.begin());
        while (!word.empty() && (word.back() == ':' || word.back() == ',' || word.back() == ')' || word.back() == '\'' || word.back() == '\"')) word.resize(word.size() - 1);
        if (!word.empty() && word.front() == '@') continue;
        word = regex_replace(tolower(word), ignore, "");
        if (!word.empty() && word.front() != '#') {
            while (!word.empty() && ispunct(word.front())) word.erase(word.begin());
            while (!word.empty() && ispunct(word.back())) word.resize(word.size() - 1);
        }
        word = regex_replace(word, expletives, "<expletive>");
    }

    words.erase(std::remove_if(words.begin(), words.end(), [=](const string& w){
        return !(w.size() > 2 && ignoredWords.find(w) == ignoredWords.end());
    }), words.end());

    words |= 
        ranges::actions::sort |
        ranges::actions::unique;

    return words;
}

struct TimeRange
{
    using timestamp = milliseconds;

    timestamp begin;
    timestamp end;
};
bool operator<(const TimeRange& lhs, const TimeRange& rhs){
    return lhs.begin < rhs.begin && lhs.end < rhs.end;
}

using WordCountMap = unordered_map<string, int>;

struct Tweet
{
    Tweet() {}
    explicit Tweet(const json& tweet) 
        : data(make_shared<shared>(shared{tweet})) 
    {}
    struct shared 
    {
        shared() {}
        explicit shared(const json& t) 
            : tweet(t)
            , words(splitwords(tweettext(tweet)))
        {}
        json tweet;
        vector<string> words;
    };
    shared_ptr<const shared> data = make_shared<shared>();
};

struct TweetGroup
{
    deque<Tweet> tweets;
    WordCountMap words;
    int positive = 0;
    int negative = 0;
    int toxic = 0;
};

struct Perspective
{
    float toxicity;
    float spam;
    float inflammatory;
};

struct Model
{
    struct shared 
    {
        string url;
        rxsc::scheduler::clock_type::time_point timestamp;
        int total = 0;
        deque<TimeRange> groups;
        std::map<TimeRange, shared_ptr<TweetGroup>> groupedtweets;
        seconds tweetsstart;
        deque<int> tweetsperminute;
        deque<Tweet> tweets;
        WordCountMap allwords;
        WordCountMap positivewords;
        WordCountMap negativewords;
        WordCountMap toxicwords;
        unordered_map<string, string> sentiment;
        unordered_map<string, Perspective> perspective;
    };
    shared_ptr<shared> data = make_shared<shared>();
};

using Reducer = function<Model(const Model&)>;

auto noop = Reducer([](const Model& m){return m;});

inline function<observable<Reducer>(observable<Reducer>)> nooponerror(string from = string{}) {
    return [=](observable<Reducer> s){
        return s | 
            on_error_resume_next([=](std::exception_ptr ep){
                if (!from.empty()) {
                    cerr << from << " - ";
                }
                cerr << rxu::what(ep) << endl;
                return observable<>::empty<Reducer>();
            }) | 
            repeat();
    };
}

inline function<observable<Reducer>(observable<Tweet>)> noopandignore() {
    return [](observable<Tweet> s){
        return s.map([=](const Tweet&){return noop;}).op(nooponerror()).ignore_elements();
    };
}

struct WordCount
{
    string word;
    int count;
    vector<float> all;
};

int idx = 0;

const int scope_all = 1;
const int scope_all_negative = 2;
const int scope_all_positive = 3;
const int scope_all_toxic = 4;
const int scope_selected = 5;
static int scope = scope_selected;

struct ViewModel
{
    ViewModel() {}
    explicit ViewModel(const Model& m) : m(m) {
        auto& model = *m.data;

        if (scope == scope_selected && idx >= 0 && idx < int(model.groups.size())) {
            assert(model.groups.size() <= model.groupedtweets.size());
            
            auto& window = model.groups.at(idx);
            auto& group = model.groupedtweets.at(window);

            data->scope_words = &data->words;
            data->scope_tweets = &group->tweets;
            data->scope_begin = utctextfrom(duration_cast<seconds>(window.begin));
            data->scope_end = utctextfrom(duration_cast<seconds>(window.end));

            data->words = group->words |
                ranges::views::transform([&](const pair<string, int>& word){
                    return WordCount{word.first, word.second, {}};
                }) |
                ranges::to_vector;

            data->words |=
                ranges::actions::sort([](const WordCount& l, const WordCount& r){
                    return l.count > r.count;
                });
        } else {

            if (scope == scope_all_negative) {
                data->scope_words = &data->negativewords;
                data->negativewords = model.negativewords |
                    ranges::views::transform([&](const pair<string, int>& word){
                        return WordCount{word.first, word.second, {}};
                    }) |
                ranges::to_vector;

                data->negativewords |=
                    ranges::actions::sort([](const WordCount& l, const WordCount& r){
                        return l.count > r.count;
                    });
            } else if (scope == scope_all_positive) {
                data->scope_words = &data->positivewords;
                data->positivewords = model.positivewords |
                    ranges::views::transform([&](const pair<string, int>& word){
                        return WordCount{word.first, word.second, {}};
                    }) |
                ranges::to_vector;

                data->positivewords |=
                    ranges::actions::sort([](const WordCount& l, const WordCount& r){
                        return l.count > r.count;
                    });
            } else if (scope == scope_all_toxic) {
                data->scope_words = &data->toxicwords;
                data->toxicwords = model.toxicwords |
                    ranges::views::transform([&](const pair<string, int>& word){
                        return WordCount{word.first, word.second, {}};
                    }) |
                ranges::to_vector;

                data->toxicwords |=
                    ranges::actions::sort([](const WordCount& l, const WordCount& r){
                        return l.count > r.count;
                    });
            } else {
                data->scope_words = &data->allwords;
            }

            data->allwords = model.allwords |
                ranges::views::transform([&](const pair<string, int>& word){
                    return WordCount{word.first, word.second, {}};
                }) |
                ranges::to_vector;

            data->allwords |=
                ranges::actions::sort([](const WordCount& l, const WordCount& r){
                    return l.count > r.count;
                });

            data->scope_tweets = &model.tweets;
            data->scope_begin = model.groups.empty() ? string{} : utctextfrom(duration_cast<seconds>(model.groups.front().begin));
            data->scope_end = model.groups.empty() ? string{} : utctextfrom(duration_cast<seconds>(model.groups.back().end));
        }

        {
            vector<pair<milliseconds, float>> groups = model.groupedtweets |
                ranges::views::transform([&](const pair<TimeRange, shared_ptr<TweetGroup>>& group){
                    return make_pair(group.first.begin, static_cast<float>(group.second->tweets.size()));
                }) |
                ranges::to_vector;

            groups |=
                ranges::actions::sort([](const pair<milliseconds, float>& l, const pair<milliseconds, float>& r){
                    return l.first < r.first;
                });

            data->groupedtpm = groups |
                ranges::views::transform([&](const pair<milliseconds, float>& group){
                    return group.second;
                }) |
                ranges::to_vector;

            data->maxtpm = data->groupedtpm.size() > 0 ? *ranges::max_element(data->groupedtpm) : 0.0f;
        }

        {
            vector<pair<milliseconds, float>> groups = model.groupedtweets |
                ranges::views::transform([&](const pair<TimeRange, shared_ptr<TweetGroup>>& group){
                    return make_pair(group.first.begin, static_cast<float>(group.second->positive));
                }) |
                ranges::to_vector;

            groups |=
                ranges::actions::sort([](const pair<milliseconds, float>& l, const pair<milliseconds, float>& r){
                    return l.first < r.first;
                });

            data->positivetpm = groups |
                ranges::views::transform([&](const pair<milliseconds, float>& group){
                    return group.second;
                }) |
                ranges::to_vector;
        }

        {
            vector<pair<milliseconds, float>> groups = model.groupedtweets |
                ranges::views::transform([&](const pair<TimeRange, shared_ptr<TweetGroup>>& group){
                    return make_pair(group.first.begin, static_cast<float>(group.second->negative));
                }) |
                ranges::to_vector;

            groups |=
                ranges::actions::sort([](const pair<milliseconds, float>& l, const pair<milliseconds, float>& r){
                    return l.first < r.first;
                });

            data->negativetpm = groups |
                ranges::views::transform([&](const pair<milliseconds, float>& group){
                    return group.second;
                }) |
                ranges::to_vector;
        }

        {
            vector<pair<milliseconds, float>> groups = model.groupedtweets |
                ranges::views::transform([&](const pair<TimeRange, shared_ptr<TweetGroup>>& group){
                    return make_pair(group.first.begin, static_cast<float>(group.second->toxic));
                }) |
                ranges::to_vector;

            groups |=
                ranges::actions::sort([](const pair<milliseconds, float>& l, const pair<milliseconds, float>& r){
                    return l.first < r.first;
                });

            data->toxictpm = groups |
                ranges::views::transform([&](const pair<milliseconds, float>& group){
                    return group.second;
                }) |
                ranges::to_vector;
        }
    }

    Model m;

    struct shared
    {
        vector<WordCount> words;
        vector<WordCount> allwords;
        vector<WordCount> negativewords;
        vector<WordCount> positivewords;
        vector<WordCount> toxicwords;
        
        vector<float> groupedtpm;
        vector<float> positivetpm;
        vector<float> negativetpm;
        vector<float> toxictpm;
        float maxtpm = 0.0f;

        string scope_begin = {};
        string scope_end = {};
        const vector<WordCount>* scope_words = nullptr;
        const deque<Tweet>* scope_tweets = nullptr;
    };

    shared_ptr<shared> data = make_shared<shared>();
};


inline function<observable<ViewModel>(observable<ViewModel>)> reportandrepeat() {
    return [](observable<ViewModel> s){
        return s | 
            on_error_resume_next([](std::exception_ptr ep){
                cerr << rxu::what(ep) << endl;
                return observable<>::empty<ViewModel>();
            }) | 
            repeat();
    };
}

}
