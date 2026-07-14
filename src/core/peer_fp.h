#ifndef EOSR_CORE_PEER_FP_H
#define EOSR_CORE_PEER_FP_H

#include <string>

namespace eosr {

// The cross-process peer fingerprint: a stable, pseudonymous label two independent trace tools
// compute identically for the same peer, without either file handing back the raw id.
//
// Construction (docs/alpha-tracing.md §4, one exact recipe so both ends agree):
//   input  = ASCII "eosr-trace-peer-v1" (18 bytes) immediately followed by the peer's product user
//            id as its 32 lowercase-hex ASCII bytes -- a fixed 50-byte concatenation, no separator.
//   digest = SHA-256 of that input.
//   result = the first 8 digest bytes as 16 lowercase hex characters.
//
// Returns the empty string if `product_user_id` is not exactly 32 lowercase-hex characters, so a
// malformed or absent id yields no fingerprint rather than one over the wrong bytes.
// Golden vector: 00112233445566778899aabbccddeeff -> ebf65ed621ba531b.
std::string peer_fingerprint(const std::string& product_user_id);

} // namespace eosr

#endif
