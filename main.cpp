#include <iostream>
#include <string>
#include <vector>
#include <map>
#include <cstdlib>
#include <iomanip>
#include "include/json.hpp"
#include "include/httplib.h"

using json = nlohmann::json;

class BeekeeperClient {
public:
    BeekeeperClient(const std::string& host, const std::string& api_key) 
        : host_(host), api_key_(api_key) {
        // Remove trailing slash from host if present
        if (!host_.empty() && host_.back() == '/') {
            host_.pop_back();
        }
        
        // Extract base host and port
        size_t proto_end = host_.find("://");
        std::string server = (proto_end == std::string::npos) ? host_ : host_.substr(proto_end + 3);
        
        size_t port_pos = server.find(':');
        if (port_pos != std::string::npos) {
            base_url_ = server.substr(0, port_pos);
            port_ = std::stoi(server.substr(port_pos + 1));
        } else {
            base_url_ = server;
            port_ = 80;
        }
    }

    json get(const std::string& path) {
        httplib::Client cli(base_url_, port_);
        httplib::Headers headers = { {"X-API-Key", api_key_} };
        auto res = cli.Get(path.c_str(), headers);
        return handle_response(res);
    }

    json post(const std::string& path, const json& body = json::object()) {
        httplib::Client cli(base_url_, port_);
        httplib::Headers headers = { {"X-API-Key", api_key_} };
        auto res = cli.Post(path.c_str(), headers, body.dump(), "application/json");
        return handle_response(res);
    }

private:
    std::string host_;
    std::string api_key_;
    std::string base_url_;
    int port_;

    json handle_response(httplib::Result& res) {
        if (!res) {
            return {{"success", false}, {"error", {{"message", "Connection failed"}}}};
        }
        if (res->status != 200 && res->status != 201) {
            try {
                return json::parse(res->body);
            } catch (...) {
                return {{"success", false}, {"error", {{"message", "HTTP " + std::to_string(res->status)}}}};
            }
        }
        return json::parse(res->body);
    }
};

void print_usage() {
    std::cout << "Beekeeper CLI - Agent-optimized training management\n\n"
              << "Usage: beekeeper <command> [subcommand] [args]\n\n"
              << "Commands:\n"
              << "  projects list              List all projects\n"
              << "  projects info <name>       Show detailed project info\n"
              << "  training start <name>      Start training for a project\n"
              << "  training stop <name>       Stop training for a project\n"
              << "  training status <name>     Get current training status\n"
              << "  logs get <name> [tail]     Get training logs\n"
              << "  run analyze <name>         Get synthesized metrics analysis\n"
              << "\nEnvironment Variables:\n"
              << "  BEEKEEPER_HOST             Server URL (default: http://localhost:5000)\n"
              << "  BEEKEEPER_API_KEY          Your API key\n";
}

