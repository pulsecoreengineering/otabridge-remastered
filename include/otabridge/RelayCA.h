#pragma once

// Root CA pinned for the relay's TLS certificate — see src/core/RelayCA.cpp
// for the PEM itself and how it was verified. Declared extern (defined once
// in RelayCA.cpp) rather than inline in this header so the ~1.8KB PEM isn't
// duplicated across every translation unit that includes RelayClient.h.
extern const char OTABRIDGE_RELAY_CA_CERT[];
