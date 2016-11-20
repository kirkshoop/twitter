/*
 * Getting timelines by Twitter Streaming API
 */
#include <iostream>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <oauth.h>
#include <curl/curl.h>
#include <json.hpp>
#include <rxcpp/rx.hpp>
#include <sstream>
#include <fstream>
#include <regex>
#include <deque>
#include <map>
#include <random>

using namespace std;
using namespace std::chrono;
using namespace rxcpp;
using namespace rxcpp::rxo;
using namespace rxcpp::rxs;

using json=nlohmann::json;

#include "tweets.h"

#include "rxcurl.h"
using namespace rxcurl;

#include "imgui.h"

#include "imgui_internal.h"
extern const char*  GetDefaultCompressedFontDataTTFBase85();

#include "imgui_impl_sdl_gl3.h"
#include <GL/glew.h>
#include <SDL.h>


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
    int total = 0;
    deque<TimeRange> groups;
    std::map<TimeRange, shared_ptr<TweetGroup>> groupedtweets;
    seconds tweetsstart;
    deque<int> tweetsperminute;
    deque<shared_ptr<const json>> tail;
};
using Reducer = function<Model(Model&)>;

struct WordCount
{
    string word;
    int count;
    vector<float> all;
};

const unordered_set<string> ignoredWords{
// added
"rt", "like", "just", "tomorrow", "new", "year", "month", "day", "today", "make", "let", "want", "did", "going", "good", "really", "know", "people", "got", "life", "need", "say", "doing", "great", "right", "time", "best", "happy", "stop", "think", "world", "watch", "gonna", "remember", "way",
"better", "team", "check", "feel", "talk", "hurry", "look", "live", "home", "game", "run", "im", "dont", "youre", "person", "house", "real", "thing", "lol", "has", "things", "thats", "fine", "ive", "youve", "yall", "didnt", "said", "come", "coming", "havent", "wont", "cant", "dont", "shouldnt",
"hasnt", "id", "youll", "e280a6",
// http://xpo6.com/list-of-english-stop-words/
"a", "about", "above", "above", "across", "after", "afterwards", "again", "against", "all", "almost", "alone", "along", "already", "also","although","always","am","among", "amongst", "amoungst", "amount",  "an", "and", "another", "any","anyhow","anyone","anything","anyway", "anywhere", "are", "around", "as",  "at", "back","be","became", "because","become","becomes", "becoming", "been", "before", "beforehand", "behind", "being", "below", "beside", "besides", "between", "beyond", "bill", "both", "bottom","but", "by", "call", "can", "cannot", "cant", "co", "con", "could", "couldnt", "cry", "de", "describe", "detail", "do", "done", "down", "due", "during", "each", "eg", "eight", "either", "eleven","else", "elsewhere", "empty", "enough", "etc", "even", "ever", "every", "everyone", "everything", "everywhere", "except", "few", "fifteen", "fify", "fill", "find", "fire", "first", "five", "for", "former", "formerly", "forty", "found", "four", "from", "front", "full", "further", "get", "give", "go", "had", "has", "hasnt", "have", "he", "hence", "her", "here", "hereafter", "hereby", "herein", "hereupon", "hers", "herself", "him", "himself", "his", "how", "however", "hundred", "ie", "if", "in", "inc", "indeed", "interest", "into", "is", "it", "its", "itself", "keep", "last", "latter", "latterly", "least", "less", "ltd", "made", "many", "may", "me", "meanwhile", "might", "mill", "mine", "more", "moreover", "most", "mostly", "move", "much", "must", "my", "myself", "name", "namely", "neither", "never", "nevertheless", "next", "nine", "no", "nobody", "none", "noone", "nor", "not", "nothing", "now", "nowhere", "of", "off", "often", "on", "once", "one", "only", "onto", "or", "other", "others", "otherwise", "our", "ours", "ourselves", "out", "over", "own","part", "per", "perhaps", "please", "put", "rather", "re", "same", "see", "seem", "seemed", "seeming", "seems", "serious", "several", "she", "should", "show", "side", "since", "sincere", "six", "sixty", "so", "some", "somehow", "someone", "something", "sometime", "sometimes", "somewhere", "still", "such", "system", "take", "ten", "than", "that", "the", "their", "them", "themselves", "then", "thence", "there", "thereafter", "thereby", "therefore", "therein", "thereupon", "these", "they", "thickv", "thin", "third", "this", "those", "though", "three", "through", "throughout", "thru", "thus", "to", "together", "too", "top", "toward", "towards", "twelve", "twenty", "two", "un", "under", "until", "up", "upon", "us", "very", "via", "was", "we", "well", "were", "what", "whatever", "when", "whence", "whenever", "where", "whereafter", "whereas", "whereby", "wherein", "whereupon", "wherever", "whether", "which", "while", "whither", "who", "whoever", "whole", "whom", "whose", "why", "will", "with", "within", "without", "would", "yet", "you", "your", "yours", "yourself", "yourselves", "the"};

