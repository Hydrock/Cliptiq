#pragma once
#include "cliptiq/types.hpp"
namespace cliptiq::lan {
// Initialize on the UI thread before starting any crypto worker/context.
Result<void> initialize_crypto_runtime();
}
