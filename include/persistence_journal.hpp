#pragma once

#include "telemetry_record.hpp"
#include <string>
#include <fstream>
#include <vector>
#include <mutex>
#include <cstdint>

namespace apex {

class PersistenceJournal {
public:
    explicit PersistenceJournal(std::string file_path);
    ~PersistenceJournal();

    // Appends an un-dispatched record to the binary journal
    bool append(const TelemetryRecord& record);

    // Reads pending records up to max_batch for re-dispatch
    std::vector<TelemetryRecord> read_batch(size_t max_batch = 100);

    // Clears or truncates replayed records
    void commit_replayed(size_t count);

    [[nodiscard]] uint64_t pending_records_count() const noexcept { return pending_count_; }
    [[nodiscard]] const std::string& file_path() const noexcept { return file_path_; }

private:
    std::string file_path_;
    mutable std::mutex mutex_;
    uint64_t pending_count_{0};
};

} // namespace apex
