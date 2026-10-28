#include "gateway_engine.hpp"
#include <iostream>
#include <iomanip>
#include <chrono>
#include <thread>
#include <csignal>
#include <atomic>
#include <string>

namespace {
std::atomic<bool> g_shutdown_requested{false};

void signal_handler(int) {
    g_shutdown_requested.store(true);
}

void print_banner() {
    std::cout << "\033[36m"
              << "   ___                             ______    __          \n"
              << "  / _ | ___  ___ __ __ ___   ___  / __/ /__ / /____ _____ \n"
              << " / __ |/ _ \\/ -_) \\ // -_) / -_) / _// / -_) __/ // / -_)\n"
              << "/_/ |_/ .__/\\__//_\\_\\\\__/  \\__/ /___/_/\\__/\\__/\\_, /\\__/ \n"
              << "     /_/                                      /___/       \n"
              << "\033[0m";
    std::cout << "\033[1;32m[ApexEdge]\033[0m High-Performance Edge IoT Telemetry & Anomaly Gateway\n";
    std::cout << "\033[90mEngine: Modern C++20 | Zero-Alloc SPSC Buffer | Welford Anomaly Detection\033[0m\n";
    std::cout << "----------------------------------------------------------------------\n";
}

void run_benchmark_mode(size_t total_iterations = 2000000) {
    std::cout << "\n\033[33m[BENCHMARK]\033[0m Running lock-free SPSC buffer & anomaly detector benchmark (" 
              << total_iterations << " ops)...\n";

    apex::SPSCRingBuffer<apex::TelemetryRecord> ring(65536);
    apex::AnomalyDetector detector(3.0, 50, 0.05);

    auto t_start = std::chrono::high_resolution_clock::now();

    // Producer / Consumer thread benchmark
    std::atomic<bool> producer_done{false};
    std::thread producer([&]() {
        for (size_t i = 0; i < total_iterations; ++i) {
            apex::TelemetryRecord rec(1000 + i, "bench-node", "vibration", 2.5 + (i % 10) * 0.1, "mm/s");
            while (!ring.push(rec)) {
                std::this_thread::yield();
            }
        }
        producer_done.store(true);
    });

    uint64_t processed = 0;
    uint64_t anomalies = 0;
    std::thread consumer([&]() {
        apex::TelemetryRecord rec;
        while (!producer_done.load() || !ring.empty()) {
            if (ring.pop(rec)) {
                if (detector.process(rec)) {
                    ++anomalies;
                }
                ++processed;
            } else {
                std::this_thread::yield();
            }
        }
    });

    producer.join();
    consumer.join();

    auto t_end = std::chrono::high_resolution_clock::now();
    double duration_sec = std::chrono::duration<double>(t_end - t_start).count();
    double throughput = static_cast<double>(processed) / duration_sec;

    std::cout << "\033[32m[RESULT]\033[0m Processed: " << processed << " records in " 
              << std::fixed << std::setprecision(3) << duration_sec << " s\n";
    std::cout << "\033[1;32m[THROUGHPUT]\033[0m \033[1m" << std::fixed << std::setprecision(0) 
              << throughput << " records/sec\033[0m (" 
              << std::fixed << std::setprecision(2) << (duration_sec * 1e9 / processed) << " ns/op)\n";
    std::cout << "\033[36m[ANOMALIES]\033[0m Detected: " << anomalies << "\n\n";
}

} // namespace

int main(int argc, char* argv[]) {
    std::signal(SIGINT, signal_handler);
    std::signal(SIGTERM, signal_handler);

    apex::GatewayConfig config;
    bool benchmark_only = false;

    // Simple CLI argument parsing
    for (int i = 1; i < argc; ++i) {
        std::string arg = argv[i];
        if (arg == "--benchmark") {
            benchmark_only = true;
        } else if (arg == "--host" && i + 1 < argc) {
            config.mqtt_broker_host = argv[++i];
        } else if (arg == "--port" && i + 1 < argc) {
            config.mqtt_broker_port = static_cast<uint16_t>(std::stoi(argv[++i]));
        } else if (arg == "--nodes" && i + 1 < argc) {
            config.simulated_devices_count = std::stoul(argv[++i]);
        } else if (arg == "--rate" && i + 1 < argc) {
            config.sensor_poll_interval_ms = static_cast<uint32_t>(std::stoul(argv[++i]));
        } else if (arg == "--help") {
            std::cout << "Usage: apex_edge [OPTIONS]\n"
                      << "  --benchmark       Run lock-free SPSC throughput benchmark and exit\n"
                      << "  --host <ip>       MQTT broker host (default: 127.0.0.1)\n"
                      << "  --port <port>     MQTT broker port (default: 1883)\n"
                      << "  --nodes <count>   Simulated sensor clusters (default: 3)\n"
                      << "  --rate <ms>       Sensor polling interval in ms (default: 100)\n"
                      << "  --help            Show this help message\n";
            return 0;
        }
    }

    print_banner();

    if (benchmark_only) {
        run_benchmark_mode();
        return 0;
    }

    std::cout << "[Config] MQTT Target: " << config.mqtt_broker_host << ":" << config.mqtt_broker_port << "\n";
    std::cout << "[Config] Industrial Nodes: " << config.simulated_devices_count << " (4 telemetry channels each)\n";
    std::cout << "[Config] Polling Interval: " << config.sensor_poll_interval_ms << " ms\n";
    std::cout << "[Config] Offline Journal: " << config.journal_file_path << "\n";
    std::cout << "Starting Gateway Engine (Press Ctrl+C to terminate)...\n\n";

    apex::GatewayEngine engine(config);
    engine.start();

    // Status display loop
    uint32_t ticks = 0;
    while (!g_shutdown_requested.load()) {
        std::this_thread::sleep_for(std::chrono::milliseconds(1000));
        ++ticks;

        auto m = engine.get_metrics();

        std::cout << "\r\033[K"
                  << "\033[90m[" << std::setw(3) << ticks << "s]\033[0m "
                  << "Ingested: \033[36m" << m.total_ingested << "\033[0m | "
                  << "Processed: \033[32m" << m.total_processed << "\033[0m | "
                  << "Anomalies: \033[31m" << m.total_anomalies << "\033[0m | "
                  << "Dispatched: \033[33m" << m.total_dispatched << "\033[0m | "
                  << "Journal: \033[35m" << m.journaled_records << "\033[0m | "
                  << "Queue: " << m.ring_buffer_size << "/" << m.ring_buffer_capacity << " | "
                  << "Speed: \033[1;37m" << std::fixed << std::setprecision(0) << m.current_throughput_ops << " rec/s\033[0m"
                  << std::flush;
    }

    std::cout << "\n\nShutting down gracefully, flushing buffers...\n";
    engine.stop();
    std::cout << "ApexEdge Gateway stopped cleanly. All states persisted.\n";
    return 0;
}
