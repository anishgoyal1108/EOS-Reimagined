#ifndef EOSR_MANAGER_SHA256_H
#define EOSR_MANAGER_SHA256_H

#include <cstddef>
#include <string>

namespace eosr {
namespace manager {

// A small incremental SHA-256 implementation used for manager ownership checks. Keeping an
// incremental state lets platform file adapters hash large SDK files without loading them twice.
class sha256_hasher {
public:
    sha256_hasher();

    void update(const unsigned char* data, std::size_t size);
    void update(const std::string& data);
    std::string final_hex();

private:
    void compress(const unsigned char block[64]);

    unsigned int state_[8];
    unsigned char buffer_[64];
    std::size_t buffer_size_;
    unsigned long long byte_count_;
    bool finalized_;
};

std::string sha256_hex(const unsigned char* data, std::size_t size);
std::string sha256_hex(const std::string& data);

} // namespace manager
} // namespace eosr

#endif
