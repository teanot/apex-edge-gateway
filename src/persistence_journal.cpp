#include "persistence_journal.hpp"
#include <iostream>
#include <filesystem>

namespace apex {

PersistenceJournal::PersistenceJournal(std::string file_path)
    : file_path_(std::move(file_path)) {
    // Check initial file size if exists
    if (std::filesystem::exists(file_path_)) {
        auto sz = std::filesystem::file_size(file_path_);
        pending_count_ = sz / sizeof(TelemetryRecord);
    }
}

PersistenceJournal::~PersistenceJournal() = default;

bool PersistenceJournal::append(const TelemetryRecord& record) {
    std::lock_guard<std::mutex> lock(mutex_);
    std::ofstream out(file_path_, std::ios::binary | std::ios::app);
    if (!out.is_open()) {
        return false;
    }

    out.write(reinterpret_cast<const char*>(&record), sizeof(TelemetryRecord));
    if (out.good()) {
        ++pending_count_;
        return true;
    }
    return false;
}

std::vector<TelemetryRecord> PersistenceJournal::read_batch(size_t max_batch) {
    std::lock_guard<std::mutex> lock(mutex_);
    std::vector<TelemetryRecord> batch;
    if (pending_count_ == 0 || !std::filesystem::exists(file_path_)) {
        return batch;
    }

    std::ifstream in(file_path_, std::ios::binary);
    if (!in.is_open()) {
        return batch;
    }

    TelemetryRecord rec;
    while (batch.size() < max_batch && in.read(reinterpret_cast<char*>(&rec), sizeof(TelemetryRecord))) {
        batch.push_back(rec);
    }

    return batch;
}

void PersistenceJournal::commit_replayed(size_t count) {
    std::lock_guard<std::mutex> lock(mutex_);
    if (count == 0 || !std::filesystem::exists(file_path_)) {
        return;
    }

    // Read remaining records beyond count
    std::ifstream in(file_path_, std::ios::binary);
    if (!in.is_open()) {
        return;
    }

    // Seek past replayed count
    in.seekg(static_cast<std::streamoff>(count * sizeof(TelemetryRecord)), std::ios::beg);

    std::string tmp_path = file_path_ + ".tmp";
    std::ofstream out(tmp_path, std::ios::binary | std::ios::trunc);
    if (out.is_open()) {
        out << in.rdbuf();
        out.close();
        in.close();

        std::filesystem::rename(tmp_path, file_path_);
        auto sz = std::filesystem::file_size(file_path_);
        pending_count_ = sz / sizeof(TelemetryRecord);
    }
}

} // namespace apex
