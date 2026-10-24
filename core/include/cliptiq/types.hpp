#pragma once
#include <cstdint>
#include <optional>
#include <string>
#include <utility>
#include <variant>
#include <vector>

namespace cliptiq {
using EntryId = std::string;
using FolderId = std::string;
using SnippetId = std::string;
using Bytes = std::vector<std::uint8_t>;
enum class ErrorCode { InvalidArgument, TooLarge, NotFound, Busy, ReadOnly, Full, Corrupt, Io, Unsupported, Internal };
struct Error { ErrorCode code; std::string message; };
template<class T> class Result {
    std::variant<T, Error> state_;
public:
    Result(T value) : state_(std::move(value)) {}
    Result(Error error) : state_(std::move(error)) {}
    explicit operator bool() const { return std::holds_alternative<T>(state_); }
    T& value() { return std::get<T>(state_); }
    const T& value() const { return std::get<T>(state_); }
    const Error& error() const { return std::get<Error>(state_); }
};
template<> class Result<void> {
    std::optional<Error> error_;
public:
    Result() = default;
    Result(Error error) : error_(std::move(error)) {}
    explicit operator bool() const { return !error_; }
    const Error& error() const { return *error_; }
};
enum class CanonicalKind { Text, Rtf, Rtfd, Html, Pdf, Files, Url, Image, Opaque };
enum class SourceConfidence { Known, BestEffort, Unknown };
enum class Flag { Unknown, No, Yes };
struct SourceInfo { std::optional<std::string> app_id; SourceConfidence confidence = SourceConfidence::Unknown; };
struct ClipboardFlags { Flag transient = Flag::Unknown, concealed = Flag::Unknown, remote = Flag::Unknown; };
enum class TextEncoding { Utf8, Utf16Le, Utf16Be };
struct Representation {
    std::uint32_t item_index = 0;
    std::string platform, native_type;
    CanonicalKind kind = CanonicalKind::Opaque;
    Bytes data;
    TextEncoding text_encoding = TextEncoding::Utf8;
};
struct ClipboardSnapshot {
    std::vector<Representation> representations;
    std::string sequence_token;
    SourceInfo source;
    ClipboardFlags flags;
    // Capture-only complete ordered native offers, including metadata/markers.
    // T02 persists allowed representations and flags, not this offer list.
    struct OfferedType { std::string platform, native_type; };
    std::vector<OfferedType> offered_types;
};
struct HistoryEntry {
    EntryId id;
    std::string digest;
    std::int64_t created_ms = 0, used_ms = 0;
    std::uint64_t payload_bytes = 0;
};
enum class SortOrder { Created, LastUsed };
struct Limits {
    std::uint64_t representation_bytes = 32ull * 1024 * 1024;
    std::uint64_t entry_bytes = 64ull * 1024 * 1024;
    std::uint64_t history_bytes = 256ull * 1024 * 1024;
    std::uint32_t max_representations = 4096;
};
struct HistoryPolicy {
    std::size_t max_count = 30;
    bool allow_duplicates = true, replace_duplicate = true;
    SortOrder sort_order = SortOrder::LastUsed;
};
struct CleanupPolicy {
    // Disabled when zero. Whole-history clear, never an entry TTL.
    std::int64_t interval_ms = 0;
};
}
