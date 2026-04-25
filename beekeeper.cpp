#include <iostream>
#include <string>
#include <vector>
#include <map>
#include <cstdlib>
#include <iomanip>
#include <thread>
#include <chrono>
#include "version.h"
#include "include/json.hpp"
#include "include/httplib.h"

using json = nlohmann::json;

class BeekeeperClient {
public:
    BeekeeperClient(const std::string& host, const std::string& api_key)
        : host_(host), api_key_(api_key) {
        if (!host_.empty() && host_.back() == '/') {
            host_.pop_back();
        }
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
        httplib::Headers headers = {{"Authorization", "Bearer " + api_key_}};
        auto res = cli.Get(path.c_str(), headers);
        return handle_response(res);
    }

    json post(const std::string& path, const json& body = json::object(), bool slow_ok = false) {
        httplib::Client cli(base_url_, port_);
        httplib::Headers headers = {{"Authorization", "Bearer " + api_key_}};
        auto res = cli.Post(path.c_str(), headers, body.dump(), "application/json");
        return handle_response(res, slow_ok);
    }

    json del(const std::string& path) {
        httplib::Client cli(base_url_, port_);
        httplib::Headers headers = {{"Authorization", "Bearer " + api_key_}};
        auto res = cli.Delete(path.c_str(), headers);
        return handle_response(res);
    }

