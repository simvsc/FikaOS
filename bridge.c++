#include <iostream>
#include <fstream>
#include <string>
#include <vector>
#include <sstream>
#include <chrono>
#include <thread>
#include <set>
#include <algorithm>
#include <curl/curl.h>
#include <nlohmann/json.hpp>

using json = nlohmann::json;

/* --- LOGGING SUBSYSTEM --- */
class AmericanoLogger {
public:
    static void log(const std::string& level, const std::string& message) {
        auto now = std::chrono::system_clock::to_time_t(std::chrono::system_clock::now());
        std::cout << "[" << level << "] " << std::put_time(std::localtime(&now), "%H:%M:%S") 
                  << " - " << message << std::endl;
    }
};

/* --- GEMINI API INTERFACE --- */
class GeminiIntelligence {
    std::string api_key;
    std::string api_url;

    struct CurlResponse {
        std::string body;
        long http_code;
    };

    static size_t WriteCallback(void* contents, size_t size, size_t nmemb, std::string* s) {
        size_t total_size = size * nmemb;
        s->append((char*)contents, total_size);
        return total_size;
    }

public:
    GeminiIntelligence(const std::string& key) : api_key(key) {
        api_url = "https://generativelanguage.googleapis.com/v1beta/models/gemini-2.0-flash:generateContent?key=" + api_key;
    }
//------------------------------------------------------------------------------------------------
    std::vector<int> fetch_scheduling_policy(const std::string& kernel_state) {
        CURL* curl = curl_easy_init();
        std::string raw_response;
        std::vector<int> target_pids;

        if (!curl) return target_pids;

        json payload = {
            {"contents", {{
                {"parts", {{
                    {"text", "You are an AI kernel scheduler. Analyze this JSON process list. "
                             "Identify processes that are resource-intensive but non-critical. "
                             "Return ONLY a JSON object with a key 'pause' containing an array of PIDs. "
                             "System State: " + kernel_state}
                }}}
            }}}
        };

        std::string json_str = payload.dump();
        struct curl_slist* headers = curl_slist_append(NULL, "Content-Type: application/json");

        curl_easy_setopt(curl, CURLOPT_URL, api_url.c_str());
        curl_easy_setopt(curl, CURLOPT_POSTFIELDS, json_str.c_str());
        curl_easy_setopt(curl, CURLOPT_HTTPHEADER, headers);
        curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, WriteCallback);
        curl_easy_setopt(curl, CURLOPT_WRITEDATA, &raw_response);
        curl_easy_setopt(curl, CURLOPT_TIMEOUT, 10L); // Safety timeout

        CURLcode res = curl_easy_perform(curl);
        
        if (res == CURLE_OK) {
            try {
                auto parsed = json::parse(raw_response);
                std::string ai_text = parsed["candidates"][0]["content"]["parts"][0]["text"];
                // Some AI models wrap JSON in triple backticks
                if (ai_text.find("```json") != std::string::npos) {
                    ai_text = ai_text.substr(7, ai_text.length() - 10);
                }
                auto decision = json::parse(ai_text);
                target_pids = decision["pause"].get<std::vector<int>>();
            } catch (...) {
                AmericanoLogger::log("ERROR", "Failed to parse AI response payload.");
            }
        }
        
        curl_easy_cleanup(curl);
        return target_pids;
    }
};
//------------------------------------------------------------------------------------------------

/* --- SYSTEM ENFORCEMENT AGENT --- */
class AmericanonpauserBridge {
    GeminiIntelligence& brain;
    std::set<std::string> critical_processes = {"systemd", "Xorg", "Americanonpauser", "sshd", "kthreadd"};

public:
    AmericanonpauserBridge(GeminiIntelligence& intel) : brain(intel) {}

    void execute_loop() {
        AmericanoLogger::log("INIT", "Orchestrator Bridge operational.");
        
        while (true) {
            std::string state = pull_telemetry("/proc/Americanonpauser/state.Americano");
            
            if (state.length() < 10) {
                AmericanoLogger::log("WARN", "Waiting for Kernel Telemetry Bridge...");
                std::this_thread::sleep_for(std::chrono::seconds(3));
                continue;
            }

            AmericanoLogger::log("AI", "Transmitting state to Gemini for inference...");
            std::vector<int> pause_list = brain.fetch_scheduling_policy(state);

            if (!pause_list.empty()) {
                AmericanoLogger::log("ACTION", "Synchronizing AI Verdict to /proc/Americanonpauser/pause.Americano");
                commit_to_kernel(pause_list);
            }

            std::this_thread::sleep_for(std::chrono::milliseconds(2500));
        }
    }

private:
    std::string pull_telemetry(const std::string& path) {
        std::ifstream f(path);
        if (!f.is_open()) return "";
        return std::string((std::istreambuf_iterator<char>(f)), std::istreambuf_iterator<char>());
    }

    void commit_to_kernel(const std::vector<int>& pids) {
        std::ofstream pause_hook("/proc/Americanonpauser/pause.Americano");
        if (!pause_hook.is_open()) {
            AmericanoLogger::log("ERROR", "Write access denied to Kernel Instruction Hook.");
            return;
        }

        for (size_t i = 0; i < pids.size(); ++i) {
            pause_hook << pids[i] << (i == pids.size() - 1 ? "" : ",");
        }
        pause_hook.close();
    }
};

int main(int argc, char** argv) {
    if (argc < 2) {
        std::cerr << "Usage: sudo ./Americano <GEMINI_API_KEY>" << std::endl;
        return 1;
    }

    curl_global_init(CURL_GLOBAL_ALL);
    
    GeminiIntelligence ai(argv[1]);
    AmericanonpauserBridge bridge(ai);
    
    try {
        bridge.execute_loop();
    } catch (const std::exception& e) {
        AmericanoLogger::log("FATAL", e.what());
    }

    curl_global_cleanup();
    return 0;
}
