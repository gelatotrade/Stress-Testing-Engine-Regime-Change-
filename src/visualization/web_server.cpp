#include "visualization/web_server.h"
#include <sys/socket.h>
#include <netinet/in.h>
#include <unistd.h>
#include <cstring>
#include <sstream>
#include <iostream>
#include <algorithm>
#include <fcntl.h>
#include <poll.h>

namespace ste {

WebServer::WebServer(int port) : port_(port) {}

WebServer::~WebServer() {
    stop();
}

void WebServer::start() {
    if (running_.load()) return;
    running_ = true;
    server_thread_ = std::thread(&WebServer::serverLoop, this);
}

void WebServer::stop() {
    running_ = false;
    if (server_fd_ >= 0) {
        ::close(server_fd_);
        server_fd_ = -1;
    }
    if (server_thread_.joinable())
        server_thread_.join();
    std::lock_guard<std::mutex> lock(clients_mutex_);
    for (int fd : client_fds_) ::close(fd);
    client_fds_.clear();
}

void WebServer::serverLoop() {
    server_fd_ = ::socket(AF_INET, SOCK_STREAM, 0);
    if (server_fd_ < 0) {
        std::cerr << "Failed to create socket\n";
        return;
    }

    int opt = 1;
    setsockopt(server_fd_, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt));

    struct sockaddr_in addr{};
    addr.sin_family = AF_INET;
    addr.sin_addr.s_addr = INADDR_ANY;
    addr.sin_port = htons(port_);

    if (::bind(server_fd_, (struct sockaddr*)&addr, sizeof(addr)) < 0) {
        std::cerr << "Failed to bind on port " << port_ << "\n";
        return;
    }

    ::listen(server_fd_, 10);

    // Set non-blocking
    int flags = fcntl(server_fd_, F_GETFL, 0);
    fcntl(server_fd_, F_SETFL, flags | O_NONBLOCK);

    std::cout << "\n========================================\n"
              << "  Stress Testing Engine - Live Dashboard\n"
              << "  Open: http://localhost:" << port_ << "\n"
              << "========================================\n\n";

    while (running_.load()) {
        struct pollfd pfd{server_fd_, POLLIN, 0};
        int ret = ::poll(&pfd, 1, 200);
        if (ret <= 0) continue;

        struct sockaddr_in client_addr{};
        socklen_t client_len = sizeof(client_addr);
        int client_fd = ::accept(server_fd_, (struct sockaddr*)&client_addr, &client_len);
        if (client_fd < 0) continue;

        // Handle in a detached thread
        std::thread(&WebServer::handleClient, this, client_fd).detach();
    }
}

void WebServer::handleClient(int client_fd) {
    char buffer[8192];
    int n = ::recv(client_fd, buffer, sizeof(buffer) - 1, 0);
    if (n <= 0) { ::close(client_fd); return; }
    buffer[n] = '\0';

    std::string request(buffer);
    std::string method, path;
    std::istringstream iss(request);
    iss >> method >> path;

    if (path == "/" || path == "/index.html") {
        sendResponse(client_fd, "200 OK", "text/html", getIndexHtml());
    } else if (path == "/app.js") {
        sendResponse(client_fd, "200 OK", "application/javascript", getMainJs());
    } else if (path == "/style.css") {
        sendResponse(client_fd, "200 OK", "text/css", getStyleCss());
    } else if (path == "/events") {
        // Server-Sent Events endpoint
        std::string header =
            "HTTP/1.1 200 OK\r\n"
            "Content-Type: text/event-stream\r\n"
            "Cache-Control: no-cache\r\n"
            "Connection: keep-alive\r\n"
            "Access-Control-Allow-Origin: *\r\n\r\n";
        ::send(client_fd, header.c_str(), header.size(), MSG_NOSIGNAL);

        {
            std::lock_guard<std::mutex> lock(clients_mutex_);
            client_fds_.push_back(client_fd);
        }

        // Send last frame immediately if available
        if (!last_frame_json_.empty()) {
            sendSSE(client_fd, last_frame_json_);
        }

        // Keep connection alive
        while (running_.load()) {
            std::this_thread::sleep_for(std::chrono::seconds(1));
            std::string ping = ": keepalive\n\n";
            if (::send(client_fd, ping.c_str(), ping.size(), MSG_NOSIGNAL) < 0) break;
        }

        {
            std::lock_guard<std::mutex> lock(clients_mutex_);
            client_fds_.erase(std::remove(client_fds_.begin(), client_fds_.end(), client_fd),
                              client_fds_.end());
        }
        ::close(client_fd);
        return;
    } else {
        sendResponse(client_fd, "404 Not Found", "text/plain", "Not Found");
    }
    ::close(client_fd);
}

