#pragma once
#include "Clipboard.hpp"
namespace cliptiq::windows {
// Structural validation only; compressed image decode belongs to the worker.
Result<void> validate_native_payload(Representation&);
// Standard Windows bitmap fallback; original PNG remains available with full alpha.
Result<Bytes> png_clipboard_dib(const Representation&, std::uint64_t max_bytes);
}
