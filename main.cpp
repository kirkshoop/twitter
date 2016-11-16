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
#include "imgui_impl_sdl_gl3.h"
#include <GL/glew.h>
#include <SDL.h>


#if 1

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

set<string> ignoredWords{
// added
"rt", "like", "just", "tomorrow", "new", "year", "month", "day", "today", "make", "let", "want", "did", "going", "good", "really", "know", "people", "got", "life", "need", "say", "doing", "great", "right", "time", "best", "happy", "stop", "think", "world", "watch", "gonna", "remember", "way",
"better", "team", "check", "feel", "talk", "hurry", "look", "live", "home", "game", "run",
// http://xpo6.com/list-of-english-stop-words/
"a", "about", "above", "above", "across", "after", "afterwards", "again", "against", "all", "almost", "alone", "along", "already", "also","although","always","am","among", "amongst", "amoungst", "amount",  "an", "and", "another", "any","anyhow","anyone","anything","anyway", "anywhere", "are", "around", "as",  "at", "back","be","became", "because","become","becomes", "becoming", "been", "before", "beforehand", "behind", "being", "below", "beside", "besides", "between", "beyond", "bill", "both", "bottom","but", "by", "call", "can", "cannot", "cant", "co", "con", "could", "couldnt", "cry", "de", "describe", "detail", "do", "done", "down", "due", "during", "each", "eg", "eight", "either", "eleven","else", "elsewhere", "empty", "enough", "etc", "even", "ever", "every", "everyone", "everything", "everywhere", "except", "few", "fifteen", "fify", "fill", "find", "fire", "first", "five", "for", "former", "formerly", "forty", "found", "four", "from", "front", "full", "further", "get", "give", "go", "had", "has", "hasnt", "have", "he", "hence", "her", "here", "hereafter", "hereby", "herein", "hereupon", "hers", "herself", "him", "himself", "his", "how", "however", "hundred", "ie", "if", "in", "inc", "indeed", "interest", "into", "is", "it", "its", "itself", "keep", "last", "latter", "latterly", "least", "less", "ltd", "made", "many", "may", "me", "meanwhile", "might", "mill", "mine", "more", "moreover", "most", "mostly", "move", "much", "must", "my", "myself", "name", "namely", "neither", "never", "nevertheless", "next", "nine", "no", "nobody", "none", "noone", "nor", "not", "nothing", "now", "nowhere", "of", "off", "often", "on", "once", "one", "only", "onto", "or", "other", "others", "otherwise", "our", "ours", "ourselves", "out", "over", "own","part", "per", "perhaps", "please", "put", "rather", "re", "same", "see", "seem", "seemed", "seeming", "seems", "serious", "several", "she", "should", "show", "side", "since", "sincere", "six", "sixty", "so", "some", "somehow", "someone", "something", "sometime", "sometimes", "somewhere", "still", "such", "system", "take", "ten", "than", "that", "the", "their", "them", "themselves", "then", "thence", "there", "thereafter", "thereby", "therefore", "therein", "thereupon", "these", "they", "thickv", "thin", "third", "this", "those", "though", "three", "through", "throughout", "thru", "thus", "to", "together", "too", "top", "toward", "towards", "twelve", "twenty", "two", "un", "under", "until", "up", "upon", "us", "very", "via", "was", "we", "well", "were", "what", "whatever", "when", "whence", "whenever", "where", "whereafter", "whereas", "whereby", "wherein", "whereupon", "wherever", "whether", "which", "while", "whither", "who", "whoever", "whole", "whom", "whose", "why", "will", "with", "within", "without", "would", "yet", "you", "your", "yours", "yourself", "yourselves", "the"};

#endif

inline float  Clamp(float v, float mn, float mx)                       { return (v < mn) ? mn : (v > mx) ? mx : v; }
inline ImVec2 Clamp(const ImVec2& f, const ImVec2& mn, ImVec2 mx)      { return ImVec2(Clamp(f.x,mn.x,mx.x), Clamp(f.y,mn.y,mx.y)); }
inline string tolower(string s) {
    transform(s.begin(), s.end(), s.begin(), [=](char c){return tolower(c);});
    return s;
}

