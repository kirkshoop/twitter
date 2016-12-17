#pragma once

namespace tweets {

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

auto isEndOfTweet = [](const string& s){
    if (s.size() < 2) return false;
    auto it0 = s.begin() + (s.size() - 2);
    auto it1 = s.begin() + (s.size() - 1);
    return *it0 == '\r' && *it1 == '\n';
};

inline auto parsetweets(observe_on_one_worker tweetthread) -> function<observable<shared_ptr<const json>>(observable<string>)> {
    return [=](observable<string> chunks){
        // create strings split on \r
        auto strings = chunks |
            concat_map([](const string& s){
                auto splits = split(s, "\r\n");
                return iterate(move(splits));
            }) |
            filter([](const string& s){
                return !s.empty();
            }) |
            publish() |
            ref_count();

        // filter to last string in each line
        auto closes = strings |
            filter(isEndOfTweet) |
            rxo::map([](const string&){return 0;});

        // group strings by line
        auto linewindows = strings |
            window_toggle(closes | start_with(0), [=](int){return closes;});

        // reduce the strings for a line into one string
        auto lines = linewindows |
            flat_map([](const observable<string>& w) {
                return w | start_with<string>("") | sum();
            });

        return lines |
            filter([](const string& s){
                return s.size() > 2 && s.find_first_not_of("\r\n") != string::npos;
            }) | 
            observe_on(tweetthread) |
            rxo::map([](const string& line){
                return make_shared<const json>(json::parse(line));
            });
    };
}

inline auto onlytweets() -> function<observable<shared_ptr<const json>>(observable<shared_ptr<const json>>)> {
    return [](observable<shared_ptr<const json>> s){
        return s | filter([](const shared_ptr<const json>& tw){
            auto& tweet = *tw;
            return !!tweet.count("timestamp_ms");
        });
    };
}

enum class errorcodeclass {
    Invalid,
    TcpRetry,
    ErrorRetry,
    StatusRetry,
    RateLimited
};

inline errorcodeclass errorclassfrom(const http_exception& ex) {
    switch(ex.code()) {
        case CURLE_COULDNT_RESOLVE_HOST:
        case CURLE_COULDNT_CONNECT:
        case CURLE_OPERATION_TIMEDOUT:
        case CURLE_BAD_CONTENT_ENCODING:
        case CURLE_REMOTE_FILE_NOT_FOUND:
            return errorcodeclass::ErrorRetry;
        case CURLE_GOT_NOTHING:
        case CURLE_PARTIAL_FILE:
        case CURLE_SEND_ERROR:
        case CURLE_RECV_ERROR:
            return errorcodeclass::TcpRetry;
        default:
            if (ex.code() == CURLE_HTTP_RETURNED_ERROR || ex.httpStatus() > 200) {
                if (ex.httpStatus() == 420) {
                    return errorcodeclass::RateLimited;
                } else if (ex.httpStatus() == 404 ||
                    ex.httpStatus() == 406 ||
                    ex.httpStatus() == 413 ||
                    ex.httpStatus() == 416) {
                    return errorcodeclass::Invalid;
                }
            }
    };
    return errorcodeclass::StatusRetry;
}

auto filechunks = [](observe_on_one_worker tweetthread, string filepath) {
    return observable<>::create<string>([=](subscriber<string> out){

        auto values = make_tuple(ifstream{filepath}, string{});
        auto state = make_shared<decltype(values)>(move(values));

        // creates a worker whose lifetime is the same as this subscription
        auto coordinator = tweetthread.create_coordinator(out.get_subscription());

        auto controller = coordinator.get_worker();

        auto producer = [out, state](const rxsc::schedulable& self) {

            if (!out.is_subscribed()) {
                // terminate loop
                return;
            }

            if (getline(get<0>(*state), get<1>(*state)))
            {
                get<1>(*state)+="\r\n";
                out.on_next(get<1>(*state));
            } else {
                out.on_completed();
                return;
            }

            // tail recurse this same action to continue loop
            self();
        };

        //controller.schedule_periodically(controller.now(), milliseconds(100), coordinator.act(producer));
        controller.schedule(coordinator.act(producer));
    });
};

auto twitter_stream_reconnection = [](observe_on_one_worker tweetthread){
    return [=](observable<string> chunks){
        return chunks |
            // https://dev.twitter.com/streaming/overview/connecting
            timeout(seconds(90), tweetthread) |
            on_error_resume_next([=](std::exception_ptr ep) -> observable<string> {
                try {rethrow_exception(ep);}
                catch (const http_exception& ex) {
                    cerr << ex.what() << endl;
                    switch(errorclassfrom(ex)) {
                        case errorcodeclass::TcpRetry:
                            cerr << "reconnecting after TCP error" << endl;
                            return observable<>::empty<string>();
                        case errorcodeclass::ErrorRetry:
                            cerr << "error code (" << ex.code() << ") - ";
                        case errorcodeclass::StatusRetry:
                            cerr << "http status (" << ex.httpStatus() << ") - waiting to retry.." << endl;
                            return observable<>::timer(seconds(5), tweetthread) | stringandignore();
                        case errorcodeclass::RateLimited:
                            cerr << "rate limited - waiting to retry.." << endl;
                            return observable<>::timer(minutes(1), tweetthread) | stringandignore();
                        case errorcodeclass::Invalid:
                            cerr << "invalid request - exit" << endl;
                        default:
                            return observable<>::error<string>(ep, tweetthread);
                    };
                }
                catch (const timeout_error& ex) {
                    cerr << "reconnecting after timeout" << endl;
                    return observable<>::empty<string>();
                }
                catch (const exception& ex) {
                    cerr << ex.what() << endl;
                    terminate();
                }
                catch (...) {
                    cerr << "unknown exception - not derived from std::exception" << endl;
                    terminate();
                }
                return observable<>::error<string>(ep, tweetthread);
            }) |
            repeat(0);
    };
};

auto twitterrequest = [](observe_on_one_worker tweetthread, ::rxcurl::rxcurl factory, string URL, string method, string CONS_KEY, string CONS_SEC, string ATOK_KEY, string ATOK_SEC){

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

        return factory.create(http_request{url, method}) |
            rxo::map([](http_response r){
                return r.body.chunks;
            }) |
            merge(tweetthread);
    }) |
    twitter_stream_reconnection(tweetthread);
};


}
