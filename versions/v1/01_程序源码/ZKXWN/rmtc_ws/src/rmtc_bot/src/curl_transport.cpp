// OWNER: B
#include "rmtc_bot/llm_client.hpp"
#ifdef RMTC_HAS_CURL
#include <curl/curl.h>
#include <memory>
#include <stdexcept>

namespace rmtc_bot {
namespace {
struct CurlGlobal { CurlGlobal(){if(curl_global_init(CURL_GLOBAL_DEFAULT)!=CURLE_OK)throw std::runtime_error("curl init failed");} ~CurlGlobal(){curl_global_cleanup();} };
std::size_t append(char* ptr,std::size_t size,std::size_t count,void* data) {
    auto& out=*static_cast<std::string*>(data);const auto bytes=size*count;
    if(out.size()+bytes>2097152)return 0;
    out.append(ptr,bytes);return bytes;
}
int progress(void* stop,curl_off_t,curl_off_t,curl_off_t,curl_off_t) {
    return static_cast<const std::atomic<bool>*>(stop)->load()?1:0;
}
class CurlTransport final:public HttpTransport {
public:
    HttpReply post(const Config& c,const std::string& bytes,const std::atomic<bool>& stop)override {
        static CurlGlobal global;
        std::unique_ptr<CURL,decltype(&curl_easy_cleanup)> handle(curl_easy_init(),&curl_easy_cleanup);
        if(!handle)return {};
        struct Headers {curl_slist* value=nullptr;~Headers(){curl_slist_free_all(value);}} headers;
        headers.value=curl_slist_append(headers.value,"Content-Type: application/json");
        const auto auth="Authorization: Bearer "+c.token;
        headers.value=curl_slist_append(headers.value,auth.c_str());
        auto* h=handle.get();HttpReply reply;
        curl_easy_setopt(h,CURLOPT_URL,c.gateway.c_str());
        curl_easy_setopt(h,CURLOPT_HTTPHEADER,headers.value);
        curl_easy_setopt(h,CURLOPT_POSTFIELDS,bytes.data());
        curl_easy_setopt(h,CURLOPT_POSTFIELDSIZE_LARGE,static_cast<curl_off_t>(bytes.size()));
        curl_easy_setopt(h,CURLOPT_TIMEOUT_MS,static_cast<long>(c.request_timeout_ms));
        curl_easy_setopt(h,CURLOPT_CONNECTTIMEOUT_MS,static_cast<long>(c.request_timeout_ms));
        curl_easy_setopt(h,CURLOPT_FOLLOWLOCATION,0L);
        curl_easy_setopt(h,CURLOPT_NOSIGNAL,1L);
        curl_easy_setopt(h,CURLOPT_WRITEFUNCTION,&append);curl_easy_setopt(h,CURLOPT_WRITEDATA,&reply.body);
        curl_easy_setopt(h,CURLOPT_NOPROGRESS,0L);
        curl_easy_setopt(h,CURLOPT_XFERINFOFUNCTION,&progress);curl_easy_setopt(h,CURLOPT_XFERINFODATA,&stop);
        const auto code=curl_easy_perform(h);
        if(code!=CURLE_OK)return {};
        curl_easy_getinfo(h,CURLINFO_RESPONSE_CODE,&reply.status);return reply;
    }
};
}
std::shared_ptr<HttpTransport> make_curl_transport(){return std::make_shared<CurlTransport>();}
}
#else
namespace rmtc_bot {std::shared_ptr<HttpTransport> make_curl_transport(){return {};}}
#endif
