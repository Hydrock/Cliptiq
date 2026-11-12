#pragma once
#include "cliptiq/types.hpp"
namespace cliptiq::lan {
// Worker thread only. Bounded decode; no thumbnail substitution or file I/O.
Result<Bytes> network_png(const Representation&);
}
