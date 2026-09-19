#pragma once

#include "lob/types.hpp"
#include <string>
#include <fstream>
#include <vector>
#include <stdexcept>
#include <cstring>

namespace lob {

#pragma pack(push, 1)
struct FeedFileHeader {
    char magic[4]{'L', 'O', 'B', 'F'};
    uint32_t version{1};
    uint64_t total_events{0};
    uint64_t start_timestamp{0};
    uint64_t end_timestamp{0};
    uint8_t reserved[32]{0};
};
#pragma pack(pop)

static_assert(sizeof(FeedFileHeader) == 64, "FeedFileHeader must be exactly 64 bytes");

class BinaryFeedWriter {
public:
    BinaryFeedWriter() = default;
    explicit BinaryFeedWriter(const std::string& path) {
        open(path);
    }

    ~BinaryFeedWriter() {
        close();
    }

    bool open(const std::string& path) {
        file_.open(path, std::ios::binary | std::ios::trunc);
        if (!file_.is_open()) return false;

        header_ = FeedFileHeader{};
        file_.write(reinterpret_cast<const char*>(&header_), sizeof(FeedFileHeader));
        event_count_ = 0;
        return true;
    }

    bool write_event(const MarketEvent& ev) {
        if (!file_.is_open()) return false;
        MarketEventRecord rec = ev.to_record();
        file_.write(reinterpret_cast<const char*>(&rec), sizeof(MarketEventRecord));
        if (event_count_ == 0) {
            header_.start_timestamp = ev.timestamp;
        }
        header_.end_timestamp = ev.timestamp;
        ++event_count_;
        return file_.good();
    }

    void close() {
        if (file_.is_open()) {
            header_.total_events = event_count_;
            file_.seekp(0, std::ios::beg);
            file_.write(reinterpret_cast<const char*>(&header_), sizeof(FeedFileHeader));
            file_.flush();
            file_.close();
        }
    }

    [[nodiscard]] uint64_t event_count() const noexcept { return event_count_; }

private:
    std::ofstream file_;
    FeedFileHeader header_{};
    uint64_t event_count_{0};
};

class BinaryFeedReader {
public:
    BinaryFeedReader() = default;
    explicit BinaryFeedReader(const std::string& path) {
        open(path);
    }

    ~BinaryFeedReader() {
        close();
    }

    bool open(const std::string& path) {
        file_.open(path, std::ios::binary);
        if (!file_.is_open()) return false;

        file_.read(reinterpret_cast<char*>(&header_), sizeof(FeedFileHeader));
        if (!file_.good() || std::memcmp(header_.magic, "LOBF", 4) != 0) {
            file_.close();
            return false;
        }

        total_events_ = header_.total_events;
        read_count_ = 0;
        return true;
    }

    bool read_next(MarketEvent& out_event) {
        if (!file_.is_open() || file_.eof()) return false;

        MarketEventRecord rec;
        file_.read(reinterpret_cast<char*>(&rec), sizeof(MarketEventRecord));
        if (file_.gcount() != static_cast<std::streamsize>(sizeof(MarketEventRecord))) {
            return false;
        }

        out_event = MarketEvent::from_record(rec);
        ++read_count_;
        return true;
    }

    void rewind() {
        if (file_.is_open()) {
            file_.clear();
            file_.seekg(sizeof(FeedFileHeader), std::ios::beg);
            read_count_ = 0;
        }
    }

    void close() {
        if (file_.is_open()) {
            file_.close();
        }
    }

    [[nodiscard]] bool is_open() const noexcept { return file_.is_open(); }
    [[nodiscard]] uint64_t total_events() const noexcept { return total_events_; }
    [[nodiscard]] uint64_t read_count() const noexcept { return read_count_; }
    [[nodiscard]] const FeedFileHeader& header() const noexcept { return header_; }

private:
    std::ifstream file_;
    FeedFileHeader header_{};
    uint64_t total_events_{0};
    uint64_t read_count_{0};
};

} // namespace lob