int main(int argc, char* argv[]) {
    if (argc < 2) {
        print_usage();
        return 1;
    }

    const char* host_env = std::getenv("BEEKEEPER_HOST");
    const char* api_key_env = std::getenv("BEEKEEPER_API_KEY");

    std::string host = host_env ? host_env : "http://localhost:5000";
    std::string api_key = api_key_env ? api_key_env : "";

    BeekeeperClient client(host, api_key);
    std::string cmd = argv[1];

    try {
        if (cmd == "projects") {
            if (argc < 3) { print_usage(); return 1; }
            std::string sub = argv[2];
            if (sub == "list") {
                auto res = client.get("/api/v1/projects");
                if (res["success"]) {
                    for (auto& p : res["data"]["projects"]) {
                        std::cout << "- " << std::left << std::setw(20) << p["name"].get<std::string>()
                                  << " Status: " << p["train_status"].get<std::string>() << "\n";
                    }
                } else {
                    std::cerr << "Error: " << (res.contains("error") ? res["error"]["message"].get<std::string>() : "Unknown error") << "\n";
                    return 1;
                }
            } else if (sub == "info" && argc >= 4) {
                std::string name = argv[3];
                auto res = client.get("/api/v1/projects/" + name);
                if (res["success"]) {
                    auto& p = res["data"]["project"];
                    std::cout << "Project: " << p["name"].get<std::string>() << "\n"
                              << "Repo:    " << p["git_url"].get<std::string>() << " (" << p["branch"].get<std::string>() << ")\n"
                              << "Status:  " << p["train_status"].get<std::string>() << "\n"
                              << "Setup:   " << p["setup_status"].get<std::string>() << "\n";
                } else {
                    std::cerr << "Error: " << (res.contains("error") ? res["error"]["message"].get<std::string>() : "Unknown error") << "\n";
                    return 1;
                }
            }
        } 
        else if (cmd == "training") {
            if (argc < 4) { print_usage(); return 1; }
            std::string sub = argv[2];
            std::string name = argv[3];
            if (sub == "start") {
                auto res = client.post("/api/v1/projects/" + name + "/training/start");
                if (res["success"]) {
                    std::cout << "Training started. PID: " << res["data"]["pid"] << "\n";
                } else {
                    std::cerr << "Error: " << (res.contains("error") ? res["error"]["message"].get<std::string>() : "Unknown error") << "\n";
                    return 1;
                }
            } else if (sub == "stop") {
                auto res = client.post("/api/v1/projects/" + name + "/training/stop");
                if (res["success"]) {
                    std::cout << "Training stopped.\n";
                } else {
                    std::cerr << "Error: " << (res.contains("error") ? res["error"]["message"].get<std::string>() : "Unknown error") << "\n";
                    return 1;
                }
            } else if (sub == "status") {
                auto res = client.get("/api/v1/projects/" + name + "/training/status");
                if (res["success"]) {
                    std::cout << "Status: " << res["data"]["status"].get<std::string>() << "\n";
                } else {
                    std::cerr << "Error: " << (res.contains("error") ? res["error"]["message"].get<std::string>() : "Unknown error") << "\n";
                    return 1;
                }
            }
        }
        else if (cmd == "logs") {
            if (argc < 4) { print_usage(); return 1; }
            std::string name = argv[3];
            std::string tail = (argc >= 5) ? argv[4] : "50";
            auto res = client.get("/api/v1/projects/" + name + "/logs?tail=" + tail);
            if (res["success"]) {
                std::cout << res["data"]["content"].get<std::string>() << "\n";
            } else {
                std::cerr << "Error: " << (res.contains("error") ? res["error"]["message"].get<std::string>() : "Unknown error") << "\n";
                return 1;
            }
        }
        else if (cmd == "run" && argc >= 4 && std::string(argv[2]) == "analyze") {
            std::string name = argv[3];
            
            // Fetch TensorBoard latest metrics
            auto tb_res = client.get("/api/v1/projects/" + name + "/tensorboard/latest");
            // Fetch Log analysis
            auto log_res = client.get("/api/v1/projects/" + name + "/logs/analysis");
            
            std::cout << "Project: " << name << "\n";
            
            if (tb_res["success"]) {
                std::cout << "\nMetrics Analysis (TensorBoard):\n";
                auto& metrics = tb_res["data"]["metrics"];
                for (auto& el : metrics.items()) {
                    auto& m = el.value();
                    std::cout << "- " << std::left << std::setw(15) << el.key() << ": "
                              << m["trend"].get<std::string>() << " (" 
                              << std::fixed << std::setprecision(1) << m["improvement_percent"].get<double>() << "% | "
                              << "Best: " << m["best_value"].get<double>() << ")\n";
                    if (m["converged"].get<bool>()) {
                        std::cout << "  [CONVERGED at step " << m["convergence_step"] << "]\n";
                    }
                }
            } else {
                std::cout << "\nTensorBoard Analysis: " << (tb_res.contains("error") ? tb_res["error"]["message"].get<std::string>() : "No data") << "\n";
            }

            if (log_res["success"]) {
                std::cout << "\nEpisode Analysis (Logs):\n";
                auto& trends = log_res["data"]["trends"];
                for (auto& el : trends.items()) {
                    auto& t = el.value();
                    std::cout << "- " << std::left << std::setw(15) << el.key() << ": "
                              << t["direction"].get<std::string>() << " (Avg: " 
                              << std::fixed << std::setprecision(2) << t["current_avg"].get<double>() << ")\n";
                }
            }
        }
        else {
            print_usage();
            return 1;
        }
    } catch (const std::exception& e) {
        std::cerr << "Fatal Error: " << e.what() << "\n";
        return 1;
    }

    return 0;
}
