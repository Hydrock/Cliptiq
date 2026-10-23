#pragma once
#include "Clipboard.hpp"
namespace cliptiq::windows {
// Structural validation only; compressed image decode belongs to the worker.
Result<void> validate_native_payload(Representation&);
}