const auto length = milliseconds(60000);
const auto every = milliseconds(5000);
const auto keep = minutes(4);

inline float  Clamp(float v, float mn, float mx)                       { return (v < mn) ? mn : (v > mx) ? mx : v; }
inline ImVec2 Clamp(const ImVec2& f, const ImVec2& mn, ImVec2 mx)      { return ImVec2(Clamp(f.x,mn.x,mx.x), Clamp(f.y,mn.y,mx.y)); }
inline string tolower(string s) {
    transform(s.begin(), s.end(), s.begin(), [=](char c){return tolower(c);});
    return s;
}

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

auto twitterrequest = [](::rxcurl::rxcurl factory, string URL, string method, string CONS_KEY, string CONS_SEC, string ATOK_KEY, string ATOK_SEC){

    return observable<>::defer([=](){

        string url;
        {
            char* signedurl = nullptr;
            RXCPP_UNWIND_AUTO([&](){
                if (!!signedurl) {
                    free(signedurl);
                }
            });
            signedurl = oauth_sign_url2(
                URL.c_str(), NULL, OA_HMAC, method.c_str(),
                CONS_KEY.c_str(), CONS_SEC.c_str(), ATOK_KEY.c_str(), ATOK_SEC.c_str()
            );
            url = signedurl;
        }

        return factory.create(http_request{url, method})
            .map([](http_response r){
                return r.body.chunks;
            })
            .merge(tweetthread);
    });
};

string tweettext(const json& tweet) {
    if (!!tweet.count("text") && tweet["text"].is_string()) {
        return tweet["text"];
    }
    return {};
}

auto t$ = tweet$ |
    on_error_resume_next([](std::exception_ptr ep){
        cerr << rxu::what(ep) << endl;
        return observable<>::empty<shared_ptr<const json>>();
    }) |
    repeat(std::numeric_limits<int>::max()) |
    publish() |
    ref_count();

int main(int argc, const char *argv[])
{
    // ==== Parse args

    auto selector = string{tolower(argc > 1 ? argv[1] : "")};

    const bool playback = argc == 3 && selector == "playback";
    const bool dumpjson = argc == 7 && selector == "dumpjson";
    const bool dumptext = argc == 7 && selector == "dumptext";
    const bool gui = argc == 6;

    if (!playback &&
        !dumptext &&
        !dumpjson &&
        !gui) {
        printf("twitter PLAYBACK <json file path>\n");
        printf("twitter DUMPJSON <CONS_KEY> <CONS_SECRET> <ATOK_KEY> <ATOK_SECRET> [sample.json | filter.json?track=<topic>]\n");
        printf("twitter DUMPTEXT <CONS_KEY> <CONS_SECRET> <ATOK_KEY> <ATOK_SECRET> [sample.json | filter.json?track=<topic>]\n");
        printf("twitter          <CONS_KEY> <CONS_SECRET> <ATOK_KEY> <ATOK_SECRET> [sample.json | filter.json?track=<topic>]\n");
        return -1;
    }

    int argoffset = 1;
    if (gui) {
        argoffset = 0;
    }
    
    // ==== Twitter keys
    const char *CONS_KEY = argv[1 + argoffset];
    const char *CONS_SEC = argv[2 + argoffset];
    const char *ATOK_KEY = argv[3 + argoffset];
    const char *ATOK_SEC = argv[4 + argoffset];

    // ==== Constants - URL
    string URL = "https://stream.twitter.com/1.1/statuses/";
    if (!playback) {
        URL += argv[5 + argoffset];
        cerr << "url = " << URL.c_str() << endl;
    }

    // ==== Constants - flags
    const bool isFilter = URL.find("/statuses/filter") != string::npos;

    // ==== Dump

    if (dumpjson)
    {
        t$
            .subscribe([=](const shared_ptr<const json>& tw){
                auto& tweet = *tw;
                cout << tweet << "\r\n";
           });
    }

    if (dumptext)
    {
        t$
            .subscribe([=](const shared_ptr<const json>& tw){
                auto& tweet = *tw;
                if (tweet["user"]["name"].is_string() && tweet["user"]["screen_name"].is_string()) {
                    cout << "------------------------------------" << endl;
                    cout << tweet["user"]["name"].get<string>() << " (" << tweet["user"]["screen_name"].get<string>() << ")" << endl;
                    cout << tweettext(tweet) << endl;
                }
           });
    }

    // Setup SDL
    if (SDL_Init(SDL_INIT_VIDEO|SDL_INIT_TIMER) != 0)
    {
        printf("Error: %s\n", SDL_GetError());
        return -1;
    }

    // Setup window
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_FLAGS, SDL_GL_CONTEXT_FORWARD_COMPATIBLE_FLAG);
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_PROFILE_MASK, SDL_GL_CONTEXT_PROFILE_CORE);
    SDL_GL_SetAttribute(SDL_GL_DOUBLEBUFFER, 1);
    SDL_GL_SetAttribute(SDL_GL_DEPTH_SIZE, 24);
    SDL_GL_SetAttribute(SDL_GL_STENCIL_SIZE, 8);
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_MAJOR_VERSION, 3);
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_MINOR_VERSION, 2);
    SDL_DisplayMode current;
    SDL_GetCurrentDisplayMode(0, &current);
    SDL_Window *window = SDL_CreateWindow("Twitter Analysis (ImGui SDL2+OpenGL3)", SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED, 1280, 720, SDL_WINDOW_OPENGL|SDL_WINDOW_RESIZABLE);
    SDL_GLContext glcontext = SDL_GL_CreateContext(window);
    glewInit();

    // Setup ImGui binding
    ImGui_ImplSdlGL3_Init(window);

    // Load Fonts
    // (there is a default font, this is only if you want to change it. see extra_fonts/README.txt for more details)
    ImGuiIO& io = ImGui::GetIO();    
    io.Fonts->AddFontDefault();
    //io.Fonts->AddFontFromFileTTF("../../extra_fonts/Cousine-Regular.ttf", 15.0f);
    //io.Fonts->AddFontFromFileTTF("../../extra_fonts/DroidSans.ttf", 16.0f);
    //io.Fonts->AddFontFromFileTTF("../../extra_fonts/ProggyClean.ttf", 13.0f);
    //io.Fonts->AddFontFromFileTTF("../../extra_fonts/ProggyTiny.ttf", 10.0f);
    //io.Fonts->AddFontFromFileTTF("c:\\Windows\\Fonts\\ArialUni.ttf", 18.0f, NULL, io.Fonts->GetGlyphRangesJapanese());

    // ImGuiIO& io = ImGui::GetIO();
    // io.Fonts->AddFontDefault();
    // ImFontConfig config;
    // config.MergeMode = true;
    //io.Fonts->AddFontFromFileTTF("./NotoEmoji-Regular.ttf", 12.f, &config);
