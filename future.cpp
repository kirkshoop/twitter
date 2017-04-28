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
    cerr << boolalpha << !!cursor.v;
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

//    auto e = async([&](){
    auto e = 
        each(objProc.begin(), objProc.end(), function<void(It<string>, It<string>)>{
            [](It<string> c, It<string> e) {
                if (c != e) {
//                    cout << (*c).size();// << endl;
                    for (auto& line : split(*c, "\r\n", Split::RemoveDelimiter)){
                        try {
                            cout << endl << setw(2) << tweettext(json::parse(line)) << endl;
                        } catch (const exception& ex){
                            cerr << ex.what() << endl;
                        }
                    }
                }
            }
        });
//    });

    cerr << endl << "begin" << endl;

    // ==== Main proccess
//    async([&](){
        objProc.execProc();
//    });

    return 0;
}