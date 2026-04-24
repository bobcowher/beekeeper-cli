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

    json post(const std::string& path, const json& body = json::object(), bool slow_ok = false) {
        httplib::Client cli(base_url_, port_);
        httplib::Headers headers = { {"X-API-Key", api_key_} };
        auto res = cli.Post(path.c_str(), headers, body.dump(), "application/json");
        return handle_response(res, slow_ok);
    }

private:
    std::string host_;
    std::string api_key_;
    std::string base_url_;
    int port_;

    json handle_response(httplib::Result& res, bool slow_ok = false) {
        if (!res) {
            auto err = res.error();
            if (slow_ok && (err == httplib::Error::Read || err == httplib::Error::Write ||
                            err == httplib::Error::ConnectionTimeout)) {
                return {{"success", false}, {"error", {{"message", "no_response"}}}};
            }
            return {{"success", false}, {"error", {{"message", "Server unreachable at " + host_}}}};
        }
        if (res->status != 200 && res->status != 201 && res->status != 202) {
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
                auto res = client.post("/api/v1/projects/" + name + "/training/start", json::object(), true);
                if (res["success"]) {
                    std::cout << "Training is starting. Pre-launch sequence (git sync, pip install) running in background.\n"
                              << "Check status with: beekeeper training status " << name << "\n";
                } else {
                    std::string msg = res.contains("error") ? res["error"]["message"].get<std::string>() : "Unknown error";
                    if (msg == "no_response") {
                        std::cerr << "Warning: No response from server — the pre-launch sequence may still be running.\n"
                                  << "Check status with: beekeeper training status " << name << "\n";
                        return 1;
                    }
                    std::cerr << "Error: " << msg << "\n";
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
                    std::string trend = m["trend"].get<std::string>();
                    std::string recent_trend = m.value("recent_trend", trend);
                    double improvement = m["improvement_percent"].get<double>();
                    double best_val = m["best_value"].get<double>();
                    double peak_val = m.value("peak_value", best_val);
                    int peak_step = m.value("peak_step", 0);
                    double peak_reversal = m.value("peak_reversal_pct", 0.0);

                    std::cout << "- " << std::left << std::setw(20) << el.key() << ": "
                              << trend << " ("
                              << std::fixed << std::setprecision(1) << improvement << "% | "
                              << "Best: " << std::setprecision(4) << best_val << ")\n";
                    if (recent_trend != trend) {
                        std::cout << "  Recent trend: " << recent_trend << "\n";
                    }
                    if (peak_reversal > 0.01) {
                        std::cout << "  Peak: " << std::setprecision(4) << peak_val
                                  << " at step " << peak_step
                                  << " | Reversal: " << std::setprecision(1) << peak_reversal << "%\n";
                    }
                    if (m["converged"].get<bool>()) {
                        std::cout << "  [CONVERGED at step " << m["convergence_step"] << "]\n";
                    }
                }
            } else {
                std::cout << "\nTensorBoard Analysis: " << (tb_res.contains("error") ? tb_res["error"]["message"].get<std::string>() : "No data") << "\n";
            }

            if (log_res["success"]) {
                auto& d = log_res["data"];
                int ep_start = d["episode_range"][0].get<int>();
                int ep_end   = d["episode_range"][1].get<int>();
                int total    = d["total_episodes"].get<int>();
                std::string trend = d.value("trend", "unknown");

                std::cout << "\nEpisode Analysis (Logs):\n";
                std::cout << "  Episodes: " << ep_start << "-" << ep_end
                          << " (" << total << " total) | Trend: " << trend << "\n";

                auto& ov = d["overall"];
                std::cout << std::fixed << std::setprecision(2);
                std::cout << "  Overall:  avg " << ov["avg_reward"].get<double>()
                          << " | max " << ov["max_reward"].get<double>()
                          << " | min " << ov["min_reward"].get<double>() << "\n";

                if (d.contains("recent")) {
                    auto& r = d["recent"];
                    std::cout << "  Recent (last " << r["count"].get<int>() << "): "
                              << "avg " << r["avg_reward"].get<double>()
                              << " | max " << r["max_reward"].get<double>()
                              << " | min " << r["min_reward"].get<double>() << "\n";
                }

                if (d.contains("quartiles") && d["quartiles"].is_array()) {
                    std::cout << "\n  Quartile progression:\n";
                    for (auto& q : d["quartiles"]) {
                        int qn = q["quartile"].get<int>();
                        int qs = q["episode_range"][0].get<int>();
                        int qe = q["episode_range"][1].get<int>();
                        std::cout << "  Q" << qn << " (ep " << qs << "-" << qe << "): "
                                  << "avg " << q["avg_reward"].get<double>()
                                  << " | max " << q["max_reward"].get<double>()
                                  << " | min " << q["min_reward"].get<double>() << "\n";
                    }
                }

                if (d.contains("latest_episode")) {
                    auto& le = d["latest_episode"];
                    std::cout << "  Latest ep " << le["episode"].get<int>()
                              << ": reward " << le["reward"].get<double>()
                              << " | steps " << le["steps"].get<int>()
                              << " | epsilon " << le["epsilon"].get<double>() << "\n";
                }
            } else {
                std::cout << "\nEpisode Analysis (Logs): "
                          << (log_res.contains("error") ? log_res["error"]["message"].get<std::string>() : "No data") << "\n";
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
