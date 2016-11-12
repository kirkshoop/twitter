#pragma once

namespace rxcurl {

struct rxcurl_state
{
    ~rxcurl_state(){
        if (!!curlm) {
            worker.as_blocking().subscribe();
            curl_multi_cleanup(curlm);
            curlm = nullptr;
        }
    }
    rxcurl_state() : thread(observe_on_new_thread()), worker(), curlm(curl_multi_init()) {
        worker = observable<>::create<int>([this](subscriber<int> out){
                while(out.is_subscribed()) {
                    int running = 0;
                    curl_multi_perform(curlm, &running);
                    out.on_next(running);
                    int handlecount = 0;
                    curl_multi_wait(curlm, nullptr, 0, 500, &handlecount);
                    if (handlecount == 0) {
                        this_thread::sleep_for(milliseconds(100));
                    }
                }
                out.on_completed();
            }) |
            subscribe_on(thread) |
            publish() |
            connect_forever();
    }
    rxcurl_state(const rxcurl_state&) = delete;
    rxcurl_state& operator=(const rxcurl_state&) = delete;
    rxcurl_state(rxcurl_state&&) = delete;
    rxcurl_state& operator=(rxcurl_state&&) = delete;

    observe_on_one_worker thread;
    observable<int> worker;
    CURLM* curlm;
};
struct http_request
{
    string url;
    string method;
};
struct http_state
{
    ~http_state() {
        if (!!curl) {
            rxcurl->worker
                .take(1)
                .tap([=](int){
                    curl_multi_remove_handle(rxcurl->curlm, curl);
                    curl_easy_cleanup(curl);
                })
                .as_blocking()
                .subscribe();

            curl = nullptr;
        }
    }
    explicit http_state(shared_ptr<rxcurl_state> m, http_request r) : rxcurl(m), request(r), curl(nullptr) {}
    http_state(const http_state&) = delete;
    http_state& operator=(const http_state&) = delete;
    http_state(http_state&&) = delete;
    http_state& operator=(http_state&&) = delete;

    shared_ptr<rxcurl_state> rxcurl;
    http_request request;
    subjects::subject<string> chunkbus;
    CURL* curl;
};
struct http_body
{
    observable<string> chunks;
    observable<string> complete;
};
struct http_response
{
    const http_request request;
    http_body body;

    shared_ptr<http_state> state;
};

size_t rxcurlhttpCallback(char* ptr, size_t size, size_t nmemb, http_state* response) {
    int iRealSize = size * nmemb;

    auto chunkout = response->chunkbus.get_subscriber();
    string chunk;
    chunk.assign(ptr, iRealSize);
    chunkout.on_next(chunk);

    return iRealSize;
}

struct rxcurl
{
    shared_ptr<rxcurl_state> state;

    observable<http_response> create(http_request request){
        return observable<>::create<http_response>([=](subscriber<http_response>& out){

            auto requestState = make_shared<http_state>(state, request);

            http_response r{request, http_body{}, requestState};

            r.body.chunks = r.state->chunkbus.get_observable()
                .tap([requestState](const string&){}); // keep connection alive

            r.body.complete = r.state->chunkbus.get_observable()
                .start_with(string{})
                .tap([requestState](const string&){}) // keep connection alive
                .sum() 
                .replay(1);
            
            // subscriber must subscribe during the on_next call to receive all marbles
            out.on_next(r);
            out.on_completed();

            state->worker
                .take(1)
                .tap([=](int){

                    auto curl = curl_easy_init();

                    // ==== cURL Setting
                    curl_easy_setopt(curl, CURLOPT_URL, r.state->request.url.c_str());

                    if (request.method == "POST") {
                        // - POST data
                        curl_easy_setopt(curl, CURLOPT_POST, 1L);
                        // - specify the POST data
                        curl_easy_setopt(curl, CURLOPT_POSTFIELDS, "");
                    }

                    // - User agent name
                    curl_easy_setopt(curl, CURLOPT_USERAGENT, "rxcpp stream analisys");
                    // - HTTP STATUS >=400 ---> ERROR
                    curl_easy_setopt(curl, CURLOPT_FAILONERROR, 1);
                    // - Callback function
                    curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, rxcurlhttpCallback);
                    // - Write data
                    curl_easy_setopt(curl, CURLOPT_WRITEDATA, (void *)r.state.get());

                    r.state->curl = curl;
                    curl_multi_add_handle(state->curlm, curl);
                })
                .subscribe();
        });
    }
};

rxcurl create_rxcurl() {
    rxcurl r{make_shared<rxcurl_state>()};
    return r;
};

}
