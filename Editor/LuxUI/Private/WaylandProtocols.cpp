// Both generated declarations precede the definitions so the C protocol symbols retain external
// linkage when this wrapper is compiled by an Editor-only CXX project.
#include <wayland-client.h>
#if defined(__GNUC__)
#pragma GCC visibility push(hidden)
#endif
#include "pointer-constraints-client-protocol.h"
#include "relative-pointer-client-protocol.h"
#if defined(__GNUC__)
#pragma GCC visibility pop
// The scanner places visibility on C const definitions. GCC ignores that attribute in C++,
// so visibility is already set on the preceding declarations instead. Scope the warning to
// generated protocol code, which is otherwise kept byte-for-byte as emitted by the scanner.
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wattributes"
#endif

extern "C" {
#include "pointer-constraints-protocol.inc"
#include "relative-pointer-protocol.inc"
}
#if defined(__GNUC__)
#pragma GCC diagnostic pop
#endif