void WebServer::sendResponse(int client_fd, const std::string& status,
                              const std::string& content_type, const std::string& body) {
    std::ostringstream response;
    response << "HTTP/1.1 " << status << "\r\n"
             << "Content-Type: " << content_type << "\r\n"
             << "Content-Length: " << body.size() << "\r\n"
             << "Connection: close\r\n"
             << "Access-Control-Allow-Origin: *\r\n\r\n"
             << body;
    std::string resp = response.str();
    ::send(client_fd, resp.c_str(), resp.size(), MSG_NOSIGNAL);
}

void WebServer::sendSSE(int client_fd, const std::string& data) {
    std::string msg = "data: " + data + "\n\n";
    ::send(client_fd, msg.c_str(), msg.size(), MSG_NOSIGNAL);
}

void WebServer::broadcastFrame(const VisualizationFrame& /*frame*/) {
    // Use broadcastJson instead - called by DataBroadcaster
}

void WebServer::broadcastJson(const std::string& json) {
    last_frame_json_ = json;
    std::lock_guard<std::mutex> lock(clients_mutex_);
    std::vector<int> dead;
    for (int fd : client_fds_) {
        std::string msg = "data: " + json + "\n\n";
        if (::send(fd, msg.c_str(), msg.size(), MSG_NOSIGNAL) < 0) {
            dead.push_back(fd);
        }
    }
    for (int fd : dead) {
        client_fds_.erase(std::remove(client_fds_.begin(), client_fds_.end(), fd),
                          client_fds_.end());
    }
}

