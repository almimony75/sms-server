#include "httplib.h"

#include <nlohmann/json.hpp>
#include <fstream>
#include <iostream>
#include <mutex>
#include <vector>
#include <thread>
#include <set>
#include <chrono>
#include <ctime>
#include <atomic>
#include <queue>
#include <condition_variable>
#include <csignal> 

using namespace httplib;
using json = nlohmann::json;

std::string latest_sms = "{}";
std::vector<std::string> sms_log;
std::mutex sms_mutex;

struct SSEClient
{
  DataSink *sink = nullptr;
  std::queue<std::string> message_queue;
  std::mutex queue_mutex;
  std::condition_variable cv;
  std::atomic<bool> connected{true};
  std::string client_id;
};

std::set<std::shared_ptr<SSEClient>> sse_clients;
std::mutex sse_clients_mutex;

// Global atomic flag to signal server shutdown
std::atomic<bool> server_running(true);

// Global pointer to the server instance to allow stopping it from the signal handler
Server *global_svr_ptr = nullptr;

// Signal handler function
void signal_handler(int signo)
{
  if (signo == SIGINT)
  { // Ctrl+C
    std::cout << "\nSIGINT received. Initiating server shutdown...\n";
    server_running = false; // Set the flag to stop content providers

    std::lock_guard<std::mutex> lock(sse_clients_mutex);
    for (auto &client : sse_clients)
    {
      client->connected = false; 
      client->cv.notify_one();   
    }

    if (global_svr_ptr)
    {
      global_svr_ptr->stop(); 
    }
  }
}

void append_to_log_file(const std::string &json_sms)
{
  std::ofstream out("sms_log.jsonl", std::ios::app);
  if (out.is_open())
  {
    out << json_sms << "\n";
  }
}

std::string current_utc_time()
{
  auto now = std::chrono::system_clock::now();
  std::time_t now_time = std::chrono::system_clock::to_time_t(now);
  char buf[30];
  std::strftime(buf, sizeof(buf), "%FT%TZ", std::gmtime(&now_time));
  return std::string(buf);
}

void notify_sse_clients(const std::string &data)
{
  std::lock_guard<std::mutex> lock(sse_clients_mutex);
  for (auto &client : sse_clients)
  {
    if (client->connected.load())
    {
      std::lock_guard<std::mutex> client_queue_lock(client->queue_mutex);
      client->message_queue.push("data: " + data + "\n\n");
      client->cv.notify_one();
    }
  }
}

int main()
{
  Server svr;
  global_svr_ptr = &svr; // Set the global pointer

  // Register the signal handler
  std::signal(SIGINT, signal_handler);

  svr.Post("/sms", [](const Request &req, Response &res)
           {
        try {
            json sms = json::parse(req.body);

            if (!sms.contains("sender") || !sms.contains("message")) {
                res.status = 400;
                res.set_content(R"({"error":"Missing sender or message"})", "application/json");
                return;
            }

            sms["received_at"] = current_utc_time();
            std::string sms_str = sms.dump();

            {
                std::lock_guard<std::mutex> lock(sms_mutex);
                latest_sms = sms_str;
                sms_log.push_back(sms_str);
            }

            append_to_log_file(sms_str);
            notify_sse_clients(sms_str);

            res.set_content(R"({"status":"ok"})", "application/json");
        } catch (const json::parse_error& e) {
            res.status = 400;
            res.set_content(R"({"error":"Invalid JSON: )" + std::string(e.what()) + R"("})", "application/json");
        } catch (...) {
            res.status = 500;
            res.set_content(R"({"error":"Internal server error"})", "application/json");
        } });

  svr.Get("/sms/latest", [](const Request &, Response &res)
          {
        std::lock_guard<std::mutex> lock(sms_mutex);
        res.set_content(latest_sms, "application/json"); });

  svr.Get("/sms/all", [](const Request &, Response &res)
          {
        std::lock_guard<std::mutex> lock(sms_mutex);
        json all = json::array();
        for (const auto& line : sms_log) {
            all.push_back(json::parse(line));
        }
        res.set_content(all.dump(2), "application/json"); });

  svr.Get("/stats", [](const Request &, Response &res)
          {
        std::lock_guard<std::mutex> lock1(sms_mutex);
        std::lock_guard<std::mutex> lock2(sse_clients_mutex);

        json stats = {
            {"total_sms", sms_log.size()},
            {"connected_sse_clients", sse_clients.size()},
            {"uptime", current_utc_time()}
        };
        res.set_content(stats.dump(2), "application/json"); });

  svr.Get("/events", [](const Request &, Response &res)
          {
        res.set_header("Content-Type", "text/event-stream");
        res.set_header("Cache-Control", "no-cache");
        res.set_header("Connection", "keep-alive");

        auto client = std::make_shared<SSEClient>();
        client->client_id = "client_" + std::to_string(reinterpret_cast<uintptr_t>(client.get()));

        {
            std::lock_guard<std::mutex> lock(sse_clients_mutex);
            sse_clients.insert(client);
            std::cout << "New SSE client connected: " << client->client_id << ", total: " << sse_clients.size() << "\n";
        }

        res.set_chunked_content_provider("text/event-stream",
            [client](size_t offset, DataSink& sink) -> bool {
                client->sink = &sink;

                std::unique_lock<std::mutex> lock(client->queue_mutex);

                // Wait for a message, client disconnect, or server shutdown.
                client->cv.wait_for(lock, std::chrono::milliseconds(500), [&]{
                    return !client->message_queue.empty() || !client->connected.load() || !server_running.load();
                });

                if (!client->connected.load() || !server_running.load()) {
                    if (client->message_queue.empty()) {
                        std::cout << "SSE client " << client->client_id << " signaling disconnect (server shutdown or client disconnected).\n";
                        return false;
                    }
                }

                if (!client->message_queue.empty()) {
                    std::string message = client->message_queue.front();
                    client->message_queue.pop();
                    std::cout << "SSE client " << client->client_id << " sending message.\n";
                    return sink.write(message.data(), message.size());
                } else {
                    std::string keepalive = ": keep-alive\n\n";
                    std::cout << "SSE client " << client->client_id << " sending keep-alive.\n";
                    return sink.write(keepalive.data(), keepalive.size());
                }
            },
            [client](bool success) {
                std::lock_guard<std::mutex> lock(sse_clients_mutex);
                sse_clients.erase(client);
                client->connected = false;
                client->cv.notify_one();
                std::cout << "SSE client " << client->client_id << " disconnected. Total: " << sse_clients.size() << "\n";
            }); });

  std::cout << "📡 Server with SSE running on http://0.0.0.0:8081\n";
  svr.listen("0.0.0.0", 8081);

  std::cout << "Server gracefully stopped.\n";
  return 0;
}