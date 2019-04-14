/*
 * Getting timelines by Twitter Streaming API
 */
#include <iostream>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>

#include <sstream>
#include <fstream>
#include <regex>
#include <deque>
#include <map>
#include <unordered_map>
#include <random>

using namespace std;
using namespace std::chrono;

#include <imgui.h>
#include <misc/freetype/imgui_freetype.h>

#include <imgui_internal.h>

#include <imgui_impl_opengl2.h>
#include <imgui_impl_sdl.h>
#include <imgui_impl_opengl2.cpp>
#include <imgui_impl_sdl.cpp>

#include <SDL2/SDL_mixer.h>

#include <oauth.h>
#include <curl/curl.h>

#include <range/v3/all.hpp>

#include <rxcpp/rx.hpp>
using namespace rxcpp;
using namespace rxcpp::rxo;
using namespace rxcpp::rxs;

#include <json.hpp>
using json=nlohmann::json;

#include "rxcurl.h"
using namespace rxcurl;

#include "rximgui.h"
using namespace rximgui;

#include "util.h"
using namespace ::util;

#include "model.h"
using namespace model;

#include "tweets.h"
using namespace tweets;


const ImVec4 clear_color = ImColor(114, 144, 154);

const auto length = milliseconds(60000);
// Time granularity for periodicity of tweet groups
const auto every = milliseconds(5000);
// For how long the tweet groups should be retained
auto keep = minutes(10);

// Create tweet groups for the period from 'timestamp'-'windows' till 'timestamp' and
// delete the ones that are too old to keep
template<class F>
inline void updategroups(Model& model, milliseconds timestamp, milliseconds window, F&& f) {

    auto& md = model.data;

    auto& m = *md;

    // We are interested in tweets no older than 'window' milliseconds from 'timestamp' ...
    auto searchbegin = duration_cast<minutes>(duration_cast<minutes>(timestamp) - window);
    // ... and not newer than 'timestamp'
    auto searchend = timestamp;
    for (auto offset = milliseconds(0);
         searchbegin + offset < searchend;
         offset += duration_cast<milliseconds>(every)) {
        // Calculate current time period
        auto key = TimeRange{searchbegin+offset, searchbegin+offset+length};
        auto it = m.groupedtweets.find(key);
        // If the current time period does not exist in the groups yet
        if (it == m.groupedtweets.end()) {
            // Append current group's period to deque of groups in the model
            m.groups.push_back(key);
            // Ensure the group deque is sorted
            m.groups |= ranges::action::sort(less<TimeRange>{});
            // Create a tweet group for the current period and insert it into the map
            it = m.groupedtweets.insert(make_pair(key, make_shared<TweetGroup>())).first;
        }

        // Regardless of whether the group already existed or not, apply f function
        // to tweet group.
        // TODO: Try to remove the condition as it is an artefact from previous revisions
        if (searchbegin+offset <= timestamp && timestamp < searchbegin+offset+length) {
            f(*it->second);
        }
    }

    // Drop tweet groups that are older than the period for which tweet groups should be retained
    while(!m.groups.empty() && m.groups.front().begin + keep < m.groups.back().begin) {
        // remove group
        m.groupedtweets.erase(m.groups.front());
        m.groups.pop_front();
    }
}

string settingsFile;
json settings;

int main(int argc, const char *argv[])
{
    // ==== Parse args

    auto command = string{};
    for(auto cursor=argv,end=argv+argc; cursor!=end; ++cursor) {
        command += string{*cursor};
    }

    cerr << "command = " << command.c_str() << endl;

    auto exefile = string{argv[0]};
    
    cerr << "exe = " << exefile.c_str() << endl;

    auto exedir = exefile.substr(0, exefile.find_last_of('/'));

    cerr << "dir = " << exedir.c_str() << endl;

    auto exeparent = exedir.substr(0, exedir.find_last_of('/'));

    cerr << "parent = " << exeparent.c_str() << endl;

    auto selector = string{tolower(argc > 1 ? argv[1] : "")};

    const bool playback = argc == 3 && selector == "playback";
    const bool gui = argc == 6;

    bool dumpjson = argc == 7 && selector == "dumpjson";
    bool dumptext = argc == 7 && selector == "dumptext";

    string inifile = exeparent + "/Resources/imgui.ini";

    cerr << "dir = " << exedir.c_str() << endl;
    
    bool setting = false;
    settingsFile = exedir + "/settings.json";

    if (argc == 2 && ifstream(argv[1]).good()) {
        setting = true;
        settingsFile = argv[1];
    } else if (!playback && argc == 3 && selector == "setting") {
        setting = true;
        settingsFile = argv[2];
    } else if (argc == 1 || argc == 2) {
        setting = true;
    } 

    if (setting) {

        cerr << "settings = " << settingsFile.c_str() << endl;

        ifstream i(settingsFile);
        if (i.good()) {
            i >> settings;
        }
    }

    if (!playback &&
        !dumptext &&
        !dumpjson &&
        !gui &&
        !setting) {
        printf("twitter <settings file path>\n");
        printf("twitter SETTING <settings file path>\n");
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

    if (settings.count("Keep") == 0) {
        settings["Keep"] = keep.count();
    } else {
        keep = minutes(settings["Keep"].get<int>());
    }

    if (settings.count("Query") == 0) {
        settings["Query"] = json::parse(R"({"Action": "sample"})");
    }

    if (settings.count("WordFilter") == 0) {
        settings["WordFilter"] = "-http,-expletive";
    }

    if (settings.count("TweetFilter") == 0) {
        settings["TweetFilter"] = "";
    }

    if (settings.count("Language") == 0) {
        settings["Language"] = "en";
    }

    // twitter api creds
    if (settings.count("ConsumerKey") == 0) {
        settings["ConsumerKey"] = string{};
    }
    if (settings.count("ConsumerSecret") == 0) {
        settings["ConsumerSecret"] = string{};
    }
    if (settings.count("AccessTokenKey") == 0) {
        settings["AccessTokenKey"] = string{};
    }
    if (settings.count("AccessTokenSecret") == 0) {
        settings["AccessTokenSecret"] = string{};
    }

    // azure ml api creds
    if (settings.count("SentimentUrl") == 0) {
        settings["SentimentUrl"] = string{};
    }
    if (settings.count("SentimentKey") == 0) {
        settings["SentimentKey"] = string{};
    }

    if (settings.count("SentimentRequests") == 0) {
        settings["SentimentRequests"] = "Off";
    }

    // google api creds
    if (settings.count("PerspectiveUrl") == 0) {
        settings["PerspectiveUrl"] = string{};
    }
    if (settings.count("PerspectiveKey") == 0) {
        settings["PerspectiveKey"] = string{};
    }

    if (settings.count("PerspectiveRequests") == 0) {
        settings["PerspectiveRequests"] = "Off";
    }

    //cerr << setw(2) << settings << endl;

    // ==== Constants - paths
    const string URL = "https://stream.twitter.com/1.1/statuses/";
    string url = URL;
    string filepath;
    if (!playback) {
        if (!setting) {
            // read from args

            url += argv[5 + argoffset];

            // ==== Twitter keys
            const char *CONS_KEY = argv[1 + argoffset];
            const char *CONS_SEC = argv[2 + argoffset];
            const char *ATOK_KEY = argv[3 + argoffset];
            const char *ATOK_SEC = argv[4 + argoffset];

            settings["ConsumerKey"] = string(CONS_KEY);
            settings["ConsumerSecret"] = string(CONS_SEC);
            settings["AccessTokenKey"] = string(ATOK_KEY);
            settings["AccessTokenSecret"] = string(ATOK_SEC);
        } 
    } else {
        filepath = argv[1 + argoffset];
        cerr << "file = " << filepath.c_str() << endl;
    }

    // Setup SDL
    if (SDL_Init(SDL_INIT_VIDEO|SDL_INIT_AUDIO|SDL_INIT_TIMER) != 0)
    {
        printf("SDL_Init Error: %s\n", SDL_GetError());
        return -1;
    }

    // Setup audio
    if (Mix_OpenAudio( 22050, MIX_DEFAULT_FORMAT, 2, 4096 ) == -1) 
    {
        printf("Mix_OpenAudio - Error: %s\n", SDL_GetError());
        return -1;
    }

    Mix_Chunk *dot = Mix_LoadWAV((exeparent + "/Resources/dot.wav").c_str());
    if (dot == NULL)
    {
        printf("Mix_LoadWAV dot - Error: %s\n", SDL_GetError());
        return -1;
    }

    // Setup window
    SDL_GL_SetAttribute(SDL_GL_DOUBLEBUFFER, 1);
    SDL_GL_SetAttribute(SDL_GL_DEPTH_SIZE, 24);
    SDL_GL_SetAttribute(SDL_GL_STENCIL_SIZE, 8);
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_MAJOR_VERSION, 2);
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_MINOR_VERSION, 2);
    SDL_DisplayMode current;
    SDL_GetCurrentDisplayMode(0, &current);
    SDL_Window* window = SDL_CreateWindow("Twitter Analysis (Dear ImGui SDL2+OpenGL)", SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED, 1280, 720, SDL_WINDOW_OPENGL|SDL_WINDOW_RESIZABLE);
    SDL_GLContext gl_context = SDL_GL_CreateContext(window);
    SDL_GL_SetSwapInterval(1); // Enable vsync

    // Setup Dear ImGui context
    IMGUI_CHECKVERSION();
    ImGui::CreateContext();
    ImGuiIO& io = ImGui::GetIO(); (void)io;
    //io.ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard;  // Enable Keyboard Controls

    // Setup Platform/Renderer bindings
    ImGui_ImplSDL2_InitForOpenGL(window, gl_context);
    ImGui_ImplOpenGL2_Init();


    io.IniFilename = inifile.c_str();

    ImGui::StyleColorsDark();

    // Setup Fonts

    int fontsadded = 0;

    static const ImWchar noto[] = { 
        0x0020, 0x0513,
        0x1e00, 0x1f4d,
        0x2000, 0x25ca,
        0xfb01, 0xfb04,
        0xfeff, 0xfffd, 
        0 };

    static const ImWchar symbols[] = { 
        0x20a0, 0x2e3b, 
        0x3008, 0x3009, 
        0x4dc0, 0x4dff, 
        0xa700, 0xa71f, 
        0 };

    ImVector<ImWchar> ranges;
    ImFontAtlas::GlyphRangesBuilder builder;
    builder.AddRanges(io.Fonts->GetGlyphRangesDefault());
    builder.AddRanges(io.Fonts->GetGlyphRangesChineseSimplifiedCommon());
    builder.AddRanges(io.Fonts->GetGlyphRangesCyrillic());
    builder.AddRanges(io.Fonts->GetGlyphRangesJapanese());
    builder.AddRanges(io.Fonts->GetGlyphRangesKorean());
    builder.AddRanges(io.Fonts->GetGlyphRangesThai());
    builder.AddRanges(symbols);
    builder.BuildRanges(&ranges);

    if (!fontsadded && ifstream("/Library/Fonts/Arial Unicode.ttf").good()) {
        ++fontsadded;
        io.Fonts->AddFontFromFileTTF("/Library/Fonts/Arial Unicode.ttf", 16.0f, nullptr, ranges.Data);
    }

    if (!fontsadded && ifstream(exedir + "/NotoMono-Regular.ttf").good()) {
        ++fontsadded;
        io.Fonts->AddFontFromFileTTF((exedir + "/NotoMono-Regular.ttf").c_str(), 13.0f, nullptr, noto);
    } else if (!fontsadded && ifstream(exeparent + "/Resources/NotoMono-Regular.ttf").good()) {
        ++fontsadded;
        io.Fonts->AddFontFromFileTTF((exeparent + "/Resources/NotoMono-Regular.ttf").c_str(), 13.0f, nullptr, noto);
    }

    static ImFontConfig config;
    config.MergeMode = true;
    if (!fontsadded && ifstream(exedir + "/NotoSansSymbols-Regular.ttf").good()) {
        ++fontsadded;
        io.Fonts->AddFontFromFileTTF((exedir + "/NotoSansSymbols-Regular.ttf").c_str(), 13.0f, &config, symbols);
    } else if (!fontsadded && ifstream(exeparent + "/Resources/NotoSansSymbols-Regular.ttf").good()) {
        ++fontsadded;
        io.Fonts->AddFontFromFileTTF((exeparent + "/Resources/NotoSansSymbols-Regular.ttf").c_str(), 13.0f, &config, symbols);
    }

    if (fontsadded) {
        ImGuiFreeType::BuildFontAtlas(io.Fonts, ImGuiFreeType::RasterizerFlags::LightHinting);
        // io.Fonts->Build();
    }

    /* Define deinitialization procedure for graphic user interface in a SafeGuard pattern.
       The closure supplied as the parameter to unwinder macro is run in a destructor of a
       local var when it goes out of scope.

       More details on Unwinder (SageGuard pattern implementation):
       http://kirkshoop.github.io/2011/09/27/unwinder-should-be-in-standard-library.html
     */
    RXCPP_UNWIND_AUTO([&](){
        ImGui_ImplOpenGL2_Shutdown();
        ImGui_ImplSDL2_Shutdown();
        ImGui::DestroyContext();
        SDL_GL_DeleteContext(gl_context);
        SDL_DestroyWindow(window);
        Mix_FreeChunk(dot);
	    Mix_CloseAudio();
        SDL_Quit();
    });

    /* In Rx concurrency model is defined by scheduling. To specify, which thread on_next() calls are
       made on, observe_on_xxx functions are used in RxCpp. These function return 'coordination' factories.
       
       General explanation of schedulers in Rx:
         http://reactivex.io/documentation/scheduler.html
       Detailed explanation of ObserveOn (C#):
         http://www.introtorx.com/Content/v1.0.10621.0/15_SchedulingAndThreading.html#SubscribeOnObserveOn
       Details on coordination and how scheduling is implemented in RxCpp:
         https://github.com/Reactive-Extensions/RxCpp/blob/master/DeveloperManual.md
       More on scheduling design in RxCpp:
         https://github.com/Reactive-Extensions/RxCpp/issues/105#issuecomment-87294867
         https://github.com/Reactive-Extensions/RxCpp/issues/9
    */
    auto mainthreadid = this_thread::get_id();
    /* Create coordination for observing on UI thread
       Variable rl is defined in rximgui.h
    
       More info on observe_on_run_loop:
         https://github.com/Reactive-Extensions/RxCpp/issues/151
         https://github.com/Reactive-Extensions/RxCpp/pull/154
    */
    auto mainthread = observe_on_run_loop(rl);

    /* Thread to download tweets from Twitter and perform initial parsing
       Example of observe_on_new_thread with explanation:
         http://rxcpp.codeplex.com/discussions/620779
     */
    auto tweetthread = observe_on_new_thread();
    /* "The event_loop scheduler is a simple round-robin fixed-size thread pool."
          http://rxcpp.codeplex.com/discussions/635113
     */
    auto poolthread = observe_on_event_loop();

    // Create a factory that returns observable of http responses from Twitter
    auto factory = create_rxcurl();

    composite_subscription lifetime;

    /* make changes to settings observable

       This allows setting changes to be composed into the expressions to reconfigure them.
    */
    rxsub::replay<json, decltype(mainthread)> setting_update(1, mainthread, lifetime);
    auto setting_updates = setting_update.get_observable();
    auto sendsettings = setting_update.get_subscriber();
    auto sf = settingsFile;
    // call update_settings() to save changes and trigger parties interested in the change
    auto update_settings = [sendsettings, sf](json s){
        ofstream o(sf);
        o << setw(4) << s;
        if (sendsettings.is_subscribed()){
            sendsettings.on_next(s);
        }
    };

    // initial update
    update_settings(settings);

    /* filter settings updates to changes in the sentiment api settings.

       debounce is used to wait until the updates pause before signaling a change.

       distinct_until_changed is used to filter out settings updates that do not change the value
    */
    auto useSentimentApi = setting_updates |
    rxo::map([=](const json& settings){
        string use = tolower(settings["SentimentRequests"].get<std::string>());
        bool haveurl = settings.count("SentimentUrl") > 0 && !settings["SentimentUrl"].get<std::string>().empty();
        bool havekey = settings.count("SentimentKey") > 0 && !settings["SentimentKey"].get<std::string>().empty();
        return use == "on" && haveurl && havekey;
    }) |
    debounce(milliseconds(500), mainthread) |
    distinct_until_changed() |
    replay(1) |
    ref_count() |
    as_dynamic();

    /* filter settings updates to changes in the perspective api settings.

       debounce is used to wait until the updates pause before signaling a change.

       distinct_until_changed is used to filter out settings updates that do not change the value
    */
    auto usePerspectiveApi = setting_updates |
    rxo::map([=](const json& settings){
        string use = tolower(settings["PerspectiveRequests"].get<std::string>());
        bool haveurl = settings.count("PerspectiveUrl") > 0 && !settings["PerspectiveUrl"].get<std::string>().empty();
        bool havekey = settings.count("PerspectiveKey") > 0 && !settings["PerspectiveKey"].get<std::string>().empty();
        return use == "on" && haveurl && havekey;
    }) |
    debounce(milliseconds(500), mainthread) |
    distinct_until_changed() |
    replay(1) |
    ref_count() |
    as_dynamic();


    /* filter settings updates to changes that change the url for the twitter stream.

       distinct_until_changed is used to filter out settings updates that do not change the url

       debounce is used to wait until the updates pause before signaling the url has changed. 
         this is important since typing in keywords would cause many intermediate changes to the url
         and twitter rate limits the user if there are too many fast restarts.
    */    
    auto urlchanges = setting_updates |
        rxo::map([=](const json& settings){
            string url = URL + settings["Query"]["Action"].get<std::string>() + ".json?";
            if (settings.count("Language") > 0) {
                url += "language=" + settings["Language"].get<std::string>() + "&";
            }
            if (settings["Query"].count("Keywords") > 0 && settings["Query"]["Keywords"].is_array()) {
                url += "track=";
                for (auto& kw : settings["Query"]["Keywords"]) {
                    url += kw.get<std::string>() + ",";
                }
            }
            return url;
        }) |
        debounce(milliseconds(1000), mainthread) |
        distinct_until_changed() |
        tap([](string url){
            cerr << "url = " << url.c_str() << endl;
        }) |
        replay(1) |
        ref_count() |
        as_dynamic();

    // ==== Tweets

    observable<string> chunks;

    // request tweets
    if (playback) {
        chunks = filechunks(tweetthread, filepath);
    } else {
        // switch to new connection whenever the url changes
        chunks = urlchanges |
            rxo::map([&](const string& url){
                // ==== Constants - flags
                const bool isFilter = url.find("/statuses/filter") != string::npos;
                string method = isFilter ? "POST" : "GET";

                /*  Create new http request for a stream of tweets

                    If an error is encountered, function on_error_resume_next ensures the error
                    item is replaced with some correct observable value. 
                    
                    In this case return never, which never completes and never emits a value.
                    The never will be cancelled when the url changes
                */
                return twitterrequest(tweetthread, factory, url, method, settings["ConsumerKey"], settings["ConsumerSecret"], settings["AccessTokenKey"], settings["AccessTokenSecret"]) |
                    // handle invalid requests by waiting for a trigger to try again
                    on_error_resume_next([](std::exception_ptr ep){
                        cerr << rxu::what(ep) << endl;
                        return rxs::never<string>();
                    });
            }) |
            switch_on_next();
    }

    // parse tweets
    auto tweets = chunks | 
        parsetweets(poolthread, tweetthread) |
        rxo::map([](parsedtweets p){
            p.errors |
                tap([](parseerror e){
                    cerr << rxu::what(e.ep) << endl;
                }) | 
                subscribe<parseerror>();
            return p.tweets;
        }) | 
        merge(tweetthread);

    /* Take tweets, make sure that any error is ignored, and publish them
       as a stream of observables available to multiple on-demand subscribers.

       Publish creates a connectable observable that can be subscribed to later and that
       starts operating when a consumer requests it
       ref_count provides interface to connectable observable to consumers that
       take ordinary observable. It also keeps the track, how many consumers are
       connected to the observable, hence the name.
       Retry repeats the input given number of times when it fails. When no count is 
       given it will repeat infinitely when it fails.

       OnErrorResumeNext:
         https://github.com/ReactiveX/RxJava/wiki/Error-Handling-Operators
       Publish:
         http://reactivex.io/documentation/operators/publish.html
       RefCount:
         http://reactivex.io/documentation/operators/refcount.html
       Detailed explanation on Publish and RefCount (C#):
         http://www.introtorx.com/content/v1.0.10621.0/14_HotAndColdObservables.html
     */
    auto ts = tweets |
        retry() |
        publish() |
        ref_count() |
        as_dynamic();

    // ==== Model

    /* Reducer is a function that takes Model and returns new Model. The operation of taking a sequence of something
       and aggregating that sequence into single instance of something, is traditionally called reduce.
       In STL traditionally term "accumulate" is used; other languages use term "fold" for similar operation.
       Each reducer here represents a mini-pipeline that listens for incomming data of interest and calculates
       result from them and/or produce side effect.

       Definition of reduce in Rx:
         http://reactivex.io/documentation/operators/reduce.html 
    */
    vector<observable<Reducer>> reducers;

    // Create new ofstream with current time epoch as filename. Twits will be dumped there, if the JSON dump mode is activated
    auto newJsonFile = [exedir]() -> unique_ptr<ofstream> {
        return unique_ptr<ofstream>{new ofstream(exedir + "/" + to_string(time_point_cast<milliseconds>(system_clock::now()).time_since_epoch().count()) + ".json")};
    };
    // The json dump file is created always
    auto jsonfile = newJsonFile();

    /* Generate a stream of booleans when the json dumping setting is changed.
       Function interval emits incremented long value with time granularity
       specified by 'every' with 'tweetthread' scheduler.
       Function map converts each long value into bool value equal to
       current state of dumpjson variable.
       Function distinct_until_changed replaces groups of equal items in the
       stream that go together with single item, thus filtering repeated values
       
       Interval:
         http://reactivex.io/documentation/operators/interval.html
         http://www.introtorx.com/content/v1.0.10621.0/04_CreatingObservableSequences.html#ObservableInterval
       Map:
         http://reactivex.io/documentation/operators/map.html
       DistinctUntilChanged:
         http://www.introtorx.com/content/v1.0.10621.0/05_Filtering.html#Distinct
     */
    auto dumpjsonchanged = interval(every, tweetthread) |
        rxo::map([&](long){return dumpjson;}) |
        start_with(dumpjson) |
        distinct_until_changed() |
        replay(1) |
        ref_count();

    /* Process tweets grouped by packs arrived during 'every' time granularity,
       delaying emission by 'length' time
       "publish and connect_forever ensure that the work is done even if nothing subscribes the result'
         (http://kirkshoop.github.io/2013/09/05/deferoperation.html)
       Detailed explanation of buffering with time:
         http://www.introtorx.com/content/v1.0.10621.0/17_SequencesOfCoincidence.html
       Delay:
         http://reactivex.io/documentation/operators/delay.html
    */
    auto delayed_tweets = ts |
        buffer_with_time(every, tweetthread) |
        delay(length, tweetthread) |
        publish(lifetime) |
        connect_forever();

    /* Whenever jsonchanged becomes true, generate an observable that consists of
       of a sequence of packs of tweets. This sequence should continue only until jsonchanged becomes false,
       Then such sequence is fed to a reducer that takes tweet data and prints it
       to stdout.

       filter emits only those elements of input sequence for which the supplied
       predicate is true.
       take_until passes the elements of input sequence forward until the
       it's argument emits an element of it's own or is terminated.
       switch_on_next combines sequences into a single sequence, ignoring all the results
       that come from sequences that emitted after the first emission of the latest sequence
       start_with specifies the first element for the sequence to start with

       Filter:
         http://reactivex.io/documentation/operators/filter.html
       TakeUntil:
         http://reactivex.io/documentation/operators/takeuntil.html
       Switch (switch_on_next):
         http://reactivex.io/documentation/operators/switch.html
         http://www.introtorx.com/Content/v1.0.10621.0/12_CombiningSequences.html#Switch
       StartWith:
         http://reactivex.io/documentation/operators/startwith.html
     */
    reducers.push_back(
        dumpjsonchanged |
        filter([](bool dj){ return dj; }) |
        rxo::map([&](bool) -> observable<Reducer> {
            return delayed_tweets | 
                take_until(
                    dumpjsonchanged | 
                    filter([](bool dj){ return !dj; }) | 
                    delay(length, tweetthread)) |
                rxo::map([&](const vector<Tweet>& tws){
                    return Reducer([&, tws](Model& m){
                        for (auto& tw: tws) {
                            auto& tweet = tw.data->tweet;
                            auto json = tweet.dump();
                            cout << json << "\r\n";
                            *jsonfile << json << "\r\n";
                        }
                        *jsonfile << flush;
                        return std::move(m);
                    });
                });
        }) |
        switch_on_next(tweetthread) |
        nooponerror() |
        start_with(noop));

    /* Store current url in the model
        Take changes to the url and save the current url in the model.
    */
    reducers.push_back(
        urlchanges | 
        rxo::map([=](string url){
            return Reducer([=](Model& m){
                m.data->url = url;
                return std::move(m);
            });
        }) |
        nooponerror() |
        start_with(noop));

    /* Dump text to cout
       Take tweets, use filter to pass them forward in the chain only if dumptext is true.
       Tap (do) function lets the programmer listen to sequence and possibly make side-effects on it.
       Here we listen to each tweet and just print them to stdout
       
       Do (aka Tap): http://www.introtorx.com/content/v1.0.10621.0/09_SideEffects.html#Do
     */
    reducers.push_back(
        ts |
        onlytweets() |
        filter([&](const Tweet&){
            return dumptext;
        }) |
        tap([=](const Tweet& tw){
            auto& tweet = tw.data->tweet;
            if (tweet["user"]["name"].is_string() && tweet["user"]["screen_name"].is_string()) {
                cout << "------------------------------------" << endl;
                cout << tweet["user"]["name"].get<string>() << " (" << tweet["user"]["screen_name"].get<string>() << ")" << endl;
                cout << tweettext(tweet) << endl;
            }
        }) |
        noopandignore() |
        start_with(noop));

    if (!playback) {
        // update groups on time interval so that minutes with no tweets are recorded.
        reducers.push_back(
            observable<>::interval(milliseconds(500), poolthread) |
            rxo::map([=](long){
                return Reducer([=](Model& m){
                    auto rangebegin = time_point_cast<milliseconds>(system_clock::now()).time_since_epoch();
                    updategroups(m, rangebegin, keep, [](TweetGroup&){});
                    return std::move(m);
                });
            }) |
            nooponerror() |
            start_with(noop));
    }

    //
    // send tweet text to sentiment api service.
    //

    auto sentimentupdates = ts |
        onlytweets() |
        buffer_with_time(milliseconds(500), tweetthread) |
        filter([](const vector<Tweet>& tws){ return !tws.empty(); }) |
        rxo::map([=](const vector<Tweet>& tws) -> observable<Reducer> {
            vector<string> text = tws | 
                ranges::view::transform([](Tweet tw){
                    auto& tweet = tw.data->tweet;
                    return tweettext(tweet);
                });
            return sentimentrequest(poolthread, factory, settings["SentimentUrl"].get<string>(), settings["SentimentKey"].get<string>(), text) |
                rxo::map([=](const string& body){
                    auto response = json::parse(body);
                    return Reducer([=](Model& m){
                        auto combined = ranges::view::zip(response["Results"]["output1"], tws);
                        ranges::for_each(combined, [&m](const auto& b) {
                            auto sentiment = get<0>(b);
                            auto tweet = get<1>(b).data->tweet;
                            auto ts = timestamp_ms(get<1>(b));
                            bool isNeg = sentiment["Sentiment"] == "negative";
                            bool isPos = sentiment["Sentiment"] == "positive";

                            m.data->sentiment[tweet["id_str"]] = sentiment["Sentiment"];

                            for (auto& word: get<1>(b).data->words) {
                                isNeg && ++m.data->negativewords[word];
                                isPos && ++m.data->positivewords[word];
                            }

                            updategroups(m, ts, length, [&](TweetGroup& tg){
                                isNeg && ++tg.negative;
                                isPos && ++tg.positive;
                            });
                        });
                        return std::move(m);
                    });
                });
        }) |
        merge() |
        nooponerror("sentiment") |
        start_with(noop) | 
        as_dynamic();

    reducers.push_back(
        useSentimentApi |
            rxo::map([=](bool use){
                return use ? sentimentupdates : just(noop) | as_dynamic();
            }) |
            switch_on_next()
    );

    //
    // send tweet text to perspective api service.
    //
    
    auto perspectiveupdates = ts |
        onlytweets() |
        // perspective api has 10 QPS limit (eval @ 100s)
        window_with_time(seconds(1), poolthread) |
        rxo::map([](observable<Tweet> tws){
            // take only first 10 from each second
            return tws.take(10);
        }) |
        merge() |
        rxo::map([=](const Tweet& tw) -> observable<Reducer> {
            auto text = tweettext(tw.data->tweet);
            auto ts = timestamp_ms(tw);
            return perspectiverequest(poolthread, factory, settings["PerspectiveUrl"].get<string>(), settings["PerspectiveKey"].get<string>(), text) |
                rxo::map([=](const string& body){
                    auto response = json::parse(body);

                    return Reducer([=](Model& m){
                        auto& scores = response["attributeScores"];

                        Perspective result{
                            scores["TOXICITY"]["summaryScore"]["value"].get<float>(), 
                            scores["SPAM"]["summaryScore"]["value"].get<float>(), 
                            scores["INFLAMMATORY"]["summaryScore"]["value"].get<float>()
                        };

                        m.data->perspective[tw.data->tweet["id_str"]] = result;

                        auto toxic = result.toxicity > 0.7f;

                        for (auto& word: tw.data->words) {
                            toxic && ++m.data->toxicwords[word];
                        }

                        updategroups(m, ts, length, [&](TweetGroup& tg){
                            toxic && ++tg.toxic;
                        });

                        return std::move(m);
                    });
                });
        }) |
        merge() |
        nooponerror("perspective") |
        start_with(noop) | 
        as_dynamic();

    reducers.push_back(
        usePerspectiveApi |
            rxo::map([=](bool use){
                return use ? perspectiveupdates : just(noop) | as_dynamic();
            }) |
            switch_on_next()
    );

    // group tweets, that arrive, by the timestamp_ms value
    reducers.push_back(
        ts |
        onlytweets() |
        observe_on(poolthread) |
        rxo::map([=](const Tweet& tw){
            auto& tweet = tw.data->tweet;

            auto text = tweettext(tweet);

            auto words = splitwords(text);

            return Reducer([=](Model& m){
                auto t = timestamp_ms(tw);

                for (auto& word: words) {
                    ++m.data->allwords[word];
                }

                updategroups(m, t, length, [&](TweetGroup& tg){
                    tg.tweets.push_back(tw);

                    for (auto& word: words) {
                        ++tg.words[word];
                    }
                });

                return std::move(m);
            });
        }) |
        nooponerror() |
        start_with(noop));

    // track users
    reducers.push_back(
        ts |
        onlytweets() |
        observe_on(poolthread) |
        rxo::map([=](const Tweet& tw){
            auto& tweet = tw.data->tweet;

            return Reducer([=](Model& m){
                auto t = timestamp_ms(tw);

                auto update = [&](UserMap& userMap, const nlohmann::json& scope) {
                    auto u = userMap.end();
                    if (scope.count("user") > 0) {
                        auto user = scope["user"];
                        auto id = user["id_str"];
                        u = userMap.find(id.get<string>());
                        if (u == userMap.end()) {
                            u = userMap.insert(make_pair(id.get<string>(), User{user, t})).first;
                        }
                    }
                    return u;
                };

                auto& all = m.data->allusers;

                User user;
                auto u = update(all, tweet);
                if (all.end() != u) {
                    u->second.tweet(t);
                    user = u->second;
                }

                User rtuser;
                if (tweet.count("retweeted_status") > 0) {
                    auto u = update(all, tweet["retweeted_status"]);
                    if (all.end() != u) {
                        u->second.retweet(t);
                        rtuser = u->second;
                    }
                }

                User qtuser;
                if (tweet.count("quoted_status") > 0) {
                    auto u = update(all, tweet["quoted_status"]);
                    if (all.end() != u) {
                        u->second.quote(t);
                        qtuser = u->second;
                    }
                }

                updategroups(m, t, length, [&](TweetGroup& tg){
                    {
                        auto u = update(tg.users, user.data->user);
                        if (tg.users.end() != u) {
                            u->second.tweet(t);
                        }
                    }

                    if (rtuser.data) {
                        auto u = update(tg.users, rtuser.data->user);
                        if (tg.users.end() != u) {
                            u->second.retweet(t);
                        }
                    }
                    if (qtuser.data) {
                        auto u = update(tg.users, qtuser.data->user);
                        if (tg.users.end() != u) {
                            u->second.quote(t);
                        }
                    }
                });

                // prune a user that is not active
                auto first = t - (keep + length);
                auto exp = find_if(all.begin(), all.end(), [=](const std::pair<string, User>& user){
                    auto t = user.second.data->active.end;
                    return t < first;
                });
                if (exp != all.end()) {
                    all.erase(exp);
                }

                return std::move(m);
            });
        }) |
        nooponerror() |
        start_with(noop));

    // window tweets by the time that they arrive
    auto tsw = ts |
        onlytweets() |
        window_with_time(length, every, poolthread) |
        publish() |
        ref_count() |
        as_dynamic();

    reducers.push_back(
        tsw |
        rxo::map([=](observable<Tweet> source){
            auto rangebegin = time_point_cast<seconds>(system_clock::now()).time_since_epoch();
            auto tweetsperminute = source | 
                start_with(Tweet{}) |
                rxo::map([=](const Tweet& tw) {
                    return Reducer([=](Model& md){
                        auto& m = *(md.data);

                        auto maxsize = duration_cast<seconds>(keep).count()/duration_cast<seconds>(every).count();

                        if (m.tweetsperminute.size() == 0) {
                            m.tweetsstart = duration_cast<seconds>(rangebegin + length);
                        }
                        
                        if (static_cast<long long>(m.tweetsperminute.size()) < maxsize) {
                            // fill in missing history
                            while (maxsize > static_cast<long long>(m.tweetsperminute.size())) {
                                m.tweetsperminute.push_front(0);
                                m.tweetsstart -= duration_cast<seconds>(every);
                            }
                        }

                        if (rangebegin >= m.tweetsstart) {

                            const auto i = duration_cast<seconds>(rangebegin - m.tweetsstart).count()/duration_cast<seconds>(every).count();

                            // add future buckets
                            while(i >= static_cast<long long>(m.tweetsperminute.size())) {
                                m.tweetsperminute.push_back(0);
                            }

                            if (tw.data->words.size() > 0) {
                                ++m.tweetsperminute[i];
                            }
                        }

                        // discard expired data
                        while(static_cast<long long>(m.tweetsperminute.size()) > maxsize) {
                            m.tweetsstart += duration_cast<seconds>(every);
                            m.tweetsperminute.pop_front();
                        }

                        return std::move(md);
                    });
                });
            return tweetsperminute;
        }) |
        merge() |
        nooponerror() |
        start_with(noop));

    // window tweets by the timestamp_ms embedded in each tweet
    auto window_timestamp = ts |
        onlytweets() |
        // for each tweet emit duplicates of that tweet for each time window
        rxo::map([](Tweet tw){
            auto e_s = duration_cast<seconds>(every).count();
            auto l_s = duration_cast<seconds>(length).count();
            auto t_s = floor<seconds>(timestamp_ms(tw)).count();
            auto te_s = t_s - (t_s % e_s);
            auto tstart_s = te_s - (l_s - e_s);
            auto tfinish_s = te_s + e_s;
            int begin = 0;
            int end = (tfinish_s - tstart_s) / e_s;
            return iterate(
                ranges::view::ints(begin, end) | 
                ranges::view::transform([=](int s){
                    return make_tuple(tstart_s + (e_s * s), tw);
                }));
        }) |
        merge() |
        // group tweets from the same window
        group_by(rxu::apply_to([](int64_t s, Tweet){
            return s;
        }), rxu::apply_to([](int64_t, Tweet tw){
            return tw;
        })) |
        rxo::map([=](grouped_observable<int64_t, Tweet> source){
            return source | 
                // stop each window after a full window interval occurs without a new tweet
                timeout(every) | 
                on_error_resume_next([](std::exception_ptr){
                    return empty<Tweet>();
                });
        }) |
        publish() |
        ref_count() |
        as_dynamic();

    // play sounds for up and down inflections in the count of tweets per minute
    reducers.push_back(window_timestamp |
        rxo::map([=](observable<Tweet> source){
            return source | count();
        }) |
        merge() |
        rxo::window(6, 1) |
        rxo::map([=](observable<int> counts){
            return counts | average() |
                zip(counts | last(), counts | first()) |
                filter(rxu::apply_to([](double avg, long last, long first){
                    // only keep if the average of three counts is less than 
                    // the last count and greater than the first count.
                    // should only keep when there is a inflection up or down.
                    return avg < last && avg > first;
                })) |
                rxo::map(rxu::apply_to([=](double , long , long ){
                    return Reducer([=](Model& md){
                        Mix_PlayChannel(-1, dot, 0);
                        return std::move(md);
                    });
                }));
        }) |
        merge() |
        nooponerror() |
        start_with(noop));

    // keep recent tweets
    reducers.push_back(
        ts |
        onlytweets() |
        // batch the tweets to be more efficient about model updates
        // the trade off is adding latency to the model updates
        buffer_with_time(milliseconds(50), poolthread) |
        filter([](const vector<Tweet>& tws){ return !tws.empty(); }) |
        rxo::map([=](const vector<Tweet>& tws){
            return Reducer([=](Model& md){
                auto& m = *(md.data);
                m.tweets.insert(m.tweets.end(), tws.begin(), tws.end());
                auto last = m.tweets.empty() ? milliseconds(0) : timestamp_ms(m.tweets.back());
                auto first = last - (keep + length);
                auto end = find_if(m.tweets.begin(), m.tweets.end(), [=](const Tweet& tw){
                    auto t = timestamp_ms(tw);
                    return t > first;
                });
                auto cursor=m.tweets.begin();
                for (;cursor!=end; ++cursor) {
                    auto sentiment = m.sentiment[cursor->data->tweet["id_str"].get<string>()];
                    m.sentiment.erase(cursor->data->tweet["id_str"].get<string>());
                    auto perspective = m.perspective[cursor->data->tweet["id_str"].get<string>()];
                    for (auto& word: cursor->data->words) {
                        if (--m.allwords[word] == 0)
                        {
                            m.allwords.erase(word);
                        }
                        if (sentiment == "negative" && --m.negativewords[word] == 0)
                        {
                            m.negativewords.erase(word);
                        }
                        if (sentiment == "positive" && --m.positivewords[word] == 0)
                        {
                            m.positivewords.erase(word);
                        }
                        if (perspective.toxicity > 0.7f && --m.toxicwords[word] == 0)
                        {
                            m.toxicwords.erase(word);
                        }
                    }
                }
                m.tweets.erase(m.tweets.begin(), end);
                return std::move(md);
            });
        }) |
        nooponerror() |
        start_with(noop));

    // record total number of tweets that have arrived
    reducers.push_back(
        ts |
        onlytweets() |
        // batch the tweets to be more efficient about model updates
        // the trade off is adding latency to the model updates
        window_with_time(milliseconds(50), poolthread) |
        rxo::map([](observable<Tweet> source){
            auto tweetsperminute = source | count() | rxo::map([](int count){
                return Reducer([=](Model& md){
                    auto& m = *(md.data);
                    m.total += count;
                    return std::move(md);
                });
            });
            return tweetsperminute;
        }) |
        merge() |
        nooponerror() |
        start_with(noop));

    // combine things that modify the model
    auto actions = iterate(reducers) |
        // give the reducers to the UX
        merge(mainthread);

    //
    // apply reducers to the model (Flux architecture)
    //

    auto models = actions |
        // apply things that modify the model
        scan(Model{}, [=](Model& m, Reducer& f){
            try {
                auto r = f(m);
                r.data->timestamp = mainthread.now();
                return r;
            } catch (const std::exception& e) {
                cerr << e.what() << endl;
                return std::move(m);
            }
        }) | 
        // only view model updates every 50ms
        sample_with_time(milliseconds(50), mainthread) |
        publish() |
        ref_count() |
        as_dynamic();

    // ==== View

    auto viewModels = models |
        // if the processing of the model takes too long, skip until caught up
        filter([=](const Model& m){
            return m.data->timestamp <= mainthread.now();
        }) |
        start_with(Model{}) |
        rxo::map([](Model& m){
            return ViewModel{m};
        }) |
        as_dynamic();

    auto draw = frames |
        with_latest_from(rxu::take_at<1>(), viewModels) |
        tap([=](const ViewModel&){
            auto renderthreadid = this_thread::get_id();
            if (mainthreadid != renderthreadid) {
                cerr << "render on wrong thread!" << endl;
                terminate();
            }
        }) |
        replay(1) |
        ref_count() |
        as_dynamic();

    vector<observable<ViewModel>> renderers;

    // render analysis
    renderers.push_back(
        draw |
        tap([=](const ViewModel& vm){
            auto& m = *vm.m.data;

            static ImGuiTextFilter wordfilter(settings["WordFilter"].get<string>().c_str());

            static ImVec4 neutralcolor = ImColor(250, 150, 0);
            static ImVec4 positivecolor = ImColor(50, 230, 50);
            static ImVec4 negativecolor = ImColor(240, 33, 33);
            
            ImGui::SetNextWindowSize(ImVec2(200,100), ImGuiSetCond_FirstUseEver);
            if (ImGui::Begin("Live Analysis")) {
                RXCPP_UNWIND(End, [](){
                    ImGui::End();
                });

                ImGui::TextWrapped("url: %s", m.url.c_str());

                {
                    ImGui::Columns(2);
                    RXCPP_UNWIND_AUTO([](){
                        ImGui::Columns(1);
                    });

                    ImGui::Text("Now: %s", utctextfrom().c_str()); ImGui::NextColumn();
                    ImGui::Text("Total Tweets: %d", m.total);
                }

                if (ImGui::CollapsingHeader("Settings", ImGuiTreeNodeFlags_Framed))
                {
                    bool changed = false;

                    ImGui::Text("%s", settingsFile.c_str());

                    if (ImGui::CollapsingHeader("Keys"))
                    {
                        static bool showkeys = false;
                        int textflags = ImGuiInputTextFlags_CharsNoBlank;
                        if (!showkeys) {
                            if (ImGui::Button("Show Keys")) {
                                showkeys = true;
                            } else {
                                textflags |= ImGuiInputTextFlags_Password;
                            }
                        } else {
                            if (ImGui::Button("Hide Keys")) {
                                showkeys = false;
                            }
                        }

                        static string ckey(settings.count("ConsumerKey") > 0 ? settings["ConsumerKey"].get<string>() : string{});
                        ckey.reserve(128);
                        if (ImGui::InputText("Consumer Key", &ckey[0], ckey.capacity(), textflags)) {
                            ckey.resize(strlen(&ckey[0]));
                            settings["ConsumerKey"] = ckey;
                            changed = true;
                        }

                        static string csecret(settings.count("ConsumerSecret") > 0 ? settings["ConsumerSecret"].get<string>() : string{});
                        csecret.reserve(128);
                        if (ImGui::InputText("Consumer Secret", &csecret[0], csecret.capacity(), textflags)) {
                            csecret.resize(strlen(&csecret[0]));
                            settings["ConsumerSecret"] = csecret;
                            changed = true;
                        }

                        static string atkey(settings.count("AccessTokenKey") > 0 ? settings["AccessTokenKey"].get<string>() : string{});
                        atkey.reserve(128);
                        if (ImGui::InputText("Access Token Key", &atkey[0], atkey.capacity(), textflags)){
                            atkey.resize(strlen(&atkey[0]));
                            settings["AccessTokenKey"] = atkey;
                            changed = true;
                        }

                        static string atsecret(settings.count("AccessTokenSecret") > 0 ? settings["AccessTokenSecret"].get<string>() : string{});
                        atsecret.reserve(128);
                        if (ImGui::InputText("Access Token Secret", &atsecret[0], atsecret.capacity(), textflags)) {
                            atsecret.resize(strlen(&atsecret[0]));
                            settings["AccessTokenSecret"] = atsecret;
                            changed = true;
                        }

                        static string sentimenturl(settings.count("SentimentUrl") > 0 ? settings["SentimentUrl"].get<string>() : string{});
                        sentimenturl.reserve(1024);
                        if (ImGui::InputText("Sentiment Url", &sentimenturl[0], sentimenturl.capacity())) {
                            sentimenturl.resize(strlen(&sentimenturl[0]));
                            settings["SentimentUrl"] = sentimenturl;
                            changed = true;
                        }

                        static string sentimentkey(settings.count("SentimentKey") > 0 ? settings["SentimentKey"].get<string>() : string{});
                        sentimentkey.reserve(1024);
                        if (ImGui::InputText("Sentiment Key", &sentimentkey[0], sentimentkey.capacity(), textflags)) {
                            sentimentkey.resize(strlen(&sentimentkey[0]));
                            settings["SentimentKey"] = sentimentkey;
                            changed = true;
                        }

                        static string perspectiveurl(settings.count("PerspectiveUrl") > 0 ? settings["PerspectiveUrl"].get<string>() : string{});
                        perspectiveurl.reserve(1024);
                        if (ImGui::InputText("Perspective Url", &perspectiveurl[0], perspectiveurl.capacity())) {
                            perspectiveurl.resize(strlen(&perspectiveurl[0]));
                            settings["PerspectiveUrl"] = perspectiveurl;
                            changed = true;
                        }

                        static string perspectivekey(settings.count("PerspectiveKey") > 0 ? settings["PerspectiveKey"].get<string>() : string{});
                        perspectivekey.reserve(1024);
                        if (ImGui::InputText("Perspective Key", &perspectivekey[0], perspectivekey.capacity(), textflags)) {
                            perspectivekey.resize(strlen(&perspectivekey[0]));
                            settings["PerspectiveKey"] = perspectivekey;
                            changed = true;
                        }
                    }

                    static int minutestokeep = keep.count();
                    ImGui::InputInt("minutes to keep", &minutestokeep);
                    changed |= keep.count() != minutestokeep;
                    keep = minutes(minutestokeep);
                    settings["Keep"] = keep.count();

                    ImGui::Separator();

                    if (ImGui::ListBoxHeader("Query", 2)) {
                        RXCPP_UNWIND(ListBoxFooter, [](){
                            ImGui::ListBoxFooter();
                        });
                        if (ImGui::Selectable("Sample", settings["Query"]["Action"].get<std::string>() == "sample")) {
                            settings["Query"]["Action"] = "sample";
                            settings["Query"].erase("Keywords");
                            changed = true;
                        }
                        if (ImGui::Selectable("Filter", settings["Query"]["Action"].get<std::string>() == "filter")) {
                            settings["Query"]["Action"] = "filter";
                            settings["Query"]["Keywords"] = json::array();
                            changed = true;
                        }
                    }

                    if (tolower(settings["Query"]["Action"].get<std::string>()) == "filter") {
                        ImGui::Indent();
                        RXCPP_UNWIND(Unindent, [](){
                            ImGui::Unindent();
                        });
                        auto keyword_list = ranges::accumulate(settings["Query"]["Keywords"].get<std::vector<string>>(), string(), [](string acc, string kw){ return acc + kw + ", ";});
                        keyword_list.reserve(128);
                        if (ImGui::InputText("Keywords", &keyword_list[0], keyword_list.capacity())) {
                            keyword_list.resize(strlen(&keyword_list[0]));
                            settings["Query"]["Keywords"] = ranges::action::split(keyword_list, ranges::view::c_str(","));
                            changed = true;
                        }
                    }

                    static string language(settings.count("Language") > 0 ? settings["Language"].get<string>() : string{});
                    language.reserve(64);
                    if (ImGui::InputText("Language", &language[0], language.capacity())) {
                        language.resize(strlen(&language[0]));
                        settings["Language"] = language;
                        changed = true;
                    }


                    static bool sentimentRequests(settings.count("SentimentRequests") > 0 && tolower(settings["SentimentRequests"].get<string>()) == "on");
                    if (ImGui::Checkbox("Call Sentiment Api", &sentimentRequests)) {
                        settings["SentimentRequests"] = sentimentRequests ? "On" : "Off";
                        changed = true;
                    }

                    static bool perspectiveRequests(settings.count("PerspectiveRequests") > 0 && tolower(settings["PerspectiveRequests"].get<string>()) == "on");
                    if (ImGui::Checkbox("Call Perspective Api", &perspectiveRequests)) {
                        settings["PerspectiveRequests"] = perspectiveRequests ? "On" : "Off";
                        changed = true;
                    }

                    if (changed) {
                        update_settings(settings);
                    }
                }

                // by window
                if (ImGui::CollapsingHeader("Tweets Per Minute (windowed by arrival time)", ImGuiTreeNodeFlags_Framed | ImGuiTreeNodeFlags_DefaultOpen))
                {
                    static vector<float> tpm;
                    tpm.clear();
                    tpm = m.tweetsperminute |
                        ranges::view::transform([](int count){return static_cast<float>(count);});
                    ImVec2 plotextent(ImGui::GetContentRegionAvailWidth(),100);
                    if (!m.tweetsperminute.empty()) {
                        ImGui::Text("%s -> %s", 
                            utctextfrom(duration_cast<seconds>(m.tweetsstart)).c_str(),
                            utctextfrom(duration_cast<seconds>(m.tweetsstart + length + (every * m.tweetsperminute.size()))).c_str());
                    }
                    ImGui::PlotLines("", &tpm[0], tpm.size(), 0, nullptr, 0.0f, fltmax, plotextent);
                }

                // by group

                if (ImGui::CollapsingHeader("Toxic Tweets Per Minute", ImGuiTreeNodeFlags_Framed))
                {
                    auto& tpm = vm.data->toxictpm;
                    ImVec2 plotextent(ImGui::GetContentRegionAvailWidth(),100);
                    ImGui::PushStyleColor(ImGuiCol_PlotLines, negativecolor);
                    // perspective api limits to 10 QPS so the max for the graph cannot exceed 10 * 60 TPM
                    ImGui::PlotLines("", &tpm[0], tpm.size(), 0, nullptr, 0.0f, std::min(vm.data->maxtpm, 10.0f * 60), plotextent);
                    ImGui::PopStyleColor(1);
                }

                if (ImGui::CollapsingHeader("Negative Tweets Per Minute", ImGuiTreeNodeFlags_Framed))
                {
                    auto& tpm = vm.data->negativetpm;
                    ImVec2 plotextent(ImGui::GetContentRegionAvailWidth(),100);
                    ImGui::PushStyleColor(ImGuiCol_PlotLines, negativecolor);
                    ImGui::PlotLines("", &tpm[0], tpm.size(), 0, nullptr, 0.0f, vm.data->maxtpm, plotextent);
                    ImGui::PopStyleColor(1);
                }

                if (ImGui::CollapsingHeader("Positive Tweets Per Minute", ImGuiTreeNodeFlags_Framed))
                {
                    auto& tpm = vm.data->positivetpm;
                    ImVec2 plotextent(ImGui::GetContentRegionAvailWidth(),100);
                    ImGui::PushStyleColor(ImGuiCol_PlotLines, positivecolor);
                    ImGui::PlotLines("", &tpm[0], tpm.size(), 0, nullptr, 0.0f, vm.data->maxtpm, plotextent);
                    ImGui::PopStyleColor(1);
                }

                if (ImGui::CollapsingHeader("Tweets Per Minute (grouped by timestamp_ms)", ImGuiTreeNodeFlags_Framed | ImGuiTreeNodeFlags_DefaultOpen))
                {
                    auto& tpm = vm.data->groupedtpm;

                    if (!m.groupedtweets.empty()) {
                        ImGui::Text("%s -> %s", 
                            utctextfrom(duration_cast<seconds>(m.groups.front().begin)).c_str(),
                            utctextfrom(duration_cast<seconds>(m.groups.back().end)).c_str());
                    }
                    ImVec2 plotposition = ImGui::GetCursorScreenPos();
                    ImVec2 plotextent(ImGui::GetContentRegionAvailWidth(),100);
                    ImGui::PlotLines("", &tpm[0], tpm.size(), 0, nullptr, 0.0f, vm.data->maxtpm, plotextent);
                    if (tpm.size() == m.groups.size() && ImGui::IsItemHovered()) {
                        const float t = Clamp((ImGui::GetMousePos().x - plotposition.x) / plotextent.x, 0.0f, 0.9999f);
                        idx = (int)(t * (m.groups.size() - 1));
                    }
                    idx = min(idx, int(m.groups.size()) - 1);
                    int defaultGroupIndex = (keep / every) - ((length / every) - 1);
                    if (idx == -1 && int(m.groups.size()) > defaultGroupIndex) {
                        idx = defaultGroupIndex;
                    }

                    ImGui::PushItemWidth(ImGui::GetContentRegionAvailWidth());
                    ImGui::SliderInt("", &idx, 0, m.groups.size() - 1);
                }
                End.dismiss();
            }
            ImGui::End();

            // ImGui::SetNextWindowSize(ImVec2(100,200), ImGuiSetCond_FirstUseEver);
            // if (ImGui::Begin("Top Users from group")) {
            //     RXCPP_UNWIND(End, [](){
            //         ImGui::End();
            //     });

            //     static vector<UserCount> top;
            //     top.clear();
            //     top = *vm.data->scope_users |
            //         ranges::view::filter([&](const UserCount& u){ return wordfilter.PassFilter(u.user.data->user["screen_name"].c_str()); }) |
            //         ranges::view::take(10);

            //     float maxCount = 0.0f;
            //     // for(auto& w : m.groups) {
            //     //     auto& g = m.groupedtweets.at(w);
            //         for(auto& u : top) {
            //             auto& counts = *(u.user.data);
            //             float count = static_cast<float>(counts.retweets + counts.tweets + counts.quotetweets);
            //             maxCount = count > maxCount ? count : maxCount;
            //             u.all.push_back(count);
            //         }
            //     // }

            //     for (auto& u : top) {
            //         ImGui::Separator();
            //         string name = u.user.data->user["name"].get<string>();
            //         string screenName = u.user.data->user["screen_name"].get<string>();
            //         ImGui::Text("%s (@%s)", name.c_str(), screenName.c_str());

            //         ImVec2 plotextent(ImGui::GetContentRegionAvailWidth(),100);
            //         ImGui::PlotLines("", &u.all[0], u.all.size(), 0, nullptr, 0.0f, maxCount, plotextent);
            //     }

            //     End.dismiss();
            // }
            // ImGui::End();

            ImGui::SetNextWindowSize(ImVec2(100,200), ImGuiSetCond_FirstUseEver);
            if (ImGui::Begin("Top Words from group")) {
                RXCPP_UNWIND(End, [](){
                    ImGui::End();
                });

                ImGui::RadioButton("Selected", &model::scope, scope_selected); ImGui::SameLine();
                ImGui::RadioButton("All -", &model::scope, scope_all_negative); ImGui::SameLine();
                ImGui::RadioButton("All +", &model::scope, scope_all_positive); ImGui::SameLine();
                ImGui::RadioButton("All \u2620", &model::scope, scope_all_toxic); ImGui::SameLine();
                ImGui::RadioButton("All", &model::scope, scope_all);

                ImGui::Text("%s -> %s", vm.data->scope_begin.c_str(), vm.data->scope_end.c_str());

                {
                    ImGui::Columns(2);
                    RXCPP_UNWIND_AUTO([](){
                        ImGui::Columns(1);
                    });
                    ImGui::Text("Tweets: %ld", vm.data->scope_tweets->size()); ImGui::NextColumn();
                    ImGui::Text("Words : %ld", vm.data->scope_words->size());
                }

                wordfilter.Draw();

                if (settings["WordFilter"].get<string>() != wordfilter.InputBuf) {
                    settings["WordFilter"] = string{wordfilter.InputBuf};
                    ofstream o(settingsFile);
                    o << setw(4) << settings;
                }
                
                static vector<WordCount> top;
                top.clear();
                top = *vm.data->scope_words |
                    ranges::view::filter([&](const WordCount& w){ return wordfilter.PassFilter(w.word.c_str()); }) |
                    ranges::view::take(10);

                float maxCount = 0.0f;
                for(auto& w : m.groups) {
                    auto& g = m.groupedtweets.at(w);
                    auto end = g->words.end();
                    for(auto& word : top) {
                        auto wrd = g->words.find(word.word);
                        float count = 0.0f;
                        if (wrd != end) {
                            count = static_cast<float>(wrd->second);
                        }
                        maxCount = count > maxCount ? count : maxCount;
                        word.all.push_back(count);
                    }
                }

                for (auto& w : top) {

                    auto total = m.allwords[w.word];
                    ImGui::Text("%4.d,", total); ImGui::SameLine();
                    auto positive = m.positivewords[w.word];
                    ImGui::TextColored(positivecolor, " +%4.d,", positive); ImGui::SameLine();
                    auto negative = m.negativewords[w.word];
                    ImGui::TextColored(negativecolor, " -%4.d", negative); ImGui::SameLine();
                    auto toxic = m.toxicwords[w.word];
                    ImGui::TextColored(negativecolor, " \u2620%4.d", toxic); ImGui::SameLine();
                    if (negative > positive) {
                        ImGui::TextColored(negativecolor, " -%6.2fx", negative / std::max(float(positive), 1.0f)); ImGui::SameLine();
                    } else {
                        ImGui::TextColored(positivecolor, " +%6.2fx", positive / std::max(float(negative), 1.0f)); ImGui::SameLine();
                    }
                    ImGui::Text(" \u2620%6.2fx", toxic / std::max(float(std::min(total, 10 * 60)-toxic), 1.0f)); ImGui::SameLine();
                    ImGui::Text(" - %s", w.word.c_str());

                    ImVec2 plotextent(ImGui::GetContentRegionAvailWidth(),100);
                    ImGui::PlotLines("", &w.all[0], w.all.size(), 0, nullptr, 0.0f, maxCount, plotextent);
                }

                End.dismiss();
            }
            ImGui::End();

            ImGui::SetNextWindowSize(ImVec2(100,200), ImGuiSetCond_FirstUseEver);
            if (ImGui::Begin("Word Cloud from group")) {
                RXCPP_UNWIND(End, [](){
                    ImGui::End();
                });

                static const ImVec4 textcolor = ImGui::GetStyle().Colors[ImGuiCol_Text];
                if (ImGui::BeginPopupContextWindow())
                {
                    RXCPP_UNWIND_AUTO([](){
                        ImGui::EndPopup();
                    });

                    ImGui::ColorEdit3("positivecolor", reinterpret_cast<float*>(&positivecolor), ImGuiColorEditFlags_Float);
                    ImGui::ColorEdit3("neutralcolor", reinterpret_cast<float*>(&neutralcolor), ImGuiColorEditFlags_Float);
                    ImGui::ColorEdit3("negativecolor", reinterpret_cast<float*>(&negativecolor), ImGuiColorEditFlags_Float);

                    if (ImGui::Button("Close"))
                        ImGui::CloseCurrentPopup();
                }

                auto origin = ImGui::GetCursorScreenPos();
                auto area = ImGui::GetContentRegionAvail();
                auto clip = ImVec4(origin.x, origin.y, origin.x + area.x, origin.y + area.y);

                auto font = ImGui::GetFont();
                auto scale = 4.0f;

                static vector<ImRect> taken;
                taken.clear();

                // start a reproducible series each frame.
                mt19937 source;

                auto maxCount = 0;
                auto cursor = vm.data->scope_words->begin();
                auto end = vm.data->scope_words->end();
                for(;cursor != end; ++cursor) {
                    if (!wordfilter.PassFilter(cursor->word.c_str())) continue;

                    maxCount = max(maxCount, cursor->count);

                    auto neutral = m.allwords[cursor->word] - (m.negativewords[cursor->word] + m.positivewords[cursor->word]);
                    using CountedColor = pair<int, ImVec4>;
                    auto color = (std::array<CountedColor, 4>{{
                        {neutral, textcolor}, 
                        {m.negativewords[cursor->word], negativecolor},
                        {m.positivewords[cursor->word], positivecolor},
                        {m.toxicwords[cursor->word], negativecolor}
                    }} |
                    ranges::action::sort([](const CountedColor& l, const CountedColor& r){
                        return l.first > r.first;
                    }))[0].second;
    
                    auto place = Clamp(static_cast<float>(cursor->count)/maxCount, 0.0f, 0.9999f);
                    auto size = Clamp(font->FontSize*scale*place, font->FontSize*scale*0.25f, font->FontSize*scale);
                    auto extent = font->CalcTextSizeA(size, fltmax, 0.0f, &cursor->word[0], &cursor->word[0] + cursor->word.size(), nullptr);

                    auto offsetx = uniform_int_distribution<>(0, area.x - extent.x);
                    auto offsety = uniform_int_distribution<>(0, area.y - extent.y);

                    ImRect bound;
                    int checked = -1;
                    int trys = 10;
                    for (;checked < int(taken.size()) && trys > 0;--trys){
                        checked = 0;
                        auto position = ImVec2(origin.x + offsetx(source), origin.y + offsety(source));
                        bound = ImRect(position.x, position.y, position.x + extent.x, position.y + extent.y);
                        for(auto& t : taken) {
                            if (t.Overlaps(bound)) break;
                            ++checked;
                        }
                    }

                    if (checked < int(taken.size()) && trys == 0) {
                        //word did not fit
                        break;
                    }

                    ImGui::GetWindowDrawList()->AddText(font, size, bound.Min, ImColor(color), &cursor->word[0], &cursor->word[0] + cursor->word.size(), 0.0f, &clip);
                    taken.push_back(bound);
                }
                End.dismiss();
            }
            ImGui::End();

            ImGui::SetNextWindowSize(ImVec2(100,200), ImGuiSetCond_FirstUseEver);
            if (ImGui::Begin("Tweets from group")) {
                RXCPP_UNWIND(End, [](){
                    ImGui::End();
                });

                if (ImGui::BeginPopupContextWindow())
                {
                    RXCPP_UNWIND_AUTO([](){
                        ImGui::EndPopup();
                    });

                    ImGui::ColorEdit3("positivecolor", reinterpret_cast<float*>(&positivecolor), ImGuiColorEditFlags_Float);
                    ImGui::ColorEdit3("neutralcolor", reinterpret_cast<float*>(&neutralcolor), ImGuiColorEditFlags_Float);
                    ImGui::ColorEdit3("negativecolor", reinterpret_cast<float*>(&negativecolor), ImGuiColorEditFlags_Float);
                    
                    if (ImGui::Button("Close"))
                        ImGui::CloseCurrentPopup();
                }

                static ImGuiTextFilter filter(settings["TweetFilter"].get<string>().c_str());

                filter.Draw();

                if (settings["TweetFilter"].get<string>() != filter.InputBuf) {
                    settings["TweetFilter"] = string{filter.InputBuf};
                    ofstream o(settingsFile);
                    o << setw(4) << settings;
                }

                auto cursor = vm.data->scope_tweets->rbegin();
                auto end = vm.data->scope_tweets->rend();
                for(int remaining = 50;cursor != end && remaining > 0; ++cursor) {
                    auto& tweet = cursor->data->tweet;
                    if (tweet["user"]["name"].is_string() && tweet["user"]["screen_name"].is_string()) {
                        auto name = tweet["user"]["name"].get<string>();
                        auto screenName = tweet["user"]["screen_name"].get<string>();
                        auto sentiment = m.sentiment[tweet["id_str"]];
                        auto perspective = m.perspective[tweet["id_str"]];
                        auto color = sentiment == "positive" ? positivecolor : sentiment == "negative" ? negativecolor : neutralcolor;
                        auto text = tweettext(tweet);
                        auto passSentiment = model::scope == scope_all_negative ? sentiment == "negative" : model::scope == scope_all_positive ? sentiment == "positive" : true;
                        auto passPerspective = model::scope == scope_all_toxic ? perspective.toxicity > 0.7f : true;
                        if (passSentiment && passPerspective && (filter.PassFilter(name.c_str()) || filter.PassFilter(screenName.c_str()) || filter.PassFilter(text.c_str()))) {
                            --remaining;
                            ImGui::Separator();
                            ImGui::Text("%s (@%s) - ", name.c_str() , screenName.c_str() ); ImGui::SameLine();
                            ImGui::TextColored(color, "%s", sentiment.c_str()); ImGui::SameLine();
                            ImGui::Text(" \u2620%6.2f, %6.2f, %6.2f", perspective.toxicity, perspective.spam, perspective.inflammatory);
                            ImGui::TextWrapped("%s", text.c_str());
                        }
                    }
                }

                End.dismiss();
            }
            ImGui::End();

        }) |
        reportandrepeat());

    // render recent
    renderers.push_back(
        draw |
        tap([=](const ViewModel& vm){
            auto& m = *vm.m.data;

            ImGui::SetNextWindowSize(ImVec2(100,200), ImGuiSetCond_FirstUseEver);
            if (ImGui::Begin("Recent Tweets")) {
                RXCPP_UNWIND(End, [](){
                    ImGui::End();
                });

                ImGui::TextWrapped("url: %s", m.url.c_str());
                ImGui::Text("Total Tweets: %d", m.total);

                if (!m.tweets.empty()) {
                    // smoothly scroll through tweets.

                    auto& front = m.tweets.front().data->tweet;

                    static auto remove = 0.0f;
                    static auto ratio = 1.0f;
                    static auto oldestid = front["id_str"].is_string() ? front["id_str"].get<string>() : string{};

                    // find first tweet to display
                    auto cursor = m.tweets.rbegin();
                    auto end = m.tweets.rend();
                    cursor = find_if(cursor, end, [&](const Tweet& tw){
                        auto& tweet = tw.data->tweet;
                        auto id = tweet["id_str"].is_string() ? tweet["id_str"].get<string>() : string{};
                        return id == oldestid;
                    });

                    auto remaining = cursor - m.tweets.rbegin();

                    // scale display speed from 1 new tweet a frame to zero new tweets per frame
                    ratio = float(remaining) / 50;
                    remove += ratio;

                    auto const count = end - cursor;
                    if (count == 0) {
                        // reset top tweet after discontinuity
                        remove = 0.0f;
                        oldestid = front["id_str"].is_string() ? front["id_str"].get<string>() : string{};
                    } else if (remove > .999f) {
                        // reset to display next tweet
                        auto it = cursor;
                        while (remove > .999f && (end - it) < int(m.tweets.size()) ) {
                            remove -= 1.0f;
                            --it;
                            oldestid = it->data->tweet["id_str"].is_string() ? it->data->tweet["id_str"].get<string>() : string{};
                        }
                        remove = 0.0f;
                    }

                    {
                        ImGui::Columns(2);
                        RXCPP_UNWIND_AUTO([](){
                            ImGui::Columns(1);
                        });
                        ImGui::Text("scroll speed: %.2f", ratio); ImGui::NextColumn();
                        ImGui::Text("pending: %ld", remaining);
                    }

                    // display no more than 50 at a time
                    while(end - cursor > 50) --end;

                    // display tweets
                    for(;cursor != end; ++cursor) {
                        auto& tweet = cursor->data->tweet;
                        if (tweet["user"]["name"].is_string() && tweet["user"]["screen_name"].is_string()) {
                            ImGui::Separator();
                            ImGui::Text("%s (@%s)", tweet["user"]["name"].get<string>().c_str() , tweet["user"]["screen_name"].get<string>().c_str() );
                            ImGui::TextWrapped("%s", tweettext(tweet).c_str());
                        }
                    }
                }
                End.dismiss();
            }
            ImGui::End();
        }) |
        reportandrepeat());

    // render controls
    renderers.push_back(
        draw |
        tap([=, &jsonfile, &dumptext, &dumpjson](const ViewModel&){

            ImGui::SetNextWindowSize(ImVec2(100,200), ImGuiSetCond_FirstUseEver);
            if (ImGui::Begin("Output")) {
                RXCPP_UNWIND(End, [](){
                    ImGui::End();
                });

                static int dumpmode = dumptext ? 1 : dumpjson ? 2 : 0;
                ImGui::RadioButton("None", &dumpmode, 0); ImGui::SameLine();
                ImGui::RadioButton("Text", &dumpmode, 1); ImGui::SameLine();
                ImGui::RadioButton("Json", &dumpmode, 2);
                dumptext = dumpmode == 1;
                if (!dumpjson && dumpmode == 2) {
                    jsonfile = newJsonFile();
                }
                dumpjson = dumpmode == 2;

                End.dismiss();
            }
            ImGui::End();
        }) |
        reportandrepeat());

    // render framerate
    renderers.push_back(
        draw |
        tap([=](const ViewModel&){

            ImGui::SetNextWindowSize(ImVec2(100,200), ImGuiSetCond_FirstUseEver);
            if (ImGui::Begin("Twitter App")) {
                RXCPP_UNWIND(End, [](){
                    ImGui::End();
                });

                ImGui::Text("Application average %.3f ms/frame (%.1f FPS)", 1000.0f / ImGui::GetIO().Framerate, ImGui::GetIO().Framerate);
                End.dismiss();
            }
            ImGui::End();
        }) |
        reportandrepeat());

    // subscribe to everything!
    iterate(renderers) |
        merge() |
        subscribe<ViewModel>(lifetime, [](const ViewModel&){});

    // ==== Main

    // main loop
    while(lifetime.is_subscribed()) {
        SDL_Event event;
        while (SDL_PollEvent(&event))
        {
            ImGui_ImplSDL2_ProcessEvent(&event);
            if (event.type == SDL_QUIT) {
                lifetime.unsubscribe();
                break;
            }
            if (event.type == SDL_WINDOWEVENT && event.window.event == SDL_WINDOWEVENT_CLOSE && event.window.windowID == SDL_GetWindowID(window)) {
                lifetime.unsubscribe();
                break;
            }
        }

        if (!lifetime.is_subscribed()) {
            break;
        }

        Mix_PlayingMusic();

        ImGui_ImplOpenGL2_NewFrame();
        ImGui_ImplSDL2_NewFrame(window);
        ImGui::NewFrame();

        while (!rl.empty() && rl.peek().when < rl.now()) {
            rl.dispatch();
        }

        sendframe();

        while (!rl.empty() && rl.peek().when < rl.now()) {
            rl.dispatch();
        }

        // Rendering
        ImGui::Render();
        glViewport(0, 0, (int)ImGui::GetIO().DisplaySize.x, (int)ImGui::GetIO().DisplaySize.y);
        glClearColor(clear_color.x, clear_color.y, clear_color.z, clear_color.w);
        glClear(GL_COLOR_BUFFER_BIT);
        ImGui_ImplOpenGL2_RenderDrawData(ImGui::GetDrawData());
        SDL_GL_SwapWindow(window);
    }

    return 0;
}