auto twitterrequest = [](::rxcurl::rxcurl& factory, string URL, string method, string CONS_KEY, string CONS_SEC, string ATOK_KEY, string ATOK_SEC){

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
        .merge();
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
    SDL_Window *window = SDL_CreateWindow("Twitter Analisys (ImGui SDL2+OpenGL3)", SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED, 1280, 720, SDL_WINDOW_OPENGL|SDL_WINDOW_RESIZABLE);
    SDL_GLContext glcontext = SDL_GL_CreateContext(window);
    glewInit();

    // Setup ImGui binding
    ImGui_ImplSdlGL3_Init(window);

    // Load Fonts
    // (there is a default font, this is only if you want to change it. see extra_fonts/README.txt for more details)
    //ImGuiIO& io = ImGui::GetIO();    
    //io.Fonts->AddFontDefault();
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

    auto grouptpm = t$ |
        group_by([](shared_ptr<const json> tw) {
            auto& tweet = *tw;
            if (!tweet.count("timestamp_ms")) {
                return minutes(~0);
            } else {
                auto t = milliseconds(stoll(tweet["timestamp_ms"].get<string>()));
                auto m = duration_cast<minutes>(t);
                return m;
            }
        }) |
        rxo::map([=](grouped_observable<minutes, shared_ptr<const json>> g){
            auto group = g | 
                take_until(observable<>::timer(minutes(2)), identity_current_thread()) | 
                scan(noop, [=](Reducer&, const shared_ptr<const json>& tw){
                    auto& tweet = *tw;
                    if (!tweet.count("timestamp_ms")) {
                        return noop;
                    }

                    // exclude entities, urls and some punct from this words list
                    auto words = split(tweettext(tweet), R"(((&\w;)|((http|ftp|https)://[\w-]+(\.[\w-]+)+([\w.,@?^=%&:/~+#-]*[\w@?^=%&/~+#-])?)|[\(\)\[\]\s,.-:;&?!'"]+))", Split::RemoveDelimiter); //"
                    for (auto& word: words) {
                        word = tolower(word);
                    }
                    words.erase(std::remove_if(words.begin(), words.end(), [=](const string& w){
                        return !(w.size() > 1 && ignoredWords.find(w) == ignoredWords.end() && URL.find(w) == string::npos);
                    }), words.end());

                    return Reducer([=](Model& m){
                        auto t = milliseconds(stoll(tweet["timestamp_ms"].get<string>()));
                        auto rangebegin = duration_cast<milliseconds>(g.get_key() - minutes(2));
                        auto rangeend = rangebegin+minutes(1);
                        auto searchend = rangeend+minutes(2);
                        auto offset = milliseconds(0);
                        for (;rangebegin+offset < searchend;offset += milliseconds(5000)){
                            if (rangebegin+offset <= t && t < rangeend+offset) {
                                auto key = TimeRange{rangebegin+offset, rangeend+offset};
                                auto it = m.groupedtweets.find(key);
                                if (it == m.groupedtweets.end()) {
                                    // add group
                                    m.groups.push_back(key);
                                    sort(m.groups.begin(), m.groups.end());
                                    it = m.groupedtweets.insert(make_pair(key, make_shared<TweetGroup>())).first;
                                }

                                it->second->tweets.push_back(tw);

                                for (auto& word: words) {
                                    ++it->second->words[word];
                                }
                            }
                        }

                        while(!m.groups.empty() && m.groups.front().begin + minutes(4) < m.groups.back().end) {
                            // remove group
                            m.groupedtweets.erase(m.groups.front());
                            m.groups.pop_front();
                        }

                        return std::move(m);
                    });
                }) |
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
        window_with_time(milliseconds(60000), milliseconds(5000), poolthread) |
        rxo::map([](observable<shared_ptr<const json>> source){
            auto tweetsperminute = source | count() | rxo::map([](int count){
                return Reducer([=](Model& m){
                    m.tweetsperminute.push_back(count);
                    while(m.tweetsperminute.size() > (2 * 60 * 2)) {
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
        rxo::map([=](const shared_ptr<const json>& tw){
            auto& tweet = *tw;
            if (!tweet.count("timestamp_ms")) {
                return noop;
            }
            return Reducer([=](Model& m){
                m.tail.push_back(tw);
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
        rxo::map([=](const shared_ptr<const json>& tw){
            auto& tweet = *tw;
            if (!tweet.count("timestamp_ms")) {
                return noop;
            }
            return Reducer([=](Model& m){
                ++m.total;
                return std::move(m);
            });
        }) |
        on_error_resume_next([](std::exception_ptr ep){
            cerr << rxu::what(ep) << endl;
            return observable<>::empty<Reducer>();
        }) |
        start_with(noop) |
        as_dynamic();

    // combine things that modify the model
    auto reducer$ = from(windowtpm, grouptpm, total, tail) |
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
        // only pass model updates every 100ms
        debounce(tweetthread, milliseconds(100)) |
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

            ImGui::SetNextWindowSize(ImVec2(200,100), ImGuiSetCond_FirstUseEver);
            if (ImGui::Begin("Live Analysis")) {
                RXCPP_UNWIND_AUTO([](){
                    ImGui::End();
                });

                ImGui::TextWrapped("url: %s", URL.c_str());
                ImGui::Text("Total Tweets: %d", m.total);

                // by window
                if (ImGui::CollapsingHeader("Tweets Per Minute (windowed)"))
                {
                    static vector<float> tpm;
                    tpm.clear();
                    transform(m.tweetsperminute.begin(), m.tweetsperminute.end(), back_inserter(tpm), [](int count){return static_cast<float>(count);});
                    ImVec2 plotextent(ImGui::GetContentRegionAvailWidth(),100);
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

                    ImVec2 plotposition = ImGui::GetCursorScreenPos();
                    ImVec2 plotextent(ImGui::GetContentRegionAvailWidth(),100);
                    ImGui::PlotLines("", &tpm[0], tpm.size(), 0, nullptr, 0.0f, fltmax, plotextent);
                    static int idx = 0;
                    if (ImGui::IsItemHovered()) {
                        const float t = Clamp((ImGui::GetMousePos().x - plotposition.x) / plotextent.x, 0.0f, 0.9999f);
                        idx = (int)(t * (tpm.size() - 1));
                    }
                    if (idx >= 0 && idx < int(tpm.size())) {
                        auto& window = m.groups.at(idx);
                        auto& group = m.groupedtweets.at(window);

                        time_t tb = duration_cast<seconds>(window.begin).count();
                        struct tm* tmb = gmtime(&tb);
                        std::stringstream buffer;
                        buffer << std::put_time(tmb, "%a %b %d %H:%M:%S %Y");

                        ImGui::Text("Start : %lld, %s", window.begin.count(), buffer.str().c_str());
                        ImGui::Text("Tweets: %ld", group->tweets.size());
                        ImGui::Text("Words : %ld", group->words.size());

                        ImGui::Separator();

                        ImGui::Text("Top 10 words:");

                        static vector<WordCount> words;
                        words.clear();
                        transform(group->words.begin(), group->words.end(), back_inserter(words), [&](const pair<string, int>& word){
                            return WordCount{word.first, word.second, {}};
                        });
                        sort(words.begin(), words.end(), [](const WordCount& l, const WordCount& r){
                            return l.count > r.count;
                        });
                        words.resize(10);

                        for(auto& w : m.groups) {
                            auto& g = m.groupedtweets.at(w);
                            auto end = g->words.end();
                            for(auto& word : words) {
                                auto wrd = g->words.find(word.word);
                                float count = 0.0f;
                                if (wrd != end) {
                                    count = static_cast<float>(wrd->second);
                                }
                                word.all.push_back(count);
                            }
                        }

                        for (auto& w : words) {
                            ImGui::Text("%d - %s", w.count, w.word.c_str());
                            ImVec2 plotextent(ImGui::GetContentRegionAvailWidth(),100);
                            ImGui::PlotLines("", &w.all[0], w.all.size(), 0, nullptr, 0.0f, fltmax, plotextent);
                        }
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
                        ImGui::Text("%s (%s)", tweet["user"]["name"].get<string>().c_str() , tweet["user"]["screen_name"].get<string>().c_str() );
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
            .on_error_resume_next([](std::exception_ptr ep){
                cerr << rxu::what(ep) << endl;
                return observable<>::empty<string>();
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