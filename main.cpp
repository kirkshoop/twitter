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

#include "imgui/imgui.h"

#include "imgui/imgui_internal.h"

#include "imgui/imgui_impl_sdl_gl3.h"
#include <GL/glew.h>
#include <SDL.h>

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
const auto every = milliseconds(5000);
auto keep = minutes(10);

inline void updategroups(
    Model& model,
    milliseconds timestamp, 
    Tweet tw = Tweet(), 
    const vector<string>& words = vector<string>{}) {

    auto& md = model.data;

    for (auto& word: words) {
        ++md->allwords[word];
    }

    auto& m = *md;

    auto searchbegin = duration_cast<minutes>(duration_cast<minutes>(timestamp) - length);
    if (words.size() > 0) {
        searchbegin = duration_cast<minutes>(duration_cast<minutes>(timestamp) - keep);
    }
    auto searchend = timestamp;
    auto offset = milliseconds(0);
    for (;searchbegin+offset < searchend;offset += duration_cast<milliseconds>(every)){
        auto key = TimeRange{searchbegin+offset, searchbegin+offset+length};
        auto it = m.groupedtweets.find(key);
        if (it == m.groupedtweets.end()) {
            // add group
            m.groups.push_back(key);
            m.groups |= ranges::action::sort(less<TimeRange>{});
            it = m.groupedtweets.insert(make_pair(key, make_shared<TweetGroup>())).first;
        }

        if (words.size() > 0) {
            if (searchbegin+offset <= timestamp && timestamp < searchbegin+offset+length) {
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
}

int main(int argc, const char *argv[])
{
    // ==== Parse args

    auto selector = string{tolower(argc > 1 ? argv[1] : "")};

    const bool playback = argc == 3 && selector == "playback";
    const bool gui = argc == 6;

    bool dumpjson = argc == 7 && selector == "dumpjson";
    bool dumptext = argc == 7 && selector == "dumptext";

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

    // ==== Constants - paths
    string URL = "https://stream.twitter.com/1.1/statuses/";
    string filepath;
    if (!playback) {
        URL += argv[5 + argoffset];
        cerr << "url = " << URL.c_str() << endl;
    } else {
        filepath = argv[1 + argoffset];
        cerr << "file = " << filepath.c_str() << endl;
    }

    // ==== Constants - flags
    const bool isFilter = URL.find("/statuses/filter") != string::npos;
    string method = isFilter ? "POST" : "GET";

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

    // Setup Fonts

    ImGuiIO& io = ImGui::GetIO();

    static const ImWchar noto[] = { 
        0x0020, 0x0513,
        0x1e00, 0x1f4d,
        0x2000, 0x25ca,
        0xfb01, 0xfb04,
        0xfeff, 0xfffd, 
        0 };
    io.Fonts->AddFontFromFileTTF("./NotoMono-Regular.ttf", 13.0f, nullptr, noto);

    static ImFontConfig config;
    config.MergeMode = true;
    static const ImWchar symbols[] = { 
        0x20a0, 0x2e3b, 
        0x3008, 0x3009, 
        0x4dc0, 0x4dff, 
        0xa700, 0xa71f, 
        0 };
    io.Fonts->AddFontFromFileTTF("./NotoSansSymbols-Regular.ttf", 13.0f, &config, symbols);

    io.Fonts->Build();

    // Cleanup
    RXCPP_UNWIND_AUTO([&](){
        ImGui_ImplSdlGL3_Shutdown();
        SDL_GL_DeleteContext(glcontext);
        SDL_DestroyWindow(window);
        SDL_Quit();
    });

    auto mainthreadid = this_thread::get_id();
    auto mainthread = observe_on_run_loop(rl);

    auto tweetthread = observe_on_new_thread();
    auto poolthread = observe_on_event_loop();

    auto factory = create_rxcurl();

    composite_subscription lifetime;

    // ==== Tweets

    observable<string> chunks;

    // request tweets
    if (playback) {
        chunks = filechunks(tweetthread, filepath);
    } else {
        chunks = twitterrequest(tweetthread, factory, URL, method, CONS_KEY, CONS_SEC, ATOK_KEY, ATOK_SEC);
    }

    // parse tweets
    auto tweets = chunks | parsetweets(poolthread, tweetthread);

    // share tweets
    auto ts = tweets |
        on_error_resume_next([](std::exception_ptr ep){
            cerr << rxu::what(ep) << endl;
            return observable<>::empty<Tweet>();
        }) |
        repeat(0) |
        publish() |
        ref_count();

    // ==== Model

    vector<observable<Reducer>> reducers;

    // dump json to cout
    reducers.push_back(
        ts |
        filter([&](const Tweet&){
            return dumpjson;
        }) |
        tap([=](const Tweet& tw){
            auto& tweet = tw.data->tweet;
            cout << tweet << "\r\n";
        }) |
        noopandignore() |
        start_with(noop));

    // dump text to cout
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
                    updategroups(m, rangebegin);
                    return std::move(m);
                });
            }) |
            nooponerror() |
            start_with(noop));
    }

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

                updategroups(m, t, tw, words);

                return std::move(m);
            });
        }) |
        nooponerror() |
        start_with(noop));

    // window tweets by the time that they arrive
    reducers.push_back(
        ts |
        onlytweets() |
        window_with_time(length, every, poolthread) |
        rxo::map([](observable<Tweet> source){
            auto rangebegin = time_point_cast<seconds>(system_clock::now()).time_since_epoch();
            auto tweetsperminute = source | 
                start_with(Tweet{}) |
                rxo::map([=](const Tweet& tw) {
                    return Reducer([=](Model& md){
                        auto& m = *(md.data);

                        auto maxsize = (duration_cast<seconds>(keep).count()+duration_cast<seconds>(length).count())/duration_cast<seconds>(every).count();

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

    // keep recent tweets
    reducers.push_back(
        ts |
        onlytweets() |
        buffer_with_time(milliseconds(200), poolthread) |
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
                    for (auto& word: cursor->data->words) {
                        if (--m.allwords[word] == 0)
                        {
                            m.allwords.erase(word);
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
        window_with_time(milliseconds(200), poolthread) |
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
        start_with(Model{}) |
        // only view model updates every 200ms
        sample_with_time(milliseconds(200), mainthread) |
        publish() |
        ref_count();

    // ==== View

    auto viewModels = models |
        // if the processing of the model takes too long, skip until caught up
        filter([=](const Model& m){
            return m.data->timestamp <= mainthread.now();
        }) |
        rxo::map([](Model& m){
            return ViewModel{m};
        }) |
        publish() |
        ref_count();

    vector<observable<ViewModel>> renderers;

    // render analysis
    renderers.push_back(
        frames |
        with_latest_from(rxu::take_at<1>(), viewModels) |
        tap([=](const ViewModel& vm){
            auto renderthreadid = this_thread::get_id();
            if (mainthreadid != renderthreadid) {
                cerr << "render on wrong thread!" << endl;
                terminate();
            }

            auto& m = *vm.m.data;

            static ImGuiTextFilter wordfilter;

            ImGui::SetNextWindowSize(ImVec2(200,100), ImGuiSetCond_FirstUseEver);
            if (ImGui::Begin("Live Analysis")) {
                RXCPP_UNWIND(End, [](){
                    ImGui::End();
                });

                ImGui::TextWrapped("url: %s", URL.c_str());

                {
                    ImGui::Columns(2);
                    RXCPP_UNWIND_AUTO([](){
                        ImGui::Columns(1);
                    });

                    ImGui::Text("Now: %s", utctextfrom().c_str()); ImGui::NextColumn();
                    ImGui::Text("Total Tweets: %d", m.total);
                }

                if (ImGui::CollapsingHeader("Settings", ImGuiTreeNodeFlags_Framed | ImGuiTreeNodeFlags_DefaultOpen))
                {
                    static int minutestokeep = keep.count();
                    ImGui::InputInt("minutes to keep", &minutestokeep);
                    keep = minutes(minutestokeep);
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
                    ImGui::PlotLines("", &tpm[0], tpm.size(), 0, nullptr, 0.0f, fltmax, plotextent);
                    if (tpm.size() == m.groups.size() && ImGui::IsItemHovered()) {
                        const float t = Clamp((ImGui::GetMousePos().x - plotposition.x) / plotextent.x, 0.0f, 0.9999f);
                        idx = (int)(t * (m.groups.size() - 1));
                    }
                    idx = min(idx, int(m.groups.size() - 1));

                    ImGui::PushItemWidth(ImGui::GetContentRegionAvailWidth());
                    ImGui::SliderInt("", &idx, 0, m.groups.size() - 1);
                }
                End.dismiss();
            }
            ImGui::End();

            ImGui::SetNextWindowSize(ImVec2(100,200), ImGuiSetCond_FirstUseEver);
            if (ImGui::Begin("Top Words from group")) {
                RXCPP_UNWIND(End, [](){
                    ImGui::End();
                });

                ImGui::RadioButton("Selected", &model::scope, scope_selected); ImGui::SameLine();
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
                    ImGui::Text("%d - %s", w.count, w.word.c_str());
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
                static ImVec4 hashtagcolor = ImColor(0, 230, 0);
                static ImVec4 mentioncolor = ImColor(0, 200, 230);
                if (ImGui::BeginPopupContextWindow())
                {
                    RXCPP_UNWIND_AUTO([](){
                        ImGui::EndPopup();
                    });

                    ImGui::ColorEdit3("hashtagcolor", reinterpret_cast<float*>(&hashtagcolor));
                    ImGui::ColorEdit3("mentioncolor", reinterpret_cast<float*>(&mentioncolor));

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

                // start a reproducable series each frame.
                mt19937 source;

                auto maxCount = 0;
                auto cursor = vm.data->scope_words->begin();
                auto end = vm.data->scope_words->end();
                for(;cursor != end; ++cursor) {
                    if (!wordfilter.PassFilter(cursor->word.c_str())) continue;

                    maxCount = max(maxCount, cursor->count);

                    auto color = cursor->word[0] == '@' ? mentioncolor : cursor->word[0] == '#' ? hashtagcolor : textcolor;
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

                static ImGuiTextFilter filter;

                filter.Draw();

                auto cursor = vm.data->scope_tweets->rbegin();
                auto end = vm.data->scope_tweets->rend();
                for(int remaining = 50;cursor != end && remaining > 0; ++cursor) {
                    auto& tweet = cursor->data->tweet;
                    if (tweet["user"]["name"].is_string() && tweet["user"]["screen_name"].is_string()) {
                        auto name = tweet["user"]["name"].get<string>();
                        auto screenName = tweet["user"]["screen_name"].get<string>();
                        auto text = tweettext(tweet);
                        if (filter.PassFilter(name.c_str()) || filter.PassFilter(screenName.c_str()) || filter.PassFilter(text.c_str())) {
                            --remaining;
                            ImGui::Separator();
                            ImGui::Text("%s (@%s)", name.c_str() , screenName.c_str() );
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
        frames |
        with_latest_from(rxu::take_at<1>(), viewModels) |
        tap([=](const ViewModel& vm){
            auto renderthreadid = this_thread::get_id();
            if (mainthreadid != renderthreadid) {
                cerr << "render on wrong thread!" << endl;
                terminate();
            }

            auto& m = *vm.m.data;

            ImGui::SetNextWindowSize(ImVec2(100,200), ImGuiSetCond_FirstUseEver);
            if (ImGui::Begin("Recent Tweets")) {
                RXCPP_UNWIND(End, [](){
                    ImGui::End();
                });

                ImGui::TextWrapped("url: %s", URL.c_str());
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
        frames |
        with_latest_from(rxu::take_at<1>(), viewModels) |
        tap([=, &dumptext, &dumpjson](const ViewModel&){
            auto renderthreadid = this_thread::get_id();
            if (mainthreadid != renderthreadid) {
                cerr << "render on wrong thread!" << endl;
                terminate();
            }

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
                dumpjson = dumpmode == 2;

                End.dismiss();
            }
            ImGui::End();
        }) |
        reportandrepeat());

    // render framerate
    renderers.push_back(
        frames |
        with_latest_from(rxu::take_at<1>(), viewModels) |
        tap([=](const ViewModel&){
            auto renderthreadid = this_thread::get_id();
            if (mainthreadid != renderthreadid) {
                cerr << "render on wrong thread!" << endl;
                terminate();
            }

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
        subscribe<ViewModel>([](const ViewModel&){});

    // ==== Main

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

        // Rendering
        glViewport(0, 0, (int)ImGui::GetIO().DisplaySize.x, (int)ImGui::GetIO().DisplaySize.y);
        glClearColor(clear_color.x, clear_color.y, clear_color.z, clear_color.w);
        glClear(GL_COLOR_BUFFER_BIT);
        ImGui::Render();
        SDL_GL_SwapWindow(window);
    }

    return 0;
}