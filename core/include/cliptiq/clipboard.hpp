#pragma once
#include "policy.hpp"
#include <functional>

namespace cliptiq {
using CaptureCallback = std::function<void(Result<ClipboardSnapshot>)>;
constexpr std::uint32_t RootPasteboardItem = UINT32_MAX;
class ClipboardBackend {
public:
    virtual ~ClipboardBackend() = default;
    virtual Capabilities capabilities() const = 0;
    virtual Result<ClipboardSnapshot> read_snapshot() = 0;
    virtual Result<std::string> write_snapshot(const ClipboardSnapshot&) = 0;
    virtual Result<void> start(CaptureCallback) = 0;
    virtual void stop() = 0;
};
}
