#pragma once
#include "types.hpp"

namespace cliptiq {
enum class CapabilityStatus { Available, Limited, Unavailable };
struct Capability {
    CapabilityStatus status = CapabilityStatus::Unavailable;
    std::string reason = "Not probed";
};
enum class CapturePlatform { Unknown, MacOS, Windows, X11, Wayland };
struct NativeTypeSupport {
    std::string platform, native_type;
    CanonicalKind kind = CanonicalKind::Opaque;
    TextEncoding text_encoding = TextEncoding::Utf8;
};
struct Capabilities {
    CapturePlatform platform = CapturePlatform::Unknown;
    Capability capture, write, hotkeys, auto_paste, source_app, tray, pointer_placement, ocr;
    // Trusted backend configuration, never populated from untrusted clipboard data.
    std::vector<NativeTypeSupport> native_types;
};
}
