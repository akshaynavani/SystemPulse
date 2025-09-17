#include "systempulse/csv_writer.h"

#include <sys/stat.h>

#include <cerrno>
#include <cstring>

namespace systempulse {

namespace {
constexpr size_t kBatchSize = 512;
}

CsvWriter::CsvWriter(SampleQueue& in, std::string path, bool append)
    : in_(in), path_(std::move(path)), append_(append) {}

CsvWriter::~CsvWriter() {
    join();
    if (fp_ && ownsFile_) {
        std::fclose(fp_);
    }
}

bool CsvWriter::open() {
    if (path_ == "-") {
        fp_ = stdout;
        ownsFile_ = false;
        std::fprintf(fp_, "%s\n", header());
        return true;
    }
    bool writeHeader = true;
    if (append_) {
        struct stat st {};
        writeHeader = ::stat(path_.c_str(), &st) != 0 || st.st_size == 0;
    }
    fp_ = std::fopen(path_.c_str(), append_ ? "a" : "w");
    if (!fp_) {
        std::fprintf(stderr, "systempulse: cannot open %s: %s\n", path_.c_str(),
                     std::strerror(errno));
        return false;
    }
    ownsFile_ = true;
    if (writeHeader) {
        std::fprintf(fp_, "%s\n", header());
    }
    return true;
}

bool CsvWriter::start() {
    if (!fp_ && !open()) {
        return false;
    }
    return thread_.start("sp-writer", [this] { run(); });
}

void CsvWriter::join() { thread_.join(); }

uint64_t CsvWriter::rowsWritten() const {
    LockGuard g(mu_);
    return rows_;
}

std::map<SeriesKey, double> CsvWriter::latest() const {
    LockGuard g(mu_);
    return latest_;
}

std::string CsvWriter::escapeField(const std::string& field) {
    if (field.find_first_of(",\"\n\r") == std::string::npos) {
        return field;
    }
    std::string out = "\"";
    for (char c : field) {
        if (c == '"') {
            out += "\"\"";
        } else if (c == '\n' || c == '\r') {
            out += ' ';
        } else {
            out += c;
        }
    }
    out += '"';
    return out;
}

std::string CsvWriter::formatRow(const Sample& s) {
    char value[64];
    std::snprintf(value, sizeof(value), "%.6g", s.value);
    std::string row;
    row.reserve(64 + s.entity.size() + s.metric.size());
    row += std::to_string(s.timestampMs);
    row += ',';
    row += escapeField(s.source);
    row += ',';
    row += escapeField(s.entity);
    row += ',';
    row += escapeField(s.metric);
    row += ',';
    row += value;
    return row;
}

void CsvWriter::run() {
    for (;;) {
        auto batch = in_.popBatch(kBatchSize);
        if (batch.empty()) {
            break;  // queue closed and drained
        }
        for (const auto& s : batch) {
            const std::string row = formatRow(s);
            std::fputs(row.c_str(), fp_);
            std::fputc('\n', fp_);
        }
        std::fflush(fp_);
        LockGuard g(mu_);
        rows_ += batch.size();
        for (const auto& s : batch) {
            latest_[SeriesKey{s.source, s.entity, s.metric}] = s.value;
        }
    }
    std::fflush(fp_);
}

}  // namespace systempulse