// U+1F1FF U+1F1FC
// U+1F1E6 U+1F1E8
    // const ImWchar glyph_ranges[] = { 0x0100, 0xFFFF, 0 };
    // io.Fonts->AddFontFromFileTTF("./EmojiOneMozilla.ttf", 16.f, &config, glyph_ranges);
    // io.Fonts->AddFontFromFileTTF("./NotoEmoji-Regular.ttf", 16.f, &config, glyph_ranges);
    // io.Fonts->AddFontFromFileTTF("~/source/rxcpp/Rx/v2/examples/twitter/NotoColorEmoji.ttf", 12.f, &config, unicode_ranges);

    ImFont* cloudFont = nullptr;
    // Load embedded ProggyClean.ttf at size 52, disable oversampling
    {
        ImFontConfig font_cfg;
        font_cfg.OversampleH = font_cfg.OversampleV = 1;
        font_cfg.PixelSnapH = true;
        if (font_cfg.Name[0] == '\0') strcpy(font_cfg.Name, "ProggyClean.ttf, 52px");

        const char* ttf_compressed_base85 = GetDefaultCompressedFontDataTTFBase85();
        cloudFont = io.Fonts->AddFontFromMemoryCompressedBase85TTF(ttf_compressed_base85, 52.0f, &font_cfg, io.Fonts->GetGlyphRangesDefault());
    }

    RXCPP_UNWIND_AUTO([&](){
        // Cleanup
        ImGui_ImplSdlGL3_Shutdown();
        SDL_GL_DeleteContext(glcontext);
        SDL_DestroyWindow(window);
        SDL_Quit();
    });

    const ImVec4 clear_color = ImColor(114, 144, 154);
    const float fltmax = numeric_limits<float>::max();

    schedulers::run_loop rl;

    auto mainthreadid = this_thread::get_id();
    auto mainthread = observe_on_run_loop(rl);

    auto poolthread = observe_on_event_loop();

    auto factory = create_rxcurl();

    auto noop = Reducer([=](Model& m){return std::move(m);});

    composite_subscription lifetime;

    subjects::subject<int> framebus;
    auto frameout = framebus.get_subscriber();
    auto sendframe = [=]() {
        frameout.on_next(1);
    };
    auto frame$ = framebus.get_observable();

    auto updategroups = [](
        Model& m,
        milliseconds timestamp, 
        const shared_ptr<const json>& tw = shared_ptr<const json>{}, 
        const vector<string>& words = vector<string>{}) {

        auto rangebegin = duration_cast<minutes>(timestamp - length);
        auto rangeend = rangebegin+duration_cast<minutes>(length);
        auto searchend = timestamp + length;
        auto offset = milliseconds(0);
        for (;rangeend+offset < searchend;offset += duration_cast<milliseconds>(every)){
            auto key = TimeRange{rangebegin+offset, rangeend+offset};
            auto it = m.groupedtweets.find(key);
            if (it == m.groupedtweets.end()) {
                // add group
                m.groups.push_back(key);
                sort(m.groups.begin(), m.groups.end());
                it = m.groupedtweets.insert(make_pair(key, make_shared<TweetGroup>())).first;
            }

            if (!!tw) {
                if (rangebegin+offset <= timestamp && timestamp < rangeend+offset) {
                    it->second->tweets.push_back(tw);

                    for (auto& word: words) {
                        ++it->second->words[word];
                    }
                }
            }
        }

        while(!m.groups.empty() && m.groups.front().begin + keep < m.groups.back().begin) {
            // remove group
            m.groupedtweets.erase(m.groups.front());
            m.groups.pop_front();
        }
    };

    auto groupbuckets = observable<>::interval(every, poolthread) |
        rxo::map([=](long){
            return Reducer([=](Model& m){
                auto rangebegin = time_point_cast<milliseconds>(system_clock::now()).time_since_epoch();
                updategroups(m, rangebegin);
                return std::move(m);
            });
        }) |
        on_error_resume_next([](std::exception_ptr ep){
            cerr << rxu::what(ep) << endl;
            return observable<>::empty<Reducer>();
        }) |
        start_with(noop) |
        as_dynamic();

    auto grouptpm = t$ |
        filter([](const shared_ptr<const json>& tw){
            auto& tweet = *tw;
            return !!tweet.count("timestamp_ms");
        }) |
        group_by([](const shared_ptr<const json>& tw) {
            auto& tweet = *tw;
            auto t = milliseconds(stoll(tweet["timestamp_ms"].get<string>()));
            auto m = duration_cast<minutes>(t);
            return m;
        }) |
        rxo::map([=](grouped_observable<minutes, shared_ptr<const json>> g){
            auto group = g | 
                take_until(observable<>::timer(length * 2), poolthread) | 
                rxo::map([=](const shared_ptr<const json>& tw){
                    auto& tweet = *tw;

                    // exclude entities, urls and some punct from this words list
                    auto text = tweettext(tweet);
                    regex ignore(R"((&[\w]+;)|((http|ftp|https)://[\w-]+(\.[\w-]+)+([\w.,@?^=%&:/~+#-]*[\w@?^=%&/~+#-])?)|['\""])"); //"
                    auto words = split(regex_replace(text, ignore, ""), R"([\(\)\[\]\s,.-:;&?!]+)", Split::RemoveDelimiter);
                    for (auto& word: words) {
                        auto w = tolower(word);
                        word.clear();
                        for(auto& c : w){
                            if(c < 0) {
                                stringstream hexed;
                                hexed << hex << nouppercase << setfill('0') << setw(2) << static_cast<unsigned int>(static_cast<unsigned char>(c));
                                word += hexed.str();
                            } else {
                                word.push_back(c);
                            }
                        }
                    }
                    words.erase(std::remove_if(words.begin(), words.end(), [=](const string& w){
                        return !(w.size() > 1 && ignoredWords.find(w) == ignoredWords.end() && URL.find(w) == string::npos);
                    }), words.end());

                    return Reducer([=](Model& m){
                        auto t = milliseconds(stoll(tweet["timestamp_ms"].get<string>()));

                        updategroups(m, t, tw, words);

                        return std::move(m);
                    });
                }) |
                observe_on(tweetthread) |
                on_error_resume_next([](std::exception_ptr ep){
                    cerr << rxu::what(ep) << endl;
                    return observable<>::empty<Reducer>();
                });
            return group;
        }) |
        merge() |
        start_with(noop) |
        as_dynamic();

    auto windowtpm = t$ |
        filter([](const shared_ptr<const json>& tw){
            auto& tweet = *tw;
            return !!tweet.count("timestamp_ms");
        }) |
        window_with_time(length, every, poolthread) |
        rxo::map([](observable<shared_ptr<const json>> source){
            auto rangebegin = time_point_cast<seconds>(system_clock::now()).time_since_epoch();
            auto tweetsperminute = source | 
                rxo::map([=](const shared_ptr<const json>&) {
                    return Reducer([=](Model& m){

                        static const auto maxsize = duration_cast<seconds>(keep).count()/duration_cast<seconds>(every).count();

                        if (m.tweetsperminute.size() == 0) {
                            m.tweetsstart = rangebegin;
                        }

                        if (rangebegin >= m.tweetsstart) {

                            const auto i = duration_cast<seconds>(rangebegin - m.tweetsstart).count()/duration_cast<seconds>(every).count();

                            while (i >= int(m.tweetsperminute.size())) {
                                m.tweetsperminute.push_back(0);
                            }

                            ++m.tweetsperminute[i];
                        }

                        while(static_cast<long long>(m.tweetsperminute.size()) > maxsize) {
                            m.tweetsstart += duration_cast<seconds>(every);
                            m.tweetsperminute.pop_front();
                        }

                        return std::move(m);
                    });
                }) |
                on_error_resume_next([](std::exception_ptr ep){
                    cerr << rxu::what(ep) << endl;
                    return observable<>::empty<Reducer>();
                });
            return tweetsperminute;
        }) |
        merge() |
        start_with(noop) |
        as_dynamic();

    auto tail = t$ |
        filter([](const shared_ptr<const json>& tw){
            auto& tweet = *tw;
            return !!tweet.count("timestamp_ms");
        }) |
        buffer_with_time(milliseconds(1000), poolthread) |
        rxo::map([=](const vector<shared_ptr<const json>>& tws){
            return Reducer([=](Model& m){
                m.tail.insert(m.tail.end(), tws.begin(), tws.end());
                while(m.tail.size() > 100) {
                    m.tail.pop_front();
                }
                return std::move(m);
            });
        }) |
        on_error_resume_next([](std::exception_ptr ep){
            cerr << rxu::what(ep) << endl;
            return observable<>::empty<Reducer>();
        }) |
        start_with(noop) |
        as_dynamic();

    auto total = t$ |
        filter([](const shared_ptr<const json>& tw){
            auto& tweet = *tw;
            return !!tweet.count("timestamp_ms");
        }) |
        window_with_time(milliseconds(1000), poolthread) |
        rxo::map([](observable<shared_ptr<const json>> source){
            auto tweetsperminute = source | count() | rxo::map([](int count){
                return Reducer([=](Model& m){
                    m.total += count;
                    return std::move(m);
                });
            });
            return tweetsperminute;
        }) |
        merge() |
        on_error_resume_next([](std::exception_ptr ep){
            cerr << rxu::what(ep) << endl;
            return observable<>::empty<Reducer>();
        }) |
        start_with(noop) |
        as_dynamic();

    // combine things that modify the model
    auto reducer$ = from(windowtpm, groupbuckets, grouptpm, total, tail) |
        merge(tweetthread);

    auto model$ = reducer$ |
        // apply things that modify the model
        scan(Model{}, [](Model& m, Reducer& f){
            try {
                return f(m);
            } catch (const std::exception& e) {
                cerr << e.what() << endl;
                return std::move(m);
            }
        }) | 
        start_with(Model{}) |
        publish() |
        connect_forever() |
        // only pass model updates every 200ms
        debounce(tweetthread, milliseconds(200)) |
        // deep copy model before sending to another thread
        rxo::map([](Model m){
            // deep copy to avoid ux seeing mutations
            for (auto& tg: m.groupedtweets) {
                tg.second = make_shared<TweetGroup>(*tg.second);
            }
            return m;
        }) |
        // give the updated model to the UX
        observe_on(mainthread);
    
    // render models
    frame$ |
        with_latest_from([=](int, const Model& m){
            auto renderthreadid = this_thread::get_id();
            if (mainthreadid != renderthreadid) {
                cerr << "render on wrong thread!" << endl;
            }

            static int idx = 0;
            static vector<WordCount> words;
            words.clear();
            auto collectwords = [&](){
                if (idx >= 0 && idx < int(m.groups.size())) {
                    auto& window = m.groups.at(idx);
                    auto& group = m.groupedtweets.at(window);

                    words.clear();
                    transform(group->words.begin(), group->words.end(), back_inserter(words), [&](const pair<string, int>& word){
                        return WordCount{word.first, word.second, {}};
                    });
                    sort(words.begin(), words.end(), [](const WordCount& l, const WordCount& r){
                        return l.count > r.count;
                    });
                }
            };

            ImGui::SetNextWindowSize(ImVec2(200,100), ImGuiSetCond_FirstUseEver);
            if (ImGui::Begin("Live Analysis")) {
                RXCPP_UNWIND_AUTO([](){
                    ImGui::End();
                });

                ImGui::TextWrapped("url: %s", URL.c_str());
                ImGui::Text("Total Tweets: %d, Now: %s", m.total, utctextfrom().c_str());

                // by window
                if (ImGui::CollapsingHeader("Tweets Per Minute (windowed)"))
                {
                    static vector<float> tpm;
                    tpm.clear();
                    transform(m.tweetsperminute.begin(), m.tweetsperminute.end(), back_inserter(tpm), [](int count){return static_cast<float>(count);});
                    ImVec2 plotextent(ImGui::GetContentRegionAvailWidth(),100);
                    if (!m.tweetsperminute.empty()) {
                        ImGui::Text("%s -> %s", 
                            utctextfrom(duration_cast<seconds>(m.tweetsstart)).c_str(),
                            utctextfrom(duration_cast<seconds>(m.tweetsstart + length + (every * m.tweetsperminute.size()))).c_str());
                    }
                    ImGui::PlotLines("", &tpm[0], tpm.size(), 0, nullptr, 0.0f, fltmax, plotextent);
                }

                // by group
                if (ImGui::CollapsingHeader("Tweets Per Minute (grouped)"))
                {
                    static vector<float> tpm;
                    tpm.clear();

                    {
                        static vector<pair<milliseconds, float>> groups;
                        groups.clear();
                        transform(m.groupedtweets.begin(), m.groupedtweets.end(), back_inserter(groups), [&](const pair<TimeRange, shared_ptr<TweetGroup>>& group){
                            return make_pair(group.first.begin, static_cast<float>(group.second->tweets.size()));
                        });
                        sort(groups.begin(), groups.end(), [](const pair<milliseconds, float>& l, const pair<milliseconds, float>& r){
                            return l.first < r.first;
                        });
                        transform(groups.begin(), groups.end(), back_inserter(tpm), [&](const pair<milliseconds, float>& group){
                            return group.second;
                        });
                    }

                    if (!m.groupedtweets.empty()) {
                        ImGui::Text("%s -> %s", 
                            utctextfrom(duration_cast<seconds>(m.groups.front().begin)).c_str(),
                            utctextfrom(duration_cast<seconds>(m.groups.back().end)).c_str());
                    }
                    ImVec2 plotposition = ImGui::GetCursorScreenPos();
                    ImVec2 plotextent(ImGui::GetContentRegionAvailWidth(),100);
                    ImGui::PlotLines("", &tpm[0], tpm.size(), 0, nullptr, 0.0f, fltmax, plotextent);
                    assert(tpm.size() == m.groups.size());
                    if (ImGui::IsItemHovered()) {
                        const float t = Clamp((ImGui::GetMousePos().x - plotposition.x) / plotextent.x, 0.0f, 0.9999f);
                        idx = (int)(t * (m.groups.size() - 1));
                    }
                    idx = min(idx, int(m.groups.size() - 1));

                    collectwords();

                    if (idx >= 0 && idx < int(m.groups.size())) {
                        auto& window = m.groups.at(idx);
                        auto& group = m.groupedtweets.at(window);

                        ImGui::Text("Start : %lld, %s", window.begin.count(), utctextfrom(duration_cast<seconds>(window.begin)).c_str());
                        ImGui::Text("End   : %lld, %s", window.end.count(), utctextfrom(duration_cast<seconds>(window.end)).c_str());
                        ImGui::Text("Tweets: %ld", group->tweets.size());
                        ImGui::Text("Words : %ld", group->words.size());

                        ImGui::Separator();

                        ImGui::Text("Top 10 words:");

                        static vector<WordCount> top10;
                        top10.clear();
                        copy(words.begin(), words.begin() + min(int(words.size()), 10), back_inserter(top10));

                        float maxCount = 0.0f;
                        for(auto& w : m.groups) {
                            auto& g = m.groupedtweets.at(w);
                            auto end = g->words.end();
                            for(auto& word : top10) {
                                auto wrd = g->words.find(word.word);
                                float count = 0.0f;
                                if (wrd != end) {
                                    count = static_cast<float>(wrd->second);
                                }
                                maxCount = count > maxCount ? count : maxCount;
                                word.all.push_back(count);
                            }
                        }

                        for (auto& w : top10) {
                            ImGui::Text("%d - %s", w.count, w.word.c_str());
                            ImVec2 plotextent(ImGui::GetContentRegionAvailWidth(),100);
                            ImGui::PlotLines("", &w.all[0], w.all.size(), 0, nullptr, 0.0f, maxCount, plotextent);
                        }
                    }
                }
            }

            ImGui::SetNextWindowSize(ImVec2(100,200), ImGuiSetCond_FirstUseEver);
            if (ImGui::Begin("Word Cloud")) {
                RXCPP_UNWIND_AUTO([](){
                    ImGui::End();
                });

                if (words.empty()) {
                    collectwords();
                }

                ImVec2 origin = ImGui::GetCursorScreenPos();
                auto area = ImGui::GetContentRegionAvail();

                auto font = cloudFont;

                static vector<ImRect> taken;
                taken.clear();

                if (!words.empty()) {
                    mt19937 source;
                    const auto maxCount = words.front().count;
                    auto cursor = words.begin();
                    auto end = words.begin() + min(int(words.size()), 50);
                    for(;cursor != end; ++cursor) {
                        auto place = Clamp(static_cast<float>(cursor->count)/maxCount, 0.0f, 0.9999f);
                        auto size = Clamp(cloudFont->FontSize*place, cloudFont->FontSize/4.0f, cloudFont->FontSize);
                        auto color = ImGui::GetStyle().Colors[ImGuiCol_Text];
                        auto extent = font->CalcTextSizeA(size, fltmax, 0.0f, &cursor->word[0], &cursor->word[0] + cursor->word.size(), nullptr);
                        std::uniform_int_distribution<> offsetx(0, area.x - extent.x);
                        std::uniform_int_distribution<> offsety(0, area.y - extent.y);

                        ImRect bound;
                        int checked = -1;
                        int trys = 10;
                        for(;checked < int(taken.size()) && trys > 0;--trys){
                            checked = 0;
                            auto position = ImVec2(origin.x + offsetx(source), origin.y + offsety(source));
                            bound = ImRect(position.x, position.y, position.x + extent.x, position.y + extent.y);
                            for(auto& t : taken) {
                                if (t.Overlaps(bound)) break;
                                ++checked;
                            }
                        }

                        ImGui::GetWindowDrawList()->AddText(font, size, bound.Min, ImColor(color), &cursor->word[0], &cursor->word[0] + cursor->word.size());
                        taken.push_back(bound);
                    }
                }
            }

            ImGui::SetNextWindowSize(ImVec2(100,200), ImGuiSetCond_FirstUseEver);
            if (ImGui::Begin("Recent Tweets")) {
                RXCPP_UNWIND_AUTO([](){
                    ImGui::End();
                });

                ImGui::TextWrapped("url: %s", URL.c_str());
                ImGui::Text("Total Tweets: %d", m.total);

                auto cursor = m.tail.rbegin();
                auto end = m.tail.rbegin() + min(int(m.tail.size()), 10);
                for(;cursor != end; ++cursor) {
                    auto& tweet = (**cursor);
                    if (tweet["user"]["name"].is_string() && tweet["user"]["screen_name"].is_string()) {
                        ImGui::Separator();
                        ImGui::Text("%s (@%s)", tweet["user"]["name"].get<string>().c_str() , tweet["user"]["screen_name"].get<string>().c_str() );
                        ImGui::TextWrapped("%s", tweettext(tweet).c_str());
                    }
                }
            }

            return m;
        }, model$) |
        on_error_resume_next([](std::exception_ptr ep){
            cerr << rxu::what(ep) << endl;
            return observable<>::empty<Model>();
        }) |
        repeat(numeric_limits<int>::max()) |
        subscribe<Model>([](const Model&){});

    // send tweets
    if (playback) {
        cout << argv[1 + argoffset] << endl;

        auto filethread = observe_on_new_thread();
        observable<>::create<string>([=](subscriber<string> out){
                std::ifstream infile(argv[1 + argoffset]);
                std::string line;
                while (std::getline(infile, line))
                {
                    if (line[0] == '{') {
                        line+="\r\n";
                        assert(isEndOfTweet(line));
                        out.on_next(line);
                    }
                }
                out.on_completed();
            })
            .subscribe_on(filethread)
            .subscribe(lifetime, chunkbus.get_subscriber());
    } else {
        string method = isFilter ? "POST" : "GET";

        twitterrequest(factory, URL, method, CONS_KEY, CONS_SEC, ATOK_KEY, ATOK_SEC)
            // https://dev.twitter.com/streaming/overview/connecting
            .timeout(seconds(90), tweetthread)
            .on_error_resume_next([=](std::exception_ptr ep) -> observable<string> {
                try {rethrow_exception(ep);}
                catch (const http_exception& ex) {
                    cerr << ex.what() << endl;
                    if (ex.code() == CURLE_COULDNT_RESOLVE_HOST ||
                        ex.code() == CURLE_COULDNT_CONNECT ||
                        ex.code() == CURLE_OPERATION_TIMEDOUT ||
                        ex.code() == CURLE_BAD_CONTENT_ENCODING ||
                        ex.code() == CURLE_REMOTE_FILE_NOT_FOUND) {
                        cerr << "error - waiting to retry.." << endl;
                        return observable<>::timer(seconds(5), tweetthread).map([](long){return string{};}).ignore_elements();
                    } else if (ex.code() == CURLE_GOT_NOTHING ||
                        ex.code() == CURLE_PARTIAL_FILE ||
                        ex.code() == CURLE_SEND_ERROR ||
                        ex.code() == CURLE_RECV_ERROR) {
                        cerr << "reconnecting after TCP error" << endl;
                        return observable<>::empty<string>();
                    } else if (ex.code() == CURLE_HTTP_RETURNED_ERROR || ex.httpStatus() > 200) {
                        if (ex.httpStatus() == 420) {
                            cerr << "rate limited - waiting to retry.." << endl;
                            return observable<>::timer(minutes(1), tweetthread).map([](long){return string{};}).ignore_elements();
                        } else if (ex.httpStatus() == 404 ||
                            ex.httpStatus() == 406 ||
                            ex.httpStatus() == 413 ||
                            ex.httpStatus() == 416) {
                            cerr << "invalid request - exit" << endl;
                            return observable<>::error<string>(ep, tweetthread);
                        }
                        return observable<>::timer(seconds(5), tweetthread).map([](long){return string{};}).ignore_elements();
                    }
                }
                catch (const timeout_error& ex) {
                    cerr << "reconnecting after timeout" << endl;
                    return observable<>::empty<string>();
                }
                catch (const exception& ex) {
                    cerr << ex.what() << endl;
                    terminate();
                }
                return observable<>::error<string>(ep, tweetthread);
            })
            .repeat()
            .subscribe(lifetime, chunkbus.get_subscriber());
    }

    // main loop
    while(lifetime.is_subscribed()) {
        SDL_Event event;
        while (SDL_PollEvent(&event))
        {
            ImGui_ImplSdlGL3_ProcessEvent(&event);
            if (event.type == SDL_QUIT) {
                lifetime.unsubscribe();
                break;
            }
        }

        if (!lifetime.is_subscribed()) {
            break;
        }

        ImGui_ImplSdlGL3_NewFrame(window);

        while (!rl.empty() && rl.peek().when < rl.now()) {
            rl.dispatch();
        }

        sendframe();

        while (!rl.empty() && rl.peek().when < rl.now()) {
            rl.dispatch();
        }

        // 1. Show a simple window
        {
            ImGui::Begin("Twitter App");
            ImGui::Text("Application average %.3f ms/frame (%.1f FPS)", 1000.0f / ImGui::GetIO().Framerate, ImGui::GetIO().Framerate);
            ImGui::End();
        }

        // Rendering
        glViewport(0, 0, (int)ImGui::GetIO().DisplaySize.x, (int)ImGui::GetIO().DisplaySize.y);
        glClearColor(clear_color.x, clear_color.y, clear_color.z, clear_color.w);
        glClear(GL_COLOR_BUFFER_BIT);
        ImGui::Render();
        SDL_GL_SwapWindow(window);
    }

    return 0;
}