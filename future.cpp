/*
 * Getting timelines by Twitter Streaming API
 * from: https://gist.github.com/komasaru/9c78a278f6916548f146
 */
#include <iostream>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>

#include <sstream>
#include <fstream>
#include <regex>
#include <random>
#include <string>
#include <unordered_set>
#include <unordered_map>

using namespace std;
using namespace std::chrono;

#include <boost/thread.hpp>
using boost::async;
using boost::future;
using boost::promise;

#include <oauth.h>
#include <curl/curl.h>

#include <range/v3/all.hpp>

#include <json.hpp>
using json=nlohmann::json;

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

auto isEndOfTweet = [](const string& s){
    if (s.size() < 2) return false;
    auto it0 = s.begin() + (s.size() - 2);
    auto it1 = s.begin() + (s.size() - 1);
    return *it0 == '\r' && *it1 == '\n';
};

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

inline string tolower(string s) {
    transform(s.begin(), s.end(), s.begin(), [=](char c){return std::tolower(c);});
    return s;
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
        ranges::action::sort |
        ranges::action::unique;

    return words;
}

template<typename T>
struct It
{
    bool end;
    struct Cursor : public enable_shared_from_this<Cursor>
    {
        promise<It<T>> p;
        T v;
        mutex m;
        condition_variable ready;
        atomic_bool gate;

        void set_value(T v) {
            unique_lock<mutex> guard(m);
//            cerr << "+" << flush;
            this->v = v;
            gate = false;
            p.set_value(It<T>(this->shared_from_this()));
//            cerr << "=" << flush;
            ready.wait(guard, [this](){
                return !!gate;
            });
//            cerr << "_" << flush;
        }
    };
    shared_ptr<Cursor> v;

    It() : end(true) {}
    explicit It(shared_ptr<Cursor> v) : end(false), v(v) {
//        cerr << "-" << flush;
    }
    explicit It(nullptr_t) : end(false), v(make_shared<Cursor>()) {
//        cerr << "^" << flush;
    }

    future<It<T>> operator++() {
//        cerr << "#" << flush;
        promise<It<T>> p;
        v->p = std::move(p);
        v->gate = true;
        v->ready.notify_all();
//        cerr << "@" << flush;
        return v->p.get_future();
    }

    T& operator*() {
//        cerr << "*" << flush;
        return v->v;
    }

//    friend bool operator == (const It<T>&, const It<T>&);
};
template<typename T>
inline bool operator == (const It<T>& l, const It<T>& r) {
    return (l.end && r.end) || (!l.end && !r.end && l.v.get() == r.v.get());
}
template<typename T>
inline bool operator != (const It<T>& l, const It<T>& r) {
    return !(l == r);
}

template<typename T>
inline future<void> each(future<It<T>> next, It<T> end, function<void(It<T>, It<T>)> f) {
//    cerr << "`" << flush;
    return next.then([=](future<It<T>> v){
        auto cursor = v.get();
//        cerr << "{" << flush;
        f(cursor, end);
//        cerr << "}" << flush;
        if (cursor == end) {
//            cerr << "$" << flush;
            promise<void> p;
            p.set_value();
            return p.get_future();
        }
//        cerr << "?" << flush;
        auto next = ++cursor;
//        cerr << "%" << flush;
        return each(std::move(next), end, f);
    });
}

size_t fncCallback(char* ptr, size_t size, size_t nmemb, It<string>::Cursor* cursor) {
    int iRealSize = size * nmemb;
    string str;
    str.append(ptr, iRealSize);
//    cerr << "." << flush;
    cursor->set_value(str);
    return iRealSize;
}

class Proc
{
    const char* cUrl;
    const char* cConsKey;
    const char* cConsSec;
    const char* cAtokKey;
    const char* cAtokSec;
    CURL        *curl;
    char*       cSignedUrl;
    It<string>  cursor;
public:
    Proc(const char*, const char*, const char*, const char*, const char*);
    void execProc();

    future<It<string>> begin();
    It<string> end();
};

// Constructor
Proc::Proc(
    const char* cUrl,
    const char* cConsKey, const char* cConsSec,
    const char* cAtokKey, const char* cAtokSec) : cursor(nullptr)
{
    this->cUrl     = cUrl;
    this->cConsKey = cConsKey;
    this->cConsSec = cConsSec;
    this->cAtokKey = cAtokKey;
    this->cAtokSec = cAtokSec;
}

