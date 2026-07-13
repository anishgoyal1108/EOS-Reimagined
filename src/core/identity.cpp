#include "core/identity.h"

#include <cstring>
#include <fstream>
#include <vector>

#include "common/crypto.h"
#include "common/log.h"
#include "platform/rng.h"

namespace eosr {

namespace {

// An id is 16 bytes rendered as the 32 hex characters EOS uses, so we take the first half of a
// SHA-256 digest. The format is unchanged from the ids we derived before; only their source is.
const std::size_t id_bytes = 16;

// Domain separators keep the three hashes independent: nothing else that hashes the static key can
// land on a profile hash, and neither id can collide with the other. A change to any derivation
// bumps its suffix.
const char* const profile_domain = "eosr-profile-v1";
const char* const eaid_domain = "eosr-eaid-v1";
const char* const puid_domain = "eosr-puid-v1";

const char* const profile_comment =
    "# EOS Reimagined profile. This key is your identity: keep it secret, and back it up -- there\n"
    "# is nobody who can reissue it. Copy this file to another machine to be the same player there.";

std::string to_hex(const u8* data, std::size_t len) {
    static const char digits[] = "0123456789abcdef";
    std::string out;
    out.reserve(len * 2);
    for (std::size_t i = 0; i < len; i++) {
        out.push_back(digits[(data[i] >> 4) & 0xf]);
        out.push_back(digits[data[i] & 0xf]);
    }
    return out;
}

bool hex_digit(char character, int& value) {
    if (character >= '0' && character <= '9') {
        value = character - '0';
    } else if (character >= 'a' && character <= 'f') {
        value = 10 + (character - 'a');
    } else if (character >= 'A' && character <= 'F') {
        value = 10 + (character - 'A');
    } else {
        return false;
    }
    return true;
}

bool from_hex(const std::string& text, u8* out, std::size_t len) {
    if (text.size() != len * 2) {
        return false;
    }
    for (std::size_t i = 0; i < len; i++) {
        int high = 0;
        int low = 0;
        if (!hex_digit(text[i * 2], high) || !hex_digit(text[i * 2 + 1], low)) {
            return false;
        }
        out[i] = static_cast<u8>((high << 4) | low);
    }
    return true;
}

// The one hash of the static key that both ids hang off.
std::vector<u8> profile_hash(const u8* public_key) {
    canonical_encoder encoder;
    encoder.field(profile_domain).field(public_key, profile_key_len);
    return sha256(encoder.data().data(), encoder.data().size());
}

std::string id_from(const canonical_encoder& encoder) {
    const std::vector<u8> digest = sha256(encoder.data().data(), encoder.data().size());
    return to_hex(digest.data(), id_bytes);
}

std::string profile_path(const std::string& directory, int slot, const char* extension) {
    // Slot zero is the ordinary single-instance case, so it gets the plain name a user can be
    // pointed at; the extra slots exist for the other local copies of the game.
    std::string name = "profile";
    if (slot != 0) {
        name += "-";
        name += static_cast<char>('0' + slot);
    }
    return directory + "/" + name + extension;
}

bool read_profile(const std::string& path, u8* secret) {
    std::ifstream file(path.c_str(), std::ios::binary);
    if (!file) {
        return false;
    }
    std::string line;
    while (std::getline(file, line)) {
        // Tolerate comments and CRLF so a profile survives being read, edited, or carried between a
        // Windows machine and a Linux one.
        while (!line.empty() && (line[line.size() - 1] == '\r' || line[line.size() - 1] == ' ')) {
            line.erase(line.size() - 1);
        }
        const std::string prefix = "key ";
        if (line.empty() || line[0] == '#' || line.compare(0, prefix.size(), prefix) != 0) {
            continue;
        }
        return from_hex(line.substr(prefix.size()), secret, profile_key_len);
    }
    return false;
}

bool write_profile(const std::string& path, const u8* secret) {
    std::string text = profile_comment;
    text += "\nkey ";
    text += to_hex(secret, profile_key_len);
    text += "\n";
    return platform::write_private_file(path, text);
}

} // namespace

std::string derive_epic_account_id(const u8 public_key[profile_key_len]) {
    const std::vector<u8> profile = profile_hash(public_key);
    canonical_encoder encoder;
    encoder.field(eaid_domain).field(profile.data(), profile.size());
    return id_from(encoder);
}

std::string derive_product_user_id(const u8 public_key[profile_key_len],
                                   const std::string& product_id, const std::string& sandbox_id,
                                   const std::string& deployment_id) {
    const std::vector<u8> profile = profile_hash(public_key);
    canonical_encoder encoder;
    encoder.field(puid_domain)
        .field(profile.data(), profile.size())
        .field(product_id)
        .field(sandbox_id)
        .field(deployment_id);
    return id_from(encoder);
}

identity::identity() : has_key_(false), persistent_(false) {
    std::memset(secret_, 0, sizeof(secret_));
    std::memset(public_, 0, sizeof(public_));
}

identity::~identity() {
    secure_wipe(secret_, sizeof(secret_));
}

bool identity::adopt_key(const u8 secret[profile_key_len]) {
    // An all-zero secret is not a key: X25519 clamping would turn it into a low-order point, and
    // every instance that "minted" one would answer to the same identity. Refuse it, so a truncated
    // or zeroed profile file is treated as no profile at all rather than a shared one.
    u8 bits = 0;
    for (std::size_t i = 0; i < profile_key_len; i++) {
        bits |= secret[i];
    }
    if (bits == 0) {
        return false;
    }
    std::memcpy(secret_, secret, profile_key_len);
    x25519_public_key(public_, secret_);
    has_key_ = true;
    rederive();
    return true;
}

bool identity::generate_ephemeral() {
    u8 secret[profile_key_len] = {0};
    const bool ok = platform::random_bytes(secret, profile_key_len) && adopt_key(secret);
    secure_wipe(secret, sizeof(secret));
    persistent_ = false;
    return ok;
}

bool identity::load_or_create(const std::string& directory) {
    if (directory.empty() || !platform::make_directories(directory)) {
        return false;
    }
    for (int slot = 0; slot < max_local_profiles; slot++) {
        // A slot another local copy of the game already holds is not an error: it just got there
        // first, and we move to the next one and keep an identity of our own.
        if (!slot_.acquire(profile_path(directory, slot, ".lock"))) {
            continue;
        }
        const std::string key_path = profile_path(directory, slot, ".key");

        u8 secret[profile_key_len] = {0};
        if (read_profile(key_path, secret) && adopt_key(secret)) {
            secure_wipe(secret, sizeof(secret));
            persistent_ = true;
            return true;
        }
        // Nothing there, or what is there is not a key. Mint one and keep it, so this instance is
        // the same player the next time it runs.
        if (!platform::random_bytes(secret, profile_key_len) || !adopt_key(secret)) {
            secure_wipe(secret, sizeof(secret));
            slot_.release();
            return false;
        }
        persistent_ = write_profile(key_path, secret);
        secure_wipe(secret, sizeof(secret));
        if (!persistent_) {
            log_warn("identity: the profile could not be written; this identity lasts only for "
                     "this run");
        }
        return true;
    }
    return false;
}

void identity::bind_product(const std::string& product_id, const std::string& sandbox_id,
                            const std::string& deployment_id) {
    product_id_ = product_id;
    sandbox_id_ = sandbox_id;
    deployment_id_ = deployment_id;
    rederive();
}

void identity::rederive() {
    if (!has_key_) {
        epic_account_id_.clear();
        product_user_id_.clear();
        return;
    }
    epic_account_id_ = derive_epic_account_id(public_);
    product_user_id_ = derive_product_user_id(public_, product_id_, sandbox_id_, deployment_id_);
}

std::string identity::export_key() const {
    if (!has_key_) {
        return std::string();
    }
    return to_hex(secret_, profile_key_len);
}

bool identity::import_key(const std::string& hex) {
    u8 secret[profile_key_len] = {0};
    const bool ok = from_hex(hex, secret, profile_key_len) && adopt_key(secret);
    secure_wipe(secret, sizeof(secret));
    if (ok) {
        // Importing replaces the key we are holding, not the one on disk -- which, if there is one,
        // is still the old key and is what the next run will load. Saying the identity is persistent
        // would promise a durability it does not have, and the player would silently be somebody
        // else tomorrow. Writing it out is a separate, deliberate act.
        persistent_ = false;
    }
    return ok;
}

} // namespace eosr
