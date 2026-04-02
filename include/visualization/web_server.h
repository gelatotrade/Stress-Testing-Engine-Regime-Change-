#pragma once
#include "core/types.h"
#include <thread>
#include <atomic>

namespace ste {

class WebServer {
public:
    explicit WebServer(int port = 8080);
    ~WebServer();

    // Start/stop the server
    void start();
    void stop();
    bool isRunning() const { return running_.load(); }

    // Push new frame to connected clients
    void broadcastFrame(const VisualizationFrame& frame);
    void broadcastJson(const std::string& json);

    int port() const { return port_; }

private:
    int port_;
    int server_fd_ = -1;
    std::atomic<bool> running_{false};
    std::thread server_thread_;
    std::vector<int> client_fds_;
    std::mutex clients_mutex_;
    std::string last_frame_json_;

    void serverLoop();
    void handleClient(int client_fd);
    void sendResponse(int client_fd, const std::string& status,
                      const std::string& content_type, const std::string& body);
    void sendSSE(int client_fd, const std::string& data);

    // Serve static files
    std::string getStaticFile(const std::string& path);
    std::string getMimeType(const std::string& path);

    // Generate embedded HTML/JS/CSS
    static std::string getIndexHtml();
    static std::string getMainJs();
    static std::string getStyleCss();
};

} // namespace ste
