#include "rmtc_bot9/llm.hpp"
#include <curl/curl.h>
namespace rmtc_bot9 {
namespace {
size_t write_cb(char* ptr, size_t size, size_t nmemb, void* userdata) {
    auto* out = static_cast<std::string*>(userdata);
    if(size && nmemb > (1024*1024-out->size())/size) return 0;
    out->append(ptr, size * nmemb);
    return size * nmemb;
}
}
bool http_post(const std::string& endpoint,const std::string& token,const std::string& body,int timeoutMs,
               std::string* response,long* status,const std::atomic<bool>& stopping) {
    response->clear();*status=0;
    static std::once_flag initFlag;static CURLcode initCode=CURLE_FAILED_INIT;
    std::call_once(initFlag,[]{initCode=curl_global_init(CURL_GLOBAL_DEFAULT);});
    if(initCode!=CURLE_OK)return false;
    CURL* curl = curl_easy_init();
    if (!curl) return false;
    struct curl_slist* headers = nullptr;
    headers = curl_slist_append(headers, "Content-Type: application/json");
    const std::string auth = "Authorization: Bearer " + token;
    headers = curl_slist_append(headers, auth.c_str());
    curl_easy_setopt(curl, CURLOPT_URL, endpoint.c_str());
    // The organizer gateway is reached directly on the contest network.
    // Desktop proxy environment must not redirect match tokens or loopback tests.
    curl_easy_setopt(curl, CURLOPT_PROXY, "");
    curl_easy_setopt(curl, CURLOPT_POST, 1L);
    curl_easy_setopt(curl, CURLOPT_POSTFIELDS, body.c_str());
    curl_easy_setopt(curl, CURLOPT_POSTFIELDSIZE, static_cast<long>(body.size()));
    curl_easy_setopt(curl, CURLOPT_HTTPHEADER, headers);
    curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, write_cb);
    curl_easy_setopt(curl, CURLOPT_WRITEDATA, response);
    curl_easy_setopt(curl, CURLOPT_TIMEOUT_MS, static_cast<long>(timeoutMs));
    curl_easy_setopt(curl, CURLOPT_CONNECTTIMEOUT_MS, 800L);
    curl_easy_setopt(curl, CURLOPT_NOSIGNAL, 1L);
    curl_easy_setopt(curl, CURLOPT_NOPROGRESS, 0L);
    curl_easy_setopt(curl, CURLOPT_XFERINFODATA, const_cast<std::atomic<bool>*>(&stopping));
    curl_easy_setopt(curl, CURLOPT_XFERINFOFUNCTION,
        +[](void* data,curl_off_t,curl_off_t,curl_off_t,curl_off_t)->int {
            return static_cast<std::atomic<bool>*>(data)->load()?1:0;
        });
    const CURLcode rc = curl_easy_perform(curl);
    if (rc == CURLE_OK) curl_easy_getinfo(curl, CURLINFO_RESPONSE_CODE, status);
    else if (rc == CURLE_OPERATION_TIMEDOUT) *status = -1;  // 读超时：调用方不重试（可能已扣配额）
    curl_slist_free_all(headers);
    curl_easy_cleanup(curl);
    return rc == CURLE_OK;
}

}