std::string WebServer::getStyleCss() {
    return R"CSS(
* { margin: 0; padding: 0; box-sizing: border-box; }
body {
    font-family: 'Segoe UI', system-ui, -apple-system, sans-serif;
    background: #0a0e17;
    color: #e0e0e0;
    overflow: hidden;
    height: 100vh;
}
.dashboard {
    display: grid;
    grid-template-columns: 1fr 1fr;
    grid-template-rows: 60px 1fr 1fr 200px;
    gap: 8px;
    padding: 8px;
    height: 100vh;
}
.header {
    grid-column: 1 / -1;
    display: flex;
    align-items: center;
    justify-content: space-between;
    padding: 0 20px;
    background: linear-gradient(135deg, #1a1f35, #0d1117);
    border-radius: 8px;
    border: 1px solid #30363d;
}
.header h1 { font-size: 18px; color: #58a6ff; }
.regime-badge {
    padding: 6px 16px;
    border-radius: 20px;
    font-weight: 600;
    font-size: 13px;
    text-transform: uppercase;
    letter-spacing: 1px;
}
.regime-BullQuiet { background: #0d4429; color: #3fb950; border: 1px solid #238636; }
.regime-BullVolatile { background: #2d3b0e; color: #a3d977; border: 1px solid #56d364; }
.regime-BearQuiet { background: #4a1e1e; color: #f08080; border: 1px solid #da3633; }
.regime-BearVolatile { background: #6b1a1a; color: #ff6b6b; border: 1px solid #ff4444;
    animation: pulse 1s infinite; }
.regime-Transition { background: #3d2e00; color: #d29922; border: 1px solid #d29922; }

@keyframes pulse {
    0%, 100% { box-shadow: 0 0 5px rgba(255,68,68,0.3); }
    50% { box-shadow: 0 0 20px rgba(255,68,68,0.6); }
}

.panel {
    background: linear-gradient(180deg, #161b22, #0d1117);
    border-radius: 8px;
    border: 1px solid #30363d;
    padding: 12px;
    position: relative;
    overflow: hidden;
}
.panel-title {
    font-size: 12px;
    color: #8b949e;
    text-transform: uppercase;
    letter-spacing: 1px;
    margin-bottom: 8px;
}
.panel-3d { grid-column: 1; grid-row: 2 / 4; }
.panel-signal { grid-column: 2; grid-row: 2; }
.panel-stress { grid-column: 2; grid-row: 3; }
.panel-metrics { grid-column: 1 / -1; grid-row: 4; }

canvas { width: 100% !important; height: 100% !important; }

.signal-card {
    padding: 16px;
    border-radius: 8px;
    margin-top: 8px;
}
.signal-StrongBuy { background: #0d4429; border-left: 4px solid #3fb950; }
.signal-Buy { background: #1a3a1a; border-left: 4px solid #56d364; }
.signal-Hold { background: #2d2d00; border-left: 4px solid #d29922; }
.signal-ReduceRisk { background: #3d2200; border-left: 4px solid #db6d28; }
.signal-GoToCash { background: #4a1e1e; border-left: 4px solid #f85149; }
.signal-Crisis { background: #6b1a1a; border-left: 4px solid #ff4444;
    animation: pulse 0.5s infinite; }

.signal-type { font-size: 24px; font-weight: 700; margin-bottom: 8px; }
.signal-reason { font-size: 13px; color: #b0b0b0; line-height: 1.5; }

.allocation-bars {
    display: flex; gap: 4px; margin-top: 12px; height: 30px; border-radius: 6px; overflow: hidden;
}
.alloc-cash { background: #58a6ff; }
.alloc-equity { background: #3fb950; }
.alloc-options { background: #d29922; }
.alloc-bar { display: flex; align-items: center; justify-content: center;
    font-size: 11px; font-weight: 600; transition: width 0.5s ease; }

.stress-table { width: 100%; font-size: 12px; border-collapse: collapse; }
.stress-table th { text-align: left; color: #8b949e; padding: 4px 8px;
    border-bottom: 1px solid #30363d; }
.stress-table td { padding: 4px 8px; border-bottom: 1px solid #21262d; }
.pnl-positive { color: #3fb950; }
.pnl-negative { color: #f85149; }

.metrics-grid {
    display: grid; grid-template-columns: repeat(8, 1fr); gap: 12px; height: 100%;
    align-content: center;
}
.metric { text-align: center; }
.metric-value { font-size: 22px; font-weight: 700; }
.metric-label { font-size: 11px; color: #8b949e; margin-top: 4px; }
.metric-green { color: #3fb950; }
.metric-red { color: #f85149; }
.metric-yellow { color: #d29922; }
.metric-blue { color: #58a6ff; }

.warning-bar {
    height: 4px;
    background: #21262d;
    border-radius: 2px;
    margin-top: 8px;
    overflow: hidden;
}
.warning-fill {
    height: 100%;
    border-radius: 2px;
    transition: width 0.5s ease, background 0.5s ease;
}

.regime-timeline {
    display: flex;
    gap: 2px;
    margin-top: 8px;
    height: 20px;
}
.regime-block {
    flex: 1;
    border-radius: 2px;
    transition: background 0.3s ease;
}
)CSS";
}

std::string WebServer::getIndexHtml() {
    return R"HTML(<!DOCTYPE html>
<html lang="en">
<head>
<meta charset="UTF-8">
<meta name="viewport" content="width=device-width, initial-scale=1.0">
<title>Stress Testing Engine - Regime Change Dashboard</title>
<link rel="stylesheet" href="/style.css">
</head>
<body>
<div class="dashboard">
    <div class="header">
        <h1>STRESS TESTING ENGINE</h1>
        <div>
            <span id="regime-badge" class="regime-badge regime-BullQuiet">BULL QUIET</span>
        </div>
        <div style="display:flex;align-items:center;gap:16px;">
            <div style="font-size:13px;">
                <span style="color:#8b949e;">SP500:</span>
                <span id="sp500-price" style="color:#58a6ff;font-weight:600;">--</span>
            </div>
            <div style="font-size:13px;">
                <span style="color:#8b949e;">VIX:</span>
                <span id="vix-level" style="color:#d29922;font-weight:600;">--</span>
            </div>
            <div style="font-size:13px;">
                <span style="color:#8b949e;">Day:</span>
                <span id="sim-day" style="color:#8b949e;">0</span>
            </div>
        </div>
    </div>

    <div class="panel panel-3d">
        <div class="panel-title">3D P&L Surface (Spot x Volatility x P&L)</div>
        <div id="three-container" style="width:100%;height:calc(100% - 30px);"></div>
    </div>

    <div class="panel panel-signal">
        <div class="panel-title">Trading Signal & Regime Analysis</div>
        <div id="signal-content"></div>
        <div class="panel-title" style="margin-top:12px;">Early Warning Score</div>
        <div class="warning-bar"><div id="warning-fill" class="warning-fill" style="width:5%;background:#3fb950;"></div></div>
        <div class="panel-title" style="margin-top:12px;">Regime Probability Distribution</div>
        <div id="regime-probs" style="display:flex;gap:8px;margin-top:4px;"></div>
        <div class="panel-title" style="margin-top:12px;">Regime Timeline</div>
        <div id="regime-timeline" class="regime-timeline"></div>
    </div>

    <div class="panel panel-stress">
        <div class="panel-title">Stress Test Results (Historical Scenarios)</div>
        <div style="overflow-y:auto;max-height:calc(100% - 30px);">
            <table class="stress-table">
                <thead><tr><th>Scenario</th><th>P&L</th><th>P&L %</th><th>Worst Leg</th></tr></thead>
                <tbody id="stress-tbody"></tbody>
            </table>
        </div>
    </div>

    <div class="panel panel-metrics">
        <div class="metrics-grid">
            <div class="metric">
                <div id="m-value" class="metric-value metric-blue">--</div>
                <div class="metric-label">Portfolio Value</div>
            </div>
            <div class="metric">
                <div id="m-return" class="metric-value metric-green">--</div>
                <div class="metric-label">Return</div>
            </div>
            <div class="metric">
                <div id="m-benchmark" class="metric-value metric-yellow">--</div>
                <div class="metric-label">SP500 Return</div>
            </div>
            <div class="metric">
                <div id="m-alpha" class="metric-value metric-green">--</div>
                <div class="metric-label">Alpha</div>
            </div>
            <div class="metric">
                <div id="m-sharpe" class="metric-value metric-blue">--</div>
                <div class="metric-label">Sharpe Ratio</div>
            </div>
            <div class="metric">
                <div id="m-drawdown" class="metric-value metric-red">--</div>
                <div class="metric-label">Max Drawdown</div>
            </div>
            <div class="metric">
                <div id="m-delta" class="metric-value metric-blue">--</div>
                <div class="metric-label">Portfolio Delta</div>
            </div>
            <div class="metric">
                <div id="m-vega" class="metric-value metric-yellow">--</div>
                <div class="metric-label">Portfolio Vega</div>
            </div>
        </div>
    </div>
</div>

<script src="https://cdnjs.cloudflare.com/ajax/libs/three.js/r128/three.min.js"></script>
<script src="https://cdn.jsdelivr.net/npm/three@0.128.0/examples/js/controls/OrbitControls.js"></script>
<script src="/app.js"></script>
</body>
</html>)HTML";
}

std::string WebServer::getMainJs() {
    return R"JS(
// Three.js 3D Visualization
let scene, camera, renderer, controls, surfaceMesh, gridHelper;
let regimeHistory = [];
let frameCount = 0;

function init3D() {
    const container = document.getElementById('three-container');
    if (!container) return;

    scene = new THREE.Scene();
    scene.background = new THREE.Color(0x0a0e17);
    scene.fog = new THREE.Fog(0x0a0e17, 50, 200);

    camera = new THREE.PerspectiveCamera(55, container.clientWidth / container.clientHeight, 0.1, 1000);
    camera.position.set(30, 25, 30);

    renderer = new THREE.WebGLRenderer({ antialias: true, alpha: true });
    renderer.setSize(container.clientWidth, container.clientHeight);
    renderer.setPixelRatio(window.devicePixelRatio);
    container.appendChild(renderer.domElement);

    controls = new THREE.OrbitControls(camera, renderer.domElement);
    controls.enableDamping = true;
    controls.dampingFactor = 0.05;
    controls.autoRotate = true;
    controls.autoRotateSpeed = 0.5;

    // Grid
    gridHelper = new THREE.GridHelper(40, 20, 0x30363d, 0x21262d);
    scene.add(gridHelper);

    // Axes
    const axesGroup = new THREE.Group();
    const axisMat = new THREE.LineBasicMaterial({ color: 0x58a6ff });
    // X axis (Spot)
    const xGeom = new THREE.BufferGeometry().setFromPoints([
        new THREE.Vector3(-20, 0, 0), new THREE.Vector3(20, 0, 0)]);
    axesGroup.add(new THREE.Line(xGeom, new THREE.LineBasicMaterial({ color: 0x58a6ff })));
    // Y axis (P&L)
    const yGeom = new THREE.BufferGeometry().setFromPoints([
        new THREE.Vector3(0, -15, 0), new THREE.Vector3(0, 15, 0)]);
    axesGroup.add(new THREE.Line(yGeom, new THREE.LineBasicMaterial({ color: 0x3fb950 })));
    // Z axis (Vol)
    const zGeom = new THREE.BufferGeometry().setFromPoints([
        new THREE.Vector3(0, 0, -20), new THREE.Vector3(0, 0, 20)]);
    axesGroup.add(new THREE.Line(zGeom, new THREE.LineBasicMaterial({ color: 0xd29922 })));
    scene.add(axesGroup);

    // Ambient light
    scene.add(new THREE.AmbientLight(0xffffff, 0.4));
    const dirLight = new THREE.DirectionalLight(0xffffff, 0.6);
    dirLight.position.set(10, 20, 10);
    scene.add(dirLight);

    // Regime indicator sphere
    const sphereGeom = new THREE.SphereGeometry(1, 32, 32);
    const sphereMat = new THREE.MeshPhongMaterial({ color: 0x3fb950, transparent: true, opacity: 0.7 });
    window.regimeSphere = new THREE.Mesh(sphereGeom, sphereMat);
    window.regimeSphere.position.set(0, 18, 0);
    scene.add(window.regimeSphere);

    window.addEventListener('resize', () => {
        camera.aspect = container.clientWidth / container.clientHeight;
        camera.updateProjectionMatrix();
        renderer.setSize(container.clientWidth, container.clientHeight);
    });

    animate();
}

function animate() {
    requestAnimationFrame(animate);
    controls.update();

    // Pulse the regime sphere
    if (window.regimeSphere) {
        const t = Date.now() * 0.001;
        window.regimeSphere.scale.setScalar(1.0 + 0.1 * Math.sin(t * 2));
    }

    renderer.render(scene, camera);
}

function updateSurface(surfaceData) {
    if (!surfaceData || !surfaceData.grid || surfaceData.grid.length === 0) return;

    // Remove old surface
    if (surfaceMesh) scene.remove(surfaceMesh);

    const rows = surfaceData.rows;
    const cols = surfaceData.cols;
    const geometry = new THREE.BufferGeometry();
    const vertices = [];
    const colors = [];
    const indices = [];

    // Compute ranges for normalization
    let xMin = Infinity, xMax = -Infinity;
    let yMin = Infinity, yMax = -Infinity;
    let zMin = Infinity, zMax = -Infinity;

    for (let i = 0; i < rows; i++) {
        for (let j = 0; j < cols; j++) {
            const p = surfaceData.grid[i][j];
            xMin = Math.min(xMin, p.x); xMax = Math.max(xMax, p.x);
            yMin = Math.min(yMin, p.y); yMax = Math.max(yMax, p.y);
            zMin = Math.min(zMin, p.z); zMax = Math.max(zMax, p.z);
        }
    }

    const xRange = xMax - xMin || 1;
    const yRange = yMax - yMin || 1;
    const zRange = Math.max(Math.abs(zMax), Math.abs(zMin)) || 1;

    for (let i = 0; i < rows; i++) {
        for (let j = 0; j < cols; j++) {
            const p = surfaceData.grid[i][j];
            // Map to 3D space: X=spot, Z=vol, Y=P&L
            const x = ((p.x - xMin) / xRange - 0.5) * 35;
            const z = ((p.y - yMin) / yRange - 0.5) * 35;
            const y = (p.z / zRange) * 12;
            vertices.push(x, y, z);
            colors.push(p.r, p.g, p.b);
        }
    }

    // Build triangle indices
    for (let i = 0; i < rows - 1; i++) {
        for (let j = 0; j < cols - 1; j++) {
            const a = i * cols + j;
            const b = i * cols + j + 1;
            const c = (i + 1) * cols + j;
            const d = (i + 1) * cols + j + 1;
            indices.push(a, b, c);
            indices.push(b, d, c);
        }
    }

    geometry.setAttribute('position', new THREE.Float32BufferAttribute(vertices, 3));
    geometry.setAttribute('color', new THREE.Float32BufferAttribute(colors, 3));
    geometry.setIndex(indices);
    geometry.computeVertexNormals();

    const material = new THREE.MeshPhongMaterial({
        vertexColors: true,
        side: THREE.DoubleSide,
        transparent: true,
        opacity: 0.85,
        shininess: 30,
        flatShading: false
    });

    surfaceMesh = new THREE.Mesh(geometry, material);
    scene.add(surfaceMesh);

    // Add wireframe overlay
    const wireMat = new THREE.MeshBasicMaterial({
        color: 0x30363d, wireframe: true, transparent: true, opacity: 0.15
    });
    const wireMesh = new THREE.Mesh(geometry.clone(), wireMat);
    surfaceMesh.add(wireMesh);
}

function regimeColor(regime) {
    const colors = {
        'BullQuiet': '#3fb950', 'BullVolatile': '#a3d977',
        'BearQuiet': '#f08080', 'BearVolatile': '#ff4444',
        'Transition': '#d29922'
    };
    return colors[regime] || '#8b949e';
}

function regimeColorHex(regime) {
    const colors = {
        'BullQuiet': 0x3fb950, 'BullVolatile': 0xa3d977,
        'BearQuiet': 0xf08080, 'BearVolatile': 0xff4444,
        'Transition': 0xd29922
    };
    return colors[regime] || 0x8b949e;
}

function updateDashboard(data) {
    frameCount++;

    // Update header
    document.getElementById('sim-day').textContent = frameCount;

    // Regime badge
    const badge = document.getElementById('regime-badge');
    badge.className = 'regime-badge regime-' + data.regime;
    badge.textContent = data.regime.replace(/([A-Z])/g, ' $1').trim().toUpperCase();

    // Regime sphere color
    if (window.regimeSphere) {
        window.regimeSphere.material.color.setHex(regimeColorHex(data.regime));
        if (data.regime === 'BearVolatile') {
            window.regimeSphere.scale.setScalar(2.0);
        }
    }

    // Portfolio metrics
    const p = data.portfolio;
    document.getElementById('m-value').textContent = '$' + (p.totalValue / 1000).toFixed(0) + 'K';
    document.getElementById('m-return').textContent = (p.portfolioReturn * 100).toFixed(2) + '%';
    document.getElementById('m-return').className = 'metric-value ' + (p.portfolioReturn >= 0 ? 'metric-green' : 'metric-red');
    document.getElementById('m-benchmark').textContent = (p.benchmarkReturn * 100).toFixed(2) + '%';
    document.getElementById('m-alpha').textContent = ((p.portfolioReturn - p.benchmarkReturn) * 100).toFixed(2) + '%';
    document.getElementById('m-alpha').className = 'metric-value ' + (p.portfolioReturn > p.benchmarkReturn ? 'metric-green' : 'metric-red');
    document.getElementById('m-sharpe').textContent = p.sharpe.toFixed(2);
    document.getElementById('m-drawdown').textContent = (p.maxDrawdown * 100).toFixed(1) + '%';
    document.getElementById('m-delta').textContent = p.delta.toFixed(2);
    document.getElementById('m-vega').textContent = p.vega.toFixed(2);

    // Signal card
    const sig = data.signal;
    document.getElementById('signal-content').innerHTML = `
        <div class="signal-card signal-${sig.signal}">
            <div class="signal-type">${sig.signal.replace(/([A-Z])/g, ' $1').trim()}</div>
            <div class="signal-reason">${sig.reason}</div>
            <div style="margin-top:8px;font-size:12px;color:#8b949e;">
                Confidence: ${(sig.confidence * 100).toFixed(0)}%
            </div>
            <div class="allocation-bars">
                <div class="alloc-bar alloc-cash" style="width:${sig.cashTarget*100}%">
                    Cash ${(sig.cashTarget*100).toFixed(0)}%</div>
                <div class="alloc-bar alloc-equity" style="width:${sig.equityTarget*100}%">
                    Equity ${(sig.equityTarget*100).toFixed(0)}%</div>
                <div class="alloc-bar alloc-options" style="width:${sig.optionsTarget*100}%">
                    Options ${(sig.optionsTarget*100).toFixed(0)}%</div>
            </div>
        </div>`;

    // Early warning bar
    const rs = data.regimeState;
    const warningScore = rs.crisisProb;
    const wFill = document.getElementById('warning-fill');
    wFill.style.width = (warningScore * 100) + '%';
    if (warningScore < 0.2) wFill.style.background = '#3fb950';
    else if (warningScore < 0.4) wFill.style.background = '#d29922';
    else if (warningScore < 0.6) wFill.style.background = '#db6d28';
    else wFill.style.background = '#ff4444';

    // Regime probabilities
    const regimeNames = ['Bull Quiet', 'Bull Volatile', 'Bear Quiet', 'Bear Volatile', 'Transition'];
    const regimeKeys = ['BullQuiet', 'BullVolatile', 'BearQuiet', 'BearVolatile', 'Transition'];
    let probsHtml = '';
    for (let i = 0; i < 5; i++) {
        const prob = (rs.probabilities[i] * 100).toFixed(0);
        probsHtml += `<div style="flex:1;text-align:center;">
            <div style="height:60px;display:flex;align-items:flex-end;justify-content:center;">
                <div style="width:100%;background:${regimeColor(regimeKeys[i])};
                    height:${Math.max(2, prob * 0.6)}px;border-radius:3px 3px 0 0;
                    transition:height 0.3s ease;"></div>
            </div>
            <div style="font-size:10px;color:#8b949e;margin-top:2px;">${regimeNames[i]}</div>
            <div style="font-size:12px;font-weight:600;color:${regimeColor(regimeKeys[i])};">${prob}%</div>
        </div>`;
    }
    document.getElementById('regime-probs').innerHTML = probsHtml;

    // Regime timeline
    regimeHistory.push(data.regime);
    if (regimeHistory.length > 100) regimeHistory.shift();
    let timelineHtml = '';
    for (const r of regimeHistory) {
        timelineHtml += `<div class="regime-block" style="background:${regimeColor(r)};"></div>`;
    }
    document.getElementById('regime-timeline').innerHTML = timelineHtml;

    // Stress test results
    if (data.stressResults && data.stressResults.length > 0) {
        let tbody = '';
        for (const r of data.stressResults) {
            const pnlClass = r.pnl >= 0 ? 'pnl-positive' : 'pnl-negative';
            tbody += `<tr>
                <td>${r.scenario}</td>
                <td class="${pnlClass}">$${(r.pnl/1000).toFixed(1)}K</td>
                <td class="${pnlClass}">${(r.pnlPct*100).toFixed(1)}%</td>
                <td>${r.worstLeg}</td>
            </tr>`;
        }
        document.getElementById('stress-tbody').innerHTML = tbody;
    }

    // Update 3D surface
    if (data.surfaces && data.surfaces.length > 0) {
        updateSurface(data.surfaces[0]);
    }
}

// SSE Connection
function connectSSE() {
    const evtSource = new EventSource('/events');
    evtSource.onmessage = function(event) {
        try {
            const data = JSON.parse(event.data);
            updateDashboard(data);
        } catch (e) { console.error('Parse error:', e); }
    };
    evtSource.onerror = function() {
        console.log('SSE connection lost, reconnecting...');
        evtSource.close();
        setTimeout(connectSSE, 2000);
    };
}

// Initialize
init3D();
connectSSE();
)JS";
}

} // namespace ste
