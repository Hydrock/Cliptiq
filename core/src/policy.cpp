#include "cliptiq/policy.hpp"
#include <algorithm>

namespace cliptiq {
namespace {
constexpr std::string_view transient_type = "org.nspasteboard.TransientType";
constexpr std::string_view concealed_type = "org.nspasteboard.ConcealedType";
constexpr std::string_view remote_type = "com.apple.is-remote-clipboard";

CaptureDecision reject(CaptureReason reason) {
    return {CaptureDisposition::Reject, reason, std::nullopt};
}
bool valid(const Capability& c) {
    return static_cast<unsigned>(c.status) <= 2 && c.reason.size() <= 4096;
}
bool valid_id(const std::string& id) {
    return !id.empty() && id.size() <= 4096 && id.find('\0') == std::string::npos;
}
bool valid(const ClipboardSnapshot& v, const CapturePolicy& p, const Capabilities& c) {
    if (v.representations.size() > 4096 || v.offered_types.size() > 4096 ||
        p.excluded_apps.size() > 1000 || c.native_types.size() > 4096 ||
        static_cast<unsigned>(p.unknown_source_mode) > 2 ||
        static_cast<unsigned>(c.platform) > 4 ||
        static_cast<unsigned>(v.source.confidence) > 2 ||
        static_cast<unsigned>(v.flags.transient) > 2 ||
        static_cast<unsigned>(v.flags.concealed) > 2 ||
        static_cast<unsigned>(v.flags.remote) > 2 || v.sequence_token.size() > 4096)
        return false;
    for (const auto* capability : {&c.capture, &c.write, &c.hotkeys, &c.auto_paste,
                                  &c.source_app, &c.tray, &c.pointer_placement, &c.ocr})
        if (!valid(*capability)) return false;
    if (v.source.app_id && !valid_id(*v.source.app_id)) return false;
    for (const auto& id : p.excluded_apps) if (!valid_id(id)) return false;
    for (const auto& type : v.offered_types)
        if (!valid_id(type.platform) || !valid_id(type.native_type)) return false;
    for (const auto& type : c.native_types)
        if (!valid_id(type.platform) || !valid_id(type.native_type) ||
            static_cast<unsigned>(type.kind) > 8 ||
            static_cast<unsigned>(type.text_encoding) > 2) return false;
    std::uint64_t bytes = 0;
    const Limits ceilings;
    for (const auto& r : v.representations) {
        if (!valid_id(r.platform) || !valid_id(r.native_type) ||
            static_cast<unsigned>(r.kind) > 8 || static_cast<unsigned>(r.text_encoding) > 2 ||
            r.data.size() > ceilings.representation_bytes || r.data.size() > ceilings.entry_bytes - bytes)
            return false;
        bytes += r.data.size();
    }
    return true;
}
bool has_type(const ClipboardSnapshot& v, std::string_view name) {
    for (const auto& t : v.offered_types) if (t.native_type == name) return true;
    for (const auto& r : v.representations) if (r.native_type == name) return true;
    return false;
}
bool metadata_type(std::string_view name) {
    return name == transient_type || name == concealed_type || name == remote_type;
}
const NativeTypeSupport* recognized(const std::string& platform, const std::string& name,
                                    const Capabilities& c) {
    for (const auto& t : c.native_types)
        if (t.platform == platform && t.native_type == name) return &t;
    return nullptr;
}
bool primary_remote_file(const ClipboardSnapshot& v, const Capabilities& c) {
    // Inspect original offers before kind filtering; never promote a secondary
    // file to primary merely because an earlier format is disabled/unknown.
    if (!v.offered_types.empty()) {
        for (const auto& t : v.offered_types) {
            if (metadata_type(t.native_type)) continue;
            auto known = recognized(t.platform, t.native_type, c);
            return known && known->kind == CanonicalKind::Files;
        }
    } else {
        for (const auto& r : v.representations) {
            if (metadata_type(r.native_type)) continue;
            auto known = recognized(r.platform, r.native_type, c);
            return known && known->kind == CanonicalKind::Files;
        }
    }
    return false;
}
bool marker_excluded(const ClipboardSnapshot& v, const CapturePolicy& p) {
    auto matches = [&](const std::string& type) {
        return std::any_of(p.excluded_apps.begin(), p.excluded_apps.end(), [&](const auto& app) {
            return !type.empty() && app.compare(0, type.size(), type) == 0;
        });
    };
    for (const auto& t : v.offered_types) if (matches(t.native_type)) return true;
    for (const auto& r : v.representations) if (matches(r.native_type)) return true;
    return false;
}
SourceInfo effective_source(const SourceInfo& source, const Capability& c) {
    if (c.status == CapabilityStatus::Unavailable || source.confidence == SourceConfidence::Unknown ||
        !source.app_id) return {};
    auto result = source;
    if (c.status == CapabilityStatus::Limited) result.confidence = SourceConfidence::BestEffort;
    return result;
}
}

CaptureDecision evaluate_capture(const ClipboardSnapshot& v, const CapturePolicy& p, const Capabilities& c) {
    if (!valid(v, p, c)) return reject(CaptureReason::InvalidInput);
    if (c.capture.status == CapabilityStatus::Unavailable) return reject(CaptureReason::CaptureUnavailable);
    if (c.platform == CapturePlatform::Unknown) return reject(CaptureReason::InvalidInput);

    auto flags = v.flags;
    if (has_type(v, transient_type)) flags.transient = Flag::Yes;
    if (has_type(v, concealed_type)) flags.concealed = Flag::Yes;
    const bool mac = c.platform == CapturePlatform::MacOS;
    if (mac && has_type(v, remote_type)) flags.remote = Flag::Yes;
    if (flags.transient == Flag::Yes) return reject(CaptureReason::Transient);
    if (p.ignore_concealed && flags.concealed == Flag::Yes) return reject(CaptureReason::Concealed);
    if (mac && flags.remote == Flag::Yes) {
        if (p.ignore_remote_macos) return reject(CaptureReason::RemoteClipboard);
        if (primary_remote_file(v, c)) return reject(CaptureReason::RemotePrimaryFile);
    }
    auto source = effective_source(v.source, c.source_app);
    if (marker_excluded(v, p)) return reject(CaptureReason::ExcludedApplication);
    if (source.app_id && std::find(p.excluded_apps.begin(), p.excluded_apps.end(), *source.app_id) != p.excluded_apps.end())
        return reject(CaptureReason::ExcludedApplication);
    if (source.confidence == SourceConfidence::Unknown &&
        (p.unknown_source_mode == UnknownSourceMode::RejectAlways ||
         (p.unknown_source_mode == UnknownSourceMode::RejectWhenExclusionsConfigured && !p.excluded_apps.empty())))
        return reject(CaptureReason::UnknownSource);

    ClipboardSnapshot out;
    out.sequence_token = v.sequence_token;
    out.source = std::move(source);
    out.flags = flags;
    for (const auto& r : v.representations) {
        if (metadata_type(r.native_type)) continue;
        auto known = recognized(r.platform, r.native_type, c);
        if (!known || known->kind != r.kind || r.kind == CanonicalKind::Opaque ||
            (r.kind == CanonicalKind::Text && known->text_encoding != r.text_encoding)) continue;
        if (!p.enabled_kinds[static_cast<unsigned>(r.kind)]) continue;
        if (mac && flags.remote == Flag::Yes && r.kind == CanonicalKind::Files) continue;
        out.representations.push_back(r);
    }
    if (out.representations.empty()) return reject(CaptureReason::NoEnabledRepresentations);
    auto reason = out.source.confidence == SourceConfidence::Known ? CaptureReason::Accepted : CaptureReason::AcceptedLimitedSource;
    return {CaptureDisposition::Accept, reason, std::move(out)};
}

std::string_view capture_reason_message(CaptureReason reason) {
    switch (reason) {
    case CaptureReason::Accepted: return "Capture accepted";
    case CaptureReason::AcceptedLimitedSource: return "Capture accepted; source identification is limited";
    case CaptureReason::CaptureUnavailable: return "Capture is unavailable in this environment";
    case CaptureReason::InvalidInput: return "Invalid capture metadata or limits";
    case CaptureReason::Transient: return "Temporary clipboard content is excluded";
    case CaptureReason::Concealed: return "Concealed clipboard content is excluded";
    case CaptureReason::RemoteClipboard: return "Apple remote clipboard content is excluded";
    case CaptureReason::RemotePrimaryFile: return "Apple remote primary file reference is excluded";
    case CaptureReason::ExcludedApplication: return "Capture matches an application exclusion";
    case CaptureReason::UnknownSource: return "Source is unknown under the configured exclusion policy";
    case CaptureReason::NoEnabledRepresentations: return "No enabled recognized representation remains";
    }
    return "Unknown capture decision";
}

Result<CaptureDecision> dispatch_capture(const ClipboardSnapshot& v, const CapturePolicy& p,
                                         const Capabilities& c, CaptureSink& sink) {
    auto decision = evaluate_capture(v, p, c);
    if (decision.disposition != CaptureDisposition::Accept) return decision;
    const auto& filtered = *decision.snapshot;
    auto stored = sink.store(filtered);
    if (!stored) return stored.error();
    if (stored.value()) {
        const bool image = std::any_of(filtered.representations.begin(), filtered.representations.end(),
                                      [](const auto& r) { return r.kind == CanonicalKind::Image; });
        if (image) {
            sink.thumbnail(*stored.value(), filtered);
            if (p.request_ocr && c.ocr.status != CapabilityStatus::Unavailable) sink.ocr(*stored.value(), filtered);
        }
    }
    return decision;
}
}
