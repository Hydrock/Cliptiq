#pragma once
#include "capabilities.hpp"
#include <array>
#include <string_view>

namespace cliptiq {
enum class UnknownSourceMode { RejectWhenExclusionsConfigured, AllowUnknown, RejectAlways };
struct CapturePolicy {
    // Text, RTF, RTFD, HTML, PDF, Files, URL, Image, Opaque.
    std::array<bool,9> enabled_kinds{{true,true,true,false,true,true,true,true,false}};
    bool ignore_concealed = false;
    bool ignore_remote_macos = false;
    bool request_ocr = false;
    std::vector<std::string> excluded_apps;
    UnknownSourceMode unknown_source_mode = UnknownSourceMode::RejectWhenExclusionsConfigured;
};
enum class CaptureDisposition { Accept, Reject };
enum class CaptureReason {
    Accepted, AcceptedLimitedSource, CaptureUnavailable, InvalidInput,
    Transient, Concealed, RemoteClipboard, RemotePrimaryFile,
    ExcludedApplication, UnknownSource, NoEnabledRepresentations
};
struct CaptureDecision {
    CaptureDisposition disposition = CaptureDisposition::Reject;
    CaptureReason reason = CaptureReason::InvalidInput;
    std::optional<ClipboardSnapshot> snapshot;
};
std::string_view capture_reason_message(CaptureReason);
CaptureDecision evaluate_capture(const ClipboardSnapshot&,const CapturePolicy&,const Capabilities&);

// Sole capture-side dispatch gate. Future native/screenshot adapters share it.
// Only a successfully retained history ID may reach thumbnail/OCR consumers.
class CaptureSink {
public:
    virtual ~CaptureSink() = default;
    virtual Result<std::optional<EntryId>> store(const ClipboardSnapshot&) = 0;
    virtual void thumbnail(const EntryId&,const ClipboardSnapshot&) = 0;
    virtual void ocr(const EntryId&,const ClipboardSnapshot&) = 0;
};
Result<CaptureDecision> dispatch_capture(const ClipboardSnapshot&,const CapturePolicy&,const Capabilities&,CaptureSink&);
}