    // Returns raw response body (for markdown/text endpoints)
    std::string raw_get(const std::string& path) {
        httplib::Client cli(base_url_, port_);
        httplib::Headers headers = {{"Authorization", "Bearer " + api_key_}};
        auto res = cli.Get(path.c_str(), headers);
        if (!res) return "";
        if (res->status != 200) return "";
        return res->body;
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

void print_version() {
    std::cout << "beekeeper " << BEEKEEPER_VERSION << "\n";
}

void print_usage() {
    std::cout << "Beekeeper CLI " << BEEKEEPER_VERSION << " - Agent-optimized training management\n\n"
              << "Usage: beekeeper <command> [subcommand] [args]\n\n"
              << "Commands:\n"
              << "  projects list                         List all projects and status\n"
              << "  projects info <name>                  Show detailed project info\n"
              << "  projects create <name> <git_url>      Create a new project (see options below)\n"
              << "                [branch] [python]       Optional: branch (default: main)\n"
              << "                [train_file] [env_type] Optional: train file, env type (venv|conda)\n"
              << "  projects instructions <name>          Fetch project-specific agent instructions\n"
              << "  projects retry <name>                 Retry failed project setup (polls until done)\n"
              << "  projects delete <name>                Delete a project and all its data\n"
              << "  training start <name>                 Start training\n"
              << "  training stop <name>                  Stop training\n"
              << "  training status <name>                Get current training status\n"
              << "  logs get <name> [tail]                Get training logs (default: last 100 lines)\n"
              << "  run analyze <name>                    Synthesized metrics + trend analysis\n"
              << "  branch list <name>                    List available branches for a project\n"
              << "  branch switch <name> <branch>         Switch to a different branch\n"
              << "  stats                                 System stats (GPU, CPU, RAM)\n"
              << "  busy                                  Check if any training is currently running\n"
              << "\nEnvironment Variables:\n"
              << "  BEEKEEPER_HOST             Server URL (default: http://localhost:5000)\n"
              << "  BEEKEEPER_API_KEY          Your API key (required when auth is enabled)\n";
}

static std::string error_msg(const json& res) {
    if (res.contains("error") && res["error"].contains("message"))
        return res["error"]["message"].get<std::string>();
    return "Unknown error";
}

int main(int argc, char* argv[]) {
    if (argc < 2) {
        print_usage();
        return 1;
    }

    std::string first_arg = argv[1];
    if (first_arg == "--version" || first_arg == "-v") {
        print_version();
        return 0;
    }
    if (first_arg == "--help" || first_arg == "-h") {
        print_usage();
        return 0;
    }

    const char* host_env = std::getenv("BEEKEEPER_HOST");
    const char* api_key_env = std::getenv("BEEKEEPER_API_KEY");

    std::string host = host_env ? host_env : "http://localhost:5000";
    std::string api_key = api_key_env ? api_key_env : "";

    BeekeeperClient client(host, api_key);
    std::string cmd = argv[1];

    try {
        // --- projects ---
        if (cmd == "projects") {
            if (argc < 3) { print_usage(); return 1; }
            std::string sub = argv[2];

            if (sub == "list") {
                auto res = client.get("/api/v1/projects");
                if (!res["success"]) { std::cerr << "Error: " << error_msg(res) << "\n"; return 1; }
                for (auto& p : res["data"]["projects"]) {
                    std::string indicator = (p["train_status"].get<std::string>() == "running") ? "▶" : " ";
                    std::cout << indicator << " " << std::left << std::setw(30) << p["name"].get<std::string>()
                              << " setup=" << std::setw(8) << p["setup_status"].get<std::string>()
                              << " train=" << p["train_status"].get<std::string>() << "\n";
                }

            } else if (sub == "info" && argc >= 4) {
                std::string name = argv[3];
                auto res = client.get("/api/v1/projects/" + name);
                if (!res["success"]) { std::cerr << "Error: " << error_msg(res) << "\n"; return 1; }
                auto& p = res["data"]["project"];
                std::cout << "Project:  " << p["name"].get<std::string>() << "\n"
                          << "Repo:     " << p["git_url"].get<std::string>() << " (" << p["branch"].get<std::string>() << ")\n"
                          << "Setup:    " << p["setup_status"].get<std::string>() << "\n"
                          << "Training: " << p["train_status"].get<std::string>() << "\n"
                          << "Python:   " << p["python_version"].get<std::string>() << "\n"
                          << "Env:      " << p["env_type"].get<std::string>() << "\n"
                          << "Train file: " << p["train_file"].get<std::string>() << "\n";

            } else if (sub == "create" && argc >= 5) {
                std::string name     = argv[3];
                std::string git_url  = argv[4];
                std::string branch   = (argc >= 6) ? argv[5] : "main";
                std::string python   = (argc >= 7) ? argv[6] : "3.12";
                std::string train_f  = (argc >= 8) ? argv[7] : "train.py";
                std::string env_type = (argc >= 9) ? argv[8] : "venv";

                json body = {
                    {"name",           name},
                    {"git_url",        git_url},
                    {"branch",         branch},
                    {"python_version", python},
                    {"train_file",     train_f},
                    {"env_type",       env_type}
                };
                auto res = client.post("/api/v1/projects", body);
                if (!res["success"]) { std::cerr << "Error: " << error_msg(res) << "\n"; return 1; }
                std::cout << "Project '" << name << "' created. Setup is running in background.\n"
                          << "Poll status with: beekeeper projects info " << name << "\n"
                          << "Or wait for setup with: beekeeper projects retry " << name << "\n";

            } else if (sub == "instructions" && argc >= 4) {
                std::string name = argv[3];
                std::string body = client.raw_get("/api/v1/projects/" + name + "/agent/instructions");
                if (body.empty()) {
                    std::cerr << "Error: Failed to fetch instructions for '" << name << "'\n";
                    return 1;
                }
                std::cout << body << "\n";

            } else if (sub == "retry" && argc >= 4) {
                std::string name = argv[3];
                auto res = client.post("/api/v1/projects/" + name + "/setup/retry");
                if (!res["success"]) { std::cerr << "Error: " << error_msg(res) << "\n"; return 1; }
                std::cout << "Setup retry started. Waiting for completion...\n";
                for (int i = 0; i < 120; ++i) {
                    std::this_thread::sleep_for(std::chrono::seconds(5));
                    auto poll = client.get("/api/v1/projects/" + name);
                    if (!poll["success"]) continue;
                    std::string setup = poll["data"]["project"]["setup_status"].get<std::string>();
                    std::cout << "  setup_status: " << setup << "\n";
                    if (setup == "ready") { std::cout << "Setup complete.\n"; return 0; }
                    if (setup == "error") {
                        std::cerr << "Setup failed: " << poll["data"]["project"].value("setup_error", "unknown") << "\n";
                        return 1;
                    }
                }
                std::cerr << "Timed out waiting for setup.\n";
                return 1;

            } else if (sub == "delete" && argc >= 4) {
                std::string name = argv[3];
                auto res = client.del("/api/v1/projects/" + name);
                if (!res["success"]) { std::cerr << "Error: " << error_msg(res) << "\n"; return 1; }
                std::cout << "Project '" << name << "' deleted.\n";

            } else {
                print_usage(); return 1;
            }
        }

        // --- training ---
        else if (cmd == "training") {
            if (argc < 4) { print_usage(); return 1; }
            std::string sub  = argv[2];
            std::string name = argv[3];

            if (sub == "start") {
                auto res = client.post("/api/v1/projects/" + name + "/training/start", json::object(), true);
                if (res["success"]) {
                    std::cout << "Training is starting. Pre-launch sequence (git sync, pip install) running in background.\n"
                              << "Check status with: beekeeper training status " << name << "\n";
                } else {
                    std::string msg = error_msg(res);
                    if (msg == "no_response") {
                        std::cout << "Warning: No response from server — pre-launch sequence is likely still running.\n"
                                  << "Check status with: beekeeper training status " << name << "\n";
                        return 0;
                    }
                    std::cerr << "Error: " << msg << "\n"; return 1;
                }
            } else if (sub == "stop") {
                auto res = client.post("/api/v1/projects/" + name + "/training/stop");
                if (!res["success"]) { std::cerr << "Error: " << error_msg(res) << "\n"; return 1; }
                std::cout << "Training stopped.\n";
            } else if (sub == "status") {
                auto res = client.get("/api/v1/projects/" + name + "/training/status");
                if (!res["success"]) { std::cerr << "Error: " << error_msg(res) << "\n"; return 1; }
                std::cout << "Status: " << res["data"]["status"].get<std::string>() << "\n";
            } else {
                print_usage(); return 1;
            }
        }

        // --- logs ---
        else if (cmd == "logs") {
            if (argc < 4) { print_usage(); return 1; }
            std::string name = argv[3];
            std::string tail = (argc >= 5) ? argv[4] : "100";
            auto res = client.get("/api/v1/projects/" + name + "/logs?tail=" + tail);
            if (!res["success"]) { std::cerr << "Error: " << error_msg(res) << "\n"; return 1; }
            std::cout << res["data"]["content"].get<std::string>() << "\n";
        }

        // --- branch ---
        else if (cmd == "branch") {
            if (argc < 4) { print_usage(); return 1; }
            std::string sub  = argv[2];
            std::string name = argv[3];

            if (sub == "list") {
                auto res = client.get("/api/v1/projects/" + name + "/branches");
                if (!res["success"]) { std::cerr << "Error: " << error_msg(res) << "\n"; return 1; }
                std::string current = res["data"]["current"].get<std::string>();
                for (auto& b : res["data"]["branches"]) {
                    std::string bname = b.get<std::string>();
                    std::cout << (bname == current ? "* " : "  ") << bname << "\n";
                }
            } else if (sub == "switch" && argc >= 5) {
                std::string branch = argv[4];
                json body = {{"branch", branch}};
                auto res = client.post("/api/v1/projects/" + name + "/branch", body);
                if (!res["success"]) { std::cerr << "Error: " << error_msg(res) << "\n"; return 1; }
                std::cout << "Switched to branch '" << branch << "'.\n";
            } else {
                print_usage(); return 1;
            }
        }

        // --- stats ---
        else if (cmd == "stats") {
            auto res = client.get("/api/v1/stats");
            if (!res["success"]) { std::cerr << "Error: " << error_msg(res) << "\n"; return 1; }
            auto& d = res["data"];
            // CPU
            auto& cpu = d["cpu"];
            std::cout << "CPU:    " << cpu["percent"].get<double>() << "% utilization"
                      << " (" << cpu["count"].get<int>() << " cores)\n";
            // RAM
            auto& mem = d["memory"];
            std::cout << "RAM:    " << mem["used_gb"].get<double>() << " GB / "
                      << mem["total_gb"].get<double>() << " GB"
                      << " (" << mem["percent"].get<double>() << "%)\n";
            // GPU
            if (d.contains("gpus") && d["gpus"].is_array() && !d["gpus"].empty()) {
                for (auto& g : d["gpus"]) {
                    std::cout << "GPU " << g["index"].get<int>() << ": " << g["name"].get<std::string>() << "\n"
                              << "  Util:  " << g["gpu_util"].get<int>() << "%\n"
                              << "  VRAM:  " << g["mem_used_h"].get<std::string>()
                              << " / " << g["mem_total_h"].get<std::string>()
                              << " (" << std::fixed << std::setprecision(1) << g["mem_percent"].get<double>() << "%)\n"
                              << "  Temp:  " << g["temp"].get<int>() << "°C\n"
                              << "  Power: " << g["power"].get<double>() << " W\n";
                }
            } else {
                std::cout << "GPU:    none detected\n";
            }
        }

        // --- busy ---
        else if (cmd == "busy") {
            auto res = client.get("/api/v1/busy");
            if (!res["success"]) { std::cerr << "Error: " << error_msg(res) << "\n"; return 1; }
            bool busy = res["data"]["busy"].get<bool>();
            if (busy) {
                std::cout << "BUSY — training is running\n";
                if (res["data"].contains("running_projects")) {
                    for (auto& p : res["data"]["running_projects"]) {
                        std::cout << "  - " << p.get<std::string>() << "\n";
                    }
                }
                return 1;  // non-zero exit so scripts can detect busy state
            } else {
                std::cout << "IDLE — no training running\n";
                return 0;
            }
        }

        // --- run analyze ---
        else if (cmd == "run" && argc >= 4 && std::string(argv[2]) == "analyze") {
            std::string name = argv[3];

            auto tb_res  = client.get("/api/v1/projects/" + name + "/tensorboard/latest");
            auto log_res = client.get("/api/v1/projects/" + name + "/logs/analysis");

            std::cout << "Project: " << name << "\n";

            if (tb_res["success"]) {
                std::cout << "\nMetrics Analysis (TensorBoard):\n";
                auto& metrics = tb_res["data"]["metrics"];
                for (auto& el : metrics.items()) {
                    auto& m = el.value();
                    std::string trend        = m["trend"].get<std::string>();
                    std::string recent_trend = m.value("recent_trend", trend);
                    double improvement       = m["improvement_percent"].get<double>();
                    double best_val          = m["best_value"].get<double>();
                    double peak_val          = m.value("peak_value", best_val);
                    int    peak_step         = m.value("peak_step", 0);
                    double peak_reversal     = m.value("peak_reversal_pct", 0.0);

                    std::cout << "- " << std::left << std::setw(20) << el.key() << ": "
                              << trend << " ("
                              << std::fixed << std::setprecision(1) << improvement << "% | "
                              << "Best: " << std::setprecision(4) << best_val << ")\n";
                    if (recent_trend != trend)
                        std::cout << "  Recent trend: " << recent_trend << "\n";
                    if (peak_reversal > 0.01)
                        std::cout << "  Peak: " << std::setprecision(4) << peak_val
                                  << " at step " << peak_step
                                  << " | Reversal: " << std::setprecision(1) << peak_reversal << "%\n";
                    if (m["converged"].get<bool>())
                        std::cout << "  [CONVERGED at step " << m["convergence_step"] << "]\n";
                }
            } else {
                std::cout << "\nTensorBoard Analysis: " << error_msg(tb_res) << "\n";
            }

            if (log_res["success"]) {
                auto& d       = log_res["data"];
                int ep_start  = d["episode_range"][0].get<int>();
                int ep_end    = d["episode_range"][1].get<int>();
                int total     = d["total_episodes"].get<int>();
                std::string trend = d.value("trend", "unknown");

                std::cout << "\nEpisode Analysis (Logs):\n"
                          << "  Episodes: " << ep_start << "-" << ep_end
                          << " (" << total << " total) | Trend: " << trend << "\n";

                auto& ov = d["overall"];
                std::cout << std::fixed << std::setprecision(2)
                          << "  Overall:  avg " << ov["avg_reward"].get<double>()
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
                        std::cout << "  Q" << q["quartile"].get<int>()
                                  << " (ep " << q["episode_range"][0].get<int>()
                                  << "-" << q["episode_range"][1].get<int>() << "): "
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
                std::cout << "\nEpisode Analysis (Logs): " << error_msg(log_res) << "\n";
            }
        }

        else {
            print_usage(); return 1;
        }

    } catch (const std::exception& e) {
        std::cerr << "Fatal Error: " << e.what() << "\n";
        return 1;
    }

    return 0;
}