void Proc::execProc()
{
    // ==== cURL Initialization
    curl_global_init(CURL_GLOBAL_ALL);
    curl = curl_easy_init();
    if (!curl) {
        cout << "[ERROR] curl_easy_init" << endl;
        curl_global_cleanup();
        return;
    }

    // ==== cURL Setting
    // - URL, POST parameters, OAuth signing method, HTTP method, OAuth keys
    cSignedUrl = oauth_sign_url2(
        cUrl, NULL, OA_HMAC, "GET",
        cConsKey, cConsSec, cAtokKey, cAtokSec
    );
    // - URL
    curl_easy_setopt(curl, CURLOPT_URL, cSignedUrl);
    // - User agent name
    curl_easy_setopt(curl, CURLOPT_USERAGENT, "mk-mode BOT");
    // - HTTP STATUS >=400 ---> ERROR
    curl_easy_setopt(curl, CURLOPT_FAILONERROR, 1);
    // - Callback function
    curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, fncCallback);
    // - Write data
    curl_easy_setopt(curl, CURLOPT_WRITEDATA, (void *)cursor.v.get());

    // ==== Execute
    int iStatus = curl_easy_perform(curl);
    if (!iStatus)
        cout << "[ERROR] curl_easy_perform: STATUS=" << iStatus << endl;

    // ==== cURL Cleanup
    curl_easy_cleanup(curl);
    curl_global_cleanup();
}

future<It<string>> Proc::begin() {
//    cerr << boolalpha << !!cursor.v;
    return cursor.v->p.get_future();
}

It<string> Proc::end() {
    return It<string>();
}

string settingsFile;
json settings;

int main(int /*argc*/, const char *argv[])
{
    auto exefile = string{argv[0]};
    
    cerr << "exe = " << exefile.c_str() << endl;

    auto exedir = exefile.substr(0, exefile.find_last_of('/'));

    cerr << "dir = " << exedir.c_str() << endl;

    auto exeparent = exedir.substr(0, exedir.find_last_of('/'));

    cerr << "parent = " << exeparent.c_str() << endl;

    settingsFile = exedir + "/settings.json";

    cerr << "settings = " << settingsFile.c_str() << endl;

    ifstream i(settingsFile);
    if (i.good()) {
        i >> settings;
    }

    string CONS_KEY = settings["ConsumerKey"];
    string CONS_SEC = settings["ConsumerSecret"];
    string ATOK_KEY = settings["AccessTokenKey"];
    string ATOK_SEC = settings["AccessTokenSecret"];

    // ==== Constants - URL
    const char *URL = "https://stream.twitter.com/1.1/statuses/sample.json?language=en";

    // ==== Instantiation
    Proc objProc(URL, CONS_KEY.c_str(), CONS_SEC.c_str(), ATOK_KEY.c_str(), ATOK_SEC.c_str());

    using Handler = function<void(const string&, const vector<string>&)>;
    vector<Handler> handlers;

    struct Stat {
        int count;
        unordered_map<string, int> words;
    };
    auto start = steady_clock::now();
    auto keep = seconds(60);
    auto every = seconds(5);
    deque<Stat> stat;
    stat.resize(keep/every);
    handlers.push_back(Handler{[&](const string& , const vector<string>& w) -> void {

        auto end = steady_clock::now();

        if (end - start > keep) {
            start += every;
            stat.pop_front();
            stat.resize(keep/every);
        }

        ++stat.back().count;
        for (auto& word : w) {
            ++stat.back().words[word];
        }

        int count = 0;
        unordered_map<string, int> words;
        for (auto& s : stat) {
            count += s.count;
            for(auto& word : s.words) {
                words[word.first] += word.second;
            }
        }

        vector<pair<string, int>> top = words |
            ranges::view::transform([&](const pair<string, int>& word){
                return word;
            });

        top |=
            ranges::action::sort([](const pair<string, int>& l, const pair<string, int>& r){
                return l.second > r.second;
            });

        top.resize(min(int(top.size()), 10));

        cout << count << " - ";
        for(auto& t : top){
            cout << t.first << ", ";
        }
        cout << endl;
        //cout << endl << t << endl;
    }});

    string partial;

    auto e = 
        each(objProc.begin(), objProc.end(), function<void(It<string>, It<string>)>{
            [&](It<string> c, It<string> e) {
                if (c != e) {
                    //
                    // split chunks and group into tweets
                    string chunk = partial + *c;
                    partial.clear();
                    auto chunks = split(chunk, "\r\n");
                    string line;
                    for (auto& chunk : chunks){
                        if (!isEndOfTweet(chunk)) {
                            line = chunk;
                            partial = chunk;
                            continue;
                        }
                        partial.clear();
                        //cerr << line.substr(0, 3) << " .. " << line.substr(max(0, int(line.size())-3)) << endl;
                        try {
                            //
                            // parse tweets
                            auto text = tweettext(json::parse(line));
                            auto words = splitwords(text);

                            //
                            // publish tweets - multicast
                            for(auto& f : handlers) {
                                f(text, words);
                            }
                        } catch (const exception& ex){
                            cerr << ex.what() << endl;
                        }
                    }
                }
            }
        });

    objProc.execProc();

    return 0;
}