#include "test_harness.hpp"
#include "persistence_journal.hpp"
#include <filesystem>

TEST_CASE(PersistenceJournal_AppendAndReplay) {
    const std::string test_file = "test_journal.bin";
    if (std::filesystem::exists(test_file)) {
        std::filesystem::remove(test_file);
    }

    {
        apex::PersistenceJournal journal(test_file);
        REQUIRE(journal.pending_records_count() == 0);

        apex::TelemetryRecord r1(1001, "node-1", "power", 45.2, "kW");
        apex::TelemetryRecord r2(1002, "node-1", "temp", 72.1, "degC");

        REQUIRE(journal.append(r1));
        REQUIRE(journal.append(r2));
        REQUIRE(journal.pending_records_count() == 2);

        auto batch = journal.read_batch(10);
        REQUIRE(batch.size() == 2);
        REQUIRE(std::string(batch[0].device_id) == "node-1");
        REQUIRE_NEAR(batch[0].value, 45.2, 0.001);
        REQUIRE_NEAR(batch[1].value, 72.1, 0.001);

        journal.commit_replayed(1);
        REQUIRE(journal.pending_records_count() == 1);

        auto remaining = journal.read_batch(10);
        REQUIRE(remaining.size() == 1);
        REQUIRE(std::string(remaining[0].metric_name) == "temp");
    }

    if (std::filesystem::exists(test_file)) {
        std::filesystem::remove(test_file);
    }
}
