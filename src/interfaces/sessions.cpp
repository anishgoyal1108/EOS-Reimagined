#include "interfaces/sessions.h"

#include <cstring>
#include <memory>
#include <mutex>

#include "common/ids.h"
#include "common/log.h"
#include "core/callback_manager.h"
#include "core/settings.h"
#include "interfaces/connect.h"
#include "net/message_router.h"
#include "net/wire.h"
#include "platform/rng.h"

namespace eosr {

namespace {

const callback_type_id cb_update = 1;
const callback_type_id cb_destroy = 2;
const callback_type_id cb_join = 3;
const callback_type_id cb_start = 4;
const callback_type_id cb_end = 5;
const callback_type_id cb_register = 6;
const callback_type_id cb_unregister = 7;
const callback_type_id cb_find = 8;
const callback_type_id cb_stub = 9;
const callback_type_id cb_notification = 10;

// A search or a join that never hears back cannot hang forever.
const std::chrono::milliseconds search_timeout(5000);
const std::chrono::milliseconds join_timeout(5000);

// Well-known keys a searcher may use that are not session attributes at all but questions about
// the session itself. The reference emulator looked them up as attributes, failed to find them,
// and so matched nothing; we answer them properly.
const char* const key_bucket = EOS_SESSIONS_SEARCH_BUCKET_ID;
const char* const key_empty_only = EOS_SESSIONS_SEARCH_EMPTY_SERVERS_ONLY;
const char* const key_nonempty_only = EOS_SESSIONS_SEARCH_NONEMPTY_SERVERS_ONLY;
const char* const key_min_slots = EOS_SESSIONS_SEARCH_MINSLOTSAVAILABLE;

struct common_completion_prefix {
    EOS_EResult result_code;
    void* client_data;
};

// --- the structs CopyInfo hands the game, and the strings they point at ---

struct details_info_holder {
    EOS_SessionDetails_Info info;
    EOS_SessionDetails_Settings settings;
    std::string session_id;
    std::string host_address;
    std::string bucket_id;
};

struct active_info_holder {
    EOS_ActiveSession_Info info;
    details_info_holder details;
    std::string session_name;
};

struct attribute_holder {
    EOS_SessionDetails_Attribute attribute;
    EOS_Sessions_AttributeData data;
    std::string key;
    std::string value;
};

// These are freed through free functions that get no handle, so they outlive any one platform and
// live here. A pointer we never issued is ignored rather than passed to free().
std::mutex g_info_mutex;
std::map<void*, std::unique_ptr<details_info_holder> > g_details_infos;
std::map<void*, std::unique_ptr<active_info_holder> > g_active_infos;
std::map<void*, std::unique_ptr<attribute_holder> > g_attributes;

// The callback structs hand the game a char*, so the text has to outlive the call. These are freed
// in free_callback when the result is done with.
const char* duplicate(const std::string& text) {
    char* copy = new char[text.size() + 1];
    std::memcpy(copy, text.c_str(), text.size() + 1);
    return copy;
}

// The register/unregister callbacks hand the game an array of the players the call actually
// changed. The ids are interned handles, so the array only holds pointers; it is freed in
// free_callback when the result is done with.
EOS_ProductUserId* alloc_player_array(const std::vector<std::string>& ids) {
    if (ids.empty()) {
        return 0;
    }
    EOS_ProductUserId* out = new EOS_ProductUserId[ids.size()];
    for (std::size_t i = 0; i < ids.size(); i++) {
        out[i] = id_registry::instance().get_product_user_id(ids[i]);
    }
    return out;
}

// A session needs an id nobody else will pick. The same source the identity is minted from does.
std::string generate_session_id() {
    static const char digits[] = "0123456789abcdef";
    u8 bytes[16];
    if (!platform::random_bytes(bytes, sizeof(bytes))) {
        // Never hand back an id we cannot tell apart from another session's.
        return std::string();
    }
    std::string out;
    out.reserve(32);
    for (std::size_t i = 0; i < sizeof(bytes); i++) {
        out += digits[(bytes[i] >> 4) & 0xf];
        out += digits[bytes[i] & 0xf];
    }
    return out;
}

bool version_ok(i32 version, i32 latest) {
    return version > 0 && version <= latest;
}

std::string text_or_empty(const char* value) {
    return (value != 0) ? value : std::string();
}

// Copy one EOS attribute into the shape we keep and put on the wire.
bool read_attribute(const EOS_Sessions_AttributeData* data, session_attribute& out) {
    if (data == 0 || data->Key == 0 || data->Key[0] == '\0') {
        return false;
    }
    if (std::strlen(data->Key) > EOS_SESSIONMODIFICATION_MAX_SESSION_ATTRIBUTE_LENGTH) {
        return false;
    }
    out.key = data->Key;
    out.value_type = static_cast<i32>(data->ValueType);
    switch (data->ValueType) {
        case EOS_EAttributeType::EOS_AT_BOOLEAN:
            out.as_bool = (data->Value.AsBool == EOS_TRUE);
            return true;
        case EOS_EAttributeType::EOS_AT_INT64:
            out.as_int64 = data->Value.AsInt64;
            return true;
        case EOS_EAttributeType::EOS_AT_DOUBLE:
            out.as_double = data->Value.AsDouble;
            return true;
        case EOS_EAttributeType::EOS_AT_STRING:
            if (data->Value.AsUtf8 == 0) {
                return false;
            }
            out.as_string = data->Value.AsUtf8;
            return true;
        default:
            return false;
    }
}

// Fill an EOS attribute from ours, borrowing `storage` for a string value.
void write_attribute(const session_attribute& from, EOS_Sessions_AttributeData& out,
                     std::string& key_storage, std::string& value_storage) {
    key_storage = from.key;
    out.ApiVersion = EOS_SESSIONS_ATTRIBUTEDATA_API_LATEST;
    out.Key = key_storage.c_str();
    out.ValueType = static_cast<EOS_EAttributeType>(from.value_type);
    switch (from.value_type) {
        case 0: out.Value.AsBool = from.as_bool ? EOS_TRUE : EOS_FALSE; break;
        case 1: out.Value.AsInt64 = from.as_int64; break;
        case 2: out.Value.AsDouble = from.as_double; break;
        default:
            value_storage = from.as_string;
            out.Value.AsUtf8 = value_storage.c_str();
            break;
    }
}

template <class value_type>
bool compare_ordered(value_type mine, value_type theirs, i32 op) {
    switch (op) {
        case 0: return mine == theirs;  // EOS_CO_EQUAL
        case 1: return mine != theirs;  // EOS_CO_NOTEQUAL
        case 2: return mine > theirs;   // EOS_CO_GREATERTHAN
        case 3: return mine >= theirs;  // EOS_CO_GREATERTHANOREQUAL
        case 4: return mine < theirs;   // EOS_CO_LESSTHAN
        case 5: return mine <= theirs;  // EOS_CO_LESSTHANOREQUAL
        // Distance, any-of, contains and the rest are not implemented. We let them match rather
        // than silently returning nothing to a game that uses one.
        default: return true;
    }
}

bool compare_attribute(const session_attribute& mine, const session_attribute& theirs, i32 op) {
    if (mine.value_type != theirs.value_type) {
        return false; // a type mismatch is not a comparison, it is a different question
    }
    switch (mine.value_type) {
        case 0: return compare_ordered<i64>(mine.as_bool ? 1 : 0, theirs.as_bool ? 1 : 0, op);
        case 1: return compare_ordered<i64>(mine.as_int64, theirs.as_int64, op);
        case 2: return compare_ordered<f64>(mine.as_double, theirs.as_double, op);
        default: return compare_ordered<std::string>(mine.as_string, theirs.as_string, op);
    }
}

bool contains(const std::vector<std::string>& list, const std::string& value) {
    for (std::size_t i = 0; i < list.size(); i++) {
        if (list[i] == value) {
            return true;
        }
    }
    return false;
}

void remove_from(std::vector<std::string>& list, const std::string& value) {
    for (std::size_t i = 0; i < list.size(); i++) {
        if (list[i] == value) {
            list.erase(list.begin() + i);
            return;
        }
    }
}

// The header defines open connections as the public capacity minus the players actually
// registered — not minus everyone present, which is what the reference emulator used.
u32 open_slots_of(const session_infos& infos) {
    const std::size_t taken = infos.registered_players.size();
    return (taken >= infos.max_players) ? 0 : infos.max_players - static_cast<u32>(taken);
}

void fill_details_info(details_info_holder& holder, const session_infos& infos) {
    holder.session_id = infos.session_id;
    holder.host_address = infos.host_address;
    holder.bucket_id = infos.bucket_id;

    holder.settings.ApiVersion = EOS_SESSIONDETAILS_SETTINGS_API_LATEST;
    holder.settings.BucketId = holder.bucket_id.c_str();
    holder.settings.NumPublicConnections = infos.max_players;
    holder.settings.bAllowJoinInProgress = infos.allow_join_in_progress ? EOS_TRUE : EOS_FALSE;
    holder.settings.PermissionLevel =
        static_cast<EOS_EOnlineSessionPermissionLevel>(infos.permission_level);
    holder.settings.bInvitesAllowed = infos.invites_allowed ? EOS_TRUE : EOS_FALSE;
    holder.settings.bSanctionsEnabled = infos.sanctions_enabled ? EOS_TRUE : EOS_FALSE;
    holder.settings.AllowedPlatformIds = 0;
    holder.settings.AllowedPlatformIdsCount = 0;

    holder.info.ApiVersion = EOS_SESSIONDETAILS_INFO_API_LATEST;
    holder.info.SessionId = holder.session_id.c_str();
    holder.info.HostAddress = holder.host_address.c_str();
    holder.info.NumOpenPublicConnections = open_slots_of(infos);
    holder.info.Settings = &holder.settings;
    holder.info.OwnerUserId = infos.owner_id.empty()
                                  ? 0
                                  : id_registry::instance().get_product_user_id(infos.owner_id);
    holder.info.OwnerServerClientId = 0;
}

} // namespace

sdk_sessions::sdk_sessions(sdk_settings& settings, callback_manager& callbacks,
                           message_router& network, sdk_connect& connect)
    : settings_(settings),
      callbacks_(callbacks),
      network_(network),
      connect_(connect),
      next_search_id_(1),
      registered_(false) {
}

sdk_sessions::~sdk_sessions() {
    emu_deinit();
}

void sdk_sessions::emu_init() {
    if (registered_) {
        return;
    }
    callbacks_.register_frame(this);
    callbacks_.register_callbacks(this);
    network_.register_listener(message_type::session_search, this);
    network_.register_listener(message_type::session_search_response, this);
    network_.register_listener(message_type::session_join_request, this);
    network_.register_listener(message_type::session_join_response, this);
    network_.register_listener(message_type::session_infos, this);
    network_.register_listener(message_type::session_destroy, this);
    network_.register_listener(message_type::session_register, this);
    network_.register_listener(message_type::session_unregister, this);
    network_.register_listener(message_type::peer_disconnected, this);
    registered_ = true;
}

void sdk_sessions::emu_deinit() {
    if (!registered_) {
        return;
    }
    network_.unregister_listener(message_type::session_search, this);
    network_.unregister_listener(message_type::session_search_response, this);
    network_.unregister_listener(message_type::session_join_request, this);
    network_.unregister_listener(message_type::session_join_response, this);
    network_.unregister_listener(message_type::session_infos, this);
    network_.unregister_listener(message_type::session_destroy, this);
    network_.unregister_listener(message_type::session_register, this);
    network_.unregister_listener(message_type::session_unregister, this);
    network_.unregister_listener(message_type::peer_disconnected, this);
    callbacks_.unregister_callbacks(this);
    callbacks_.unregister_frame(this);

    sessions_.clear();
    modifications_.clear();
    searches_.clear();
    details_.clear();
    actives_.clear();
    pending_finds_.clear();
    pending_joins_.clear();
    registered_ = false;
}

sdk_sessions::session* sdk_sessions::find_by_name(const std::string& name) {
    std::map<std::string, session>::iterator it = sessions_.find(name);
    return (it != sessions_.end()) ? &it->second : 0;
}

const sdk_sessions::session* sdk_sessions::find_by_name(const std::string& name) const {
    std::map<std::string, session>::const_iterator it = sessions_.find(name);
    return (it != sessions_.end()) ? &it->second : 0;
}

sdk_sessions::session* sdk_sessions::find_by_id(const std::string& session_id) {
    // The wire only ever carries the session id; the name is ours alone.
    std::map<std::string, session>::iterator it = sessions_.begin();
    for (; it != sessions_.end(); ++it) {
        if (it->second.infos.session_id == session_id) {
            return &it->second;
        }
    }
    return 0;
}

const sdk_sessions::session* sdk_sessions::find_by_id(const std::string& session_id) const {
    std::map<std::string, session>::const_iterator it = sessions_.begin();
    for (; it != sessions_.end(); ++it) {
        if (it->second.infos.session_id == session_id) {
            return &it->second;
        }
    }
    return 0;
}

void sdk_sessions::send_to(const std::string& peer, message_type type, const byte_writer& payload) {
    net_envelope envelope;
    envelope.type_tag = static_cast<u16>(type);
    envelope.source_id = settings_.product_user_id();
    envelope.dest_id = peer;
    envelope.game_id = settings_.product_id();
    envelope.payload = payload.data();
    network_.send(envelope);
}

void sdk_sessions::send_to_members(const session& entry, message_type type,
                                   const byte_writer& payload, const std::string& except) {
    for (std::size_t i = 0; i < entry.infos.registered_players.size(); i++) {
        const std::string& player = entry.infos.registered_players[i];
        if (player == settings_.product_user_id() || player == except) {
            continue;
        }
        send_to(player, type, payload);
    }
}

void sdk_sessions::broadcast_session(const session& entry) {
    byte_writer writer;
    serialize(writer, entry.infos);
    send_to_members(entry, message_type::session_infos, writer, std::string());
}

void sdk_sessions::deliver(callback_type_id type, std::size_t info_size,
                           completion_delegate delegate, void* client_data, EOS_EResult code) {
    if (delegate == 0) {
        return;
    }
    std::unique_ptr<frame_result> result(new frame_result());
    void* payload = result->create_callback(type, info_size, delegate);
    common_completion_prefix* prefix = static_cast<common_completion_prefix*>(payload);
    prefix->result_code = code;
    prefix->client_data = client_data;
    result->set_done(true);
    callbacks_.add_callback(this, std::move(result));
}

void sdk_sessions::queue_stub_result(void* client_data, completion_delegate delegate,
                                     std::size_t info_size) {
    deliver(cb_stub, info_size, delegate, client_data, EOS_EResult::EOS_NotImplemented);
}

EOS_NotificationId sdk_sessions::add_stub_notification(void* client_data,
                                                       completion_delegate delegate,
                                                       std::size_t info_size) {
    if (delegate == 0) {
        return EOS_INVALID_NOTIFICATIONID;
    }
    std::unique_ptr<frame_result> result(new frame_result());
    void* payload = result->create_callback(cb_notification, info_size, delegate);
    // Every notification info begins with ClientData; the rest is filled at the event, which for
    // the invite path does not happen yet.
    void** client = static_cast<void**>(payload);
    *client = client_data;
    return callbacks_.add_notification(this, std::move(result));
}

void sdk_sessions::remove_notification(EOS_NotificationId id) {
    callbacks_.remove_notification(this, id);
}

// --- Session modification ---

EOS_EResult sdk_sessions::create_session_modification(
    const EOS_Sessions_CreateSessionModificationOptions* options, EOS_HSessionModification* out) {
    if (out == 0) {
        return EOS_EResult::EOS_InvalidParameters;
    }
    *out = 0;
    if (options == 0 ||
        !version_ok(options->ApiVersion, EOS_SESSIONS_CREATESESSIONMODIFICATION_API_LATEST) ||
        options->SessionName == 0 || options->SessionName[0] == '\0' || options->BucketId == 0 ||
        options->LocalUserId == 0) {
        return EOS_EResult::EOS_InvalidParameters;
    }
    // A game may pin the session id, but only within the length the SDK allows.
    const std::string override_id = text_or_empty(options->SessionId);
    if (!override_id.empty() &&
        (override_id.size() < EOS_SESSIONMODIFICATION_MIN_SESSIONIDOVERRIDE_LENGTH ||
         override_id.size() > EOS_SESSIONMODIFICATION_MAX_SESSIONIDOVERRIDE_LENGTH)) {
        return EOS_EResult::EOS_InvalidParameters;
    }

    std::unique_ptr<modification_object> object(new modification_object());
    object->session_name = options->SessionName;
    object->local_user = options->LocalUserId->id_str;
    object->creating = true;
    object->infos.session_id = override_id;
    object->infos.bucket_id = options->BucketId;
    object->infos.max_players = options->MaxPlayers;
    object->infos.host_address = "127.0.0.1";
    object->infos.sanctions_enabled = (options->bSanctionsEnabled == EOS_TRUE);
    *out = reinterpret_cast<EOS_HSessionModification>(modifications_.add(std::move(object)));
    return EOS_EResult::EOS_Success;
}

EOS_EResult sdk_sessions::update_session_modification(
    const EOS_Sessions_UpdateSessionModificationOptions* options, EOS_HSessionModification* out) {
    if (out == 0) {
        return EOS_EResult::EOS_InvalidParameters;
    }
    *out = 0;
    if (options == 0 ||
        !version_ok(options->ApiVersion, EOS_SESSIONS_UPDATESESSIONMODIFICATION_API_LATEST) ||
        options->SessionName == 0 || options->SessionName[0] == '\0') {
        return EOS_EResult::EOS_InvalidParameters;
    }
    session* entry = find_by_name(options->SessionName);
    if (entry == 0) {
        return EOS_EResult::EOS_NotFound;
    }

    // Modifying starts from what the session already is, so a game that changes one field does not
    // silently blank the rest.
    std::unique_ptr<modification_object> object(new modification_object());
    object->session_name = options->SessionName;
    object->local_user = entry->local_user;
    object->creating = false;
    object->infos = entry->infos;
    *out = reinterpret_cast<EOS_HSessionModification>(modifications_.add(std::move(object)));
    return EOS_EResult::EOS_Success;
}

EOS_EResult sdk_sessions::modification_set_bucket_id(
    void* handle, const EOS_SessionModification_SetBucketIdOptions* options) {
    modification_object* object = modifications_.find(handle);
    if (object == 0 || options == 0 ||
        !version_ok(options->ApiVersion, EOS_SESSIONMODIFICATION_SETBUCKETID_API_LATEST) ||
        options->BucketId == 0) {
        return EOS_EResult::EOS_InvalidParameters;
    }
    object->infos.bucket_id = options->BucketId;
    return EOS_EResult::EOS_Success;
}

EOS_EResult sdk_sessions::modification_set_host_address(
    void* handle, const EOS_SessionModification_SetHostAddressOptions* options) {
    modification_object* object = modifications_.find(handle);
    if (object == 0 || options == 0 ||
        !version_ok(options->ApiVersion, EOS_SESSIONMODIFICATION_SETHOSTADDRESS_API_LATEST) ||
        options->HostAddress == 0) {
        return EOS_EResult::EOS_InvalidParameters;
    }
    object->infos.host_address = options->HostAddress;
    return EOS_EResult::EOS_Success;
}

EOS_EResult sdk_sessions::modification_set_permission_level(
    void* handle, const EOS_SessionModification_SetPermissionLevelOptions* options) {
    modification_object* object = modifications_.find(handle);
    if (object == 0 || options == 0 ||
        !version_ok(options->ApiVersion, EOS_SESSIONMODIFICATION_SETPERMISSIONLEVEL_API_LATEST)) {
        return EOS_EResult::EOS_InvalidParameters;
    }
    object->infos.permission_level = static_cast<i32>(options->PermissionLevel);
    return EOS_EResult::EOS_Success;
}

EOS_EResult sdk_sessions::modification_set_join_in_progress(
    void* handle, const EOS_SessionModification_SetJoinInProgressAllowedOptions* options) {
    modification_object* object = modifications_.find(handle);
    if (object == 0 || options == 0 ||
        !version_ok(options->ApiVersion,
                    EOS_SESSIONMODIFICATION_SETJOININPROGRESSALLOWED_API_LATEST)) {
        return EOS_EResult::EOS_InvalidParameters;
    }
    object->infos.allow_join_in_progress = (options->bAllowJoinInProgress == EOS_TRUE);
    return EOS_EResult::EOS_Success;
}

EOS_EResult sdk_sessions::modification_set_max_players(
    void* handle, const EOS_SessionModification_SetMaxPlayersOptions* options) {
    modification_object* object = modifications_.find(handle);
    if (object == 0 || options == 0 ||
        !version_ok(options->ApiVersion, EOS_SESSIONMODIFICATION_SETMAXPLAYERS_API_LATEST)) {
        return EOS_EResult::EOS_InvalidParameters;
    }
    object->infos.max_players = options->MaxPlayers;
    return EOS_EResult::EOS_Success;
}

EOS_EResult sdk_sessions::modification_set_invites_allowed(
    void* handle, const EOS_SessionModification_SetInvitesAllowedOptions* options) {
    modification_object* object = modifications_.find(handle);
    if (object == 0 || options == 0 ||
        !version_ok(options->ApiVersion, EOS_SESSIONMODIFICATION_SETINVITESALLOWED_API_LATEST)) {
        return EOS_EResult::EOS_InvalidParameters;
    }
    object->infos.invites_allowed = (options->bInvitesAllowed == EOS_TRUE);
    return EOS_EResult::EOS_Success;
}

EOS_EResult sdk_sessions::modification_add_attribute(
    void* handle, const EOS_SessionModification_AddAttributeOptions* options) {
    modification_object* object = modifications_.find(handle);
    if (object == 0 || options == 0 ||
        !version_ok(options->ApiVersion, EOS_SESSIONMODIFICATION_ADDATTRIBUTE_API_LATEST)) {
        return EOS_EResult::EOS_InvalidParameters;
    }
    session_attribute attribute;
    if (!read_attribute(options->SessionAttribute, attribute)) {
        return EOS_EResult::EOS_InvalidParameters;
    }
    attribute.advertisement = static_cast<i32>(options->AdvertisementType);

    // Replacing an attribute we already carry is not growth, so the cap only bites on a new key.
    for (std::size_t i = 0; i < object->infos.attributes.size(); i++) {
        if (object->infos.attributes[i].key == attribute.key) {
            object->infos.attributes[i] = attribute;
            return EOS_EResult::EOS_Success;
        }
    }
    if (object->infos.attributes.size() >= EOS_SESSIONMODIFICATION_MAX_SESSION_ATTRIBUTES) {
        return EOS_EResult::EOS_LimitExceeded;
    }
    object->infos.attributes.push_back(attribute);
    return EOS_EResult::EOS_Success;
}

EOS_EResult sdk_sessions::modification_remove_attribute(
    void* handle, const EOS_SessionModification_RemoveAttributeOptions* options) {
    modification_object* object = modifications_.find(handle);
    if (object == 0 || options == 0 ||
        !version_ok(options->ApiVersion, EOS_SESSIONMODIFICATION_REMOVEATTRIBUTE_API_LATEST) ||
        options->Key == 0) {
        return EOS_EResult::EOS_InvalidParameters;
    }
    for (std::size_t i = 0; i < object->infos.attributes.size(); i++) {
        if (object->infos.attributes[i].key == options->Key) {
            object->infos.attributes.erase(object->infos.attributes.begin() + i);
            return EOS_EResult::EOS_Success;
        }
    }
    return EOS_EResult::EOS_NotFound;
}

void sdk_sessions::modification_release(void* handle) {
    modifications_.release(handle);
}

// --- Session lifecycle ---

void sdk_sessions::update_session(const EOS_Sessions_UpdateSessionOptions* options,
                                  void* client_data, EOS_Sessions_OnUpdateSessionCallback delegate) {
    if (delegate == 0) {
        return;
    }
    modification_object* object =
        (options != 0) ? modifications_.find(options->SessionModificationHandle) : 0;
    if (options == 0 || !version_ok(options->ApiVersion, EOS_SESSIONS_UPDATESESSION_API_LATEST) ||
        object == 0) {
        deliver(cb_update, sizeof(EOS_Sessions_UpdateSessionCallbackInfo),
                reinterpret_cast<completion_delegate>(delegate), client_data,
                EOS_EResult::EOS_InvalidParameters);
        return;
    }

    EOS_EResult code = EOS_EResult::EOS_Success;
    session* entry = find_by_name(object->session_name);

    if (object->creating) {
        const std::string new_id =
            object->infos.session_id.empty() ? generate_session_id() : object->infos.session_id;
        if (entry != 0) {
            code = EOS_EResult::EOS_Sessions_SessionAlreadyExists;
        } else if (new_id.empty()) {
            // The platform RNG failed us; better to fail the create than to seat a session under an
            // empty id that would alias every other id-less lookup.
            code = EOS_EResult::EOS_UnexpectedError;
        } else {
            session created;
            created.infos = object->infos;
            created.local_user = object->local_user;
            created.local_state = session::hosting;
            created.infos.session_id = new_id;
            created.infos.owner_id = object->local_user;
            created.infos.state = static_cast<i32>(EOS_EOnlineSessionState::EOS_OSS_Pending);
            // The host is in its own session, and registered, which is what lets it later answer
            // a join and register others.
            created.infos.registered_players.push_back(object->local_user);
            created.infos.open_slots = open_slots_of(created.infos);
            sessions_[object->session_name] = created;
        }
    } else if (entry == 0) {
        code = EOS_EResult::EOS_NotFound;
    } else {
        // The id and the state belong to the session, not to a modification, so a game cannot
        // rewrite either by staging one.
        const std::string keep_id = entry->infos.session_id;
        const i32 keep_state = entry->infos.state;
        const std::vector<std::string> keep_registered = entry->infos.registered_players;
        entry->infos = object->infos;
        entry->infos.session_id = keep_id;
        entry->infos.state = keep_state;
        entry->infos.owner_id = entry->infos.owner_id.empty() ? entry->local_user : entry->infos.owner_id;
        entry->infos.registered_players = keep_registered;
        entry->infos.open_slots = open_slots_of(entry->infos);
        broadcast_session(*entry);
    }

    // The callback carries the name and the id, so both have to outlive the call.
    std::unique_ptr<frame_result> result(new frame_result());
    EOS_Sessions_UpdateSessionCallbackInfo* info =
        static_cast<EOS_Sessions_UpdateSessionCallbackInfo*>(
            result->create_callback(cb_update, sizeof(EOS_Sessions_UpdateSessionCallbackInfo),
                                    reinterpret_cast<completion_delegate>(delegate)));
    info->ResultCode = code;
    info->ClientData = client_data;
    const session* settled = find_by_name(object->session_name);
    info->SessionName = duplicate(object->session_name);
    info->SessionId = duplicate(settled != 0 ? settled->infos.session_id : std::string());
    result->set_done(true);
    callbacks_.add_callback(this, std::move(result));
}

void sdk_sessions::destroy_session(const EOS_Sessions_DestroySessionOptions* options,
                                   void* client_data,
                                   EOS_Sessions_OnDestroySessionCallback delegate) {
    if (delegate == 0) {
        return;
    }
    if (options == 0 || !version_ok(options->ApiVersion, EOS_SESSIONS_DESTROYSESSION_API_LATEST) ||
        options->SessionName == 0) {
        deliver(cb_destroy, sizeof(EOS_Sessions_DestroySessionCallbackInfo),
                reinterpret_cast<completion_delegate>(delegate), client_data,
                EOS_EResult::EOS_InvalidParameters);
        return;
    }
    session* entry = find_by_name(options->SessionName);
    if (entry == 0) {
        deliver(cb_destroy, sizeof(EOS_Sessions_DestroySessionCallbackInfo),
                reinterpret_cast<completion_delegate>(delegate), client_data,
                EOS_EResult::EOS_NotFound);
        return;
    }

    // Destroying a session we host ends it for everyone. Destroying one we merely joined is us
    // leaving, so we give the seat back instead — otherwise the host holds it for a player who
    // is standing in the menu, and a few rounds of joining and quitting fill the game with ghosts.
    byte_writer writer;
    if (entry->local_state == session::hosting) {
        session_destroy notice;
        notice.session_id = entry->infos.session_id;
        serialize(writer, notice);
        send_to_members(*entry, message_type::session_destroy, writer, std::string());
    } else {
        session_members notice;
        notice.session_id = entry->infos.session_id;
        notice.player_ids.push_back(settings_.product_user_id());
        serialize(writer, notice);
        send_to_members(*entry, message_type::session_unregister, writer, std::string());
    }
    sessions_.erase(options->SessionName);

    deliver(cb_destroy, sizeof(EOS_Sessions_DestroySessionCallbackInfo),
            reinterpret_cast<completion_delegate>(delegate), client_data, EOS_EResult::EOS_Success);
}

void sdk_sessions::start_session(const EOS_Sessions_StartSessionOptions* options, void* client_data,
                                 EOS_Sessions_OnStartSessionCallback delegate) {
    if (delegate == 0) {
        return;
    }
    if (options == 0 || !version_ok(options->ApiVersion, EOS_SESSIONS_STARTSESSION_API_LATEST) ||
        options->SessionName == 0) {
        deliver(cb_start, sizeof(EOS_Sessions_StartSessionCallbackInfo),
                reinterpret_cast<completion_delegate>(delegate), client_data,
                EOS_EResult::EOS_InvalidParameters);
        return;
    }
    session* entry = find_by_name(options->SessionName);
    if (entry == 0) {
        deliver(cb_start, sizeof(EOS_Sessions_StartSessionCallbackInfo),
                reinterpret_cast<completion_delegate>(delegate), client_data,
                EOS_EResult::EOS_NotFound);
        return;
    }
    // A session can only be started from the lobby before a match or from one that has ended.
    const i32 state = entry->infos.state;
    if (state != static_cast<i32>(EOS_EOnlineSessionState::EOS_OSS_Pending) &&
        state != static_cast<i32>(EOS_EOnlineSessionState::EOS_OSS_Ended)) {
        deliver(cb_start, sizeof(EOS_Sessions_StartSessionCallbackInfo),
                reinterpret_cast<completion_delegate>(delegate), client_data,
                EOS_EResult::EOS_InvalidParameters);
        return;
    }
    entry->infos.state = static_cast<i32>(EOS_EOnlineSessionState::EOS_OSS_InProgress);
    broadcast_session(*entry);
    deliver(cb_start, sizeof(EOS_Sessions_StartSessionCallbackInfo),
            reinterpret_cast<completion_delegate>(delegate), client_data, EOS_EResult::EOS_Success);
}

void sdk_sessions::end_session(const EOS_Sessions_EndSessionOptions* options, void* client_data,
                               EOS_Sessions_OnEndSessionCallback delegate) {
    if (delegate == 0) {
        return;
    }
    if (options == 0 || !version_ok(options->ApiVersion, EOS_SESSIONS_ENDSESSION_API_LATEST) ||
        options->SessionName == 0) {
        deliver(cb_end, sizeof(EOS_Sessions_EndSessionCallbackInfo),
                reinterpret_cast<completion_delegate>(delegate), client_data,
                EOS_EResult::EOS_InvalidParameters);
        return;
    }
    session* entry = find_by_name(options->SessionName);
    if (entry == 0) {
        deliver(cb_end, sizeof(EOS_Sessions_EndSessionCallbackInfo),
                reinterpret_cast<completion_delegate>(delegate), client_data,
                EOS_EResult::EOS_NotFound);
        return;
    }
    if (entry->infos.state != static_cast<i32>(EOS_EOnlineSessionState::EOS_OSS_InProgress)) {
        deliver(cb_end, sizeof(EOS_Sessions_EndSessionCallbackInfo),
                reinterpret_cast<completion_delegate>(delegate), client_data,
                EOS_EResult::EOS_InvalidParameters);
        return;
    }
    entry->infos.state = static_cast<i32>(EOS_EOnlineSessionState::EOS_OSS_Ended);
    // Members have to learn a match ended, just as they learn it started.
    broadcast_session(*entry);
    deliver(cb_end, sizeof(EOS_Sessions_EndSessionCallbackInfo),
            reinterpret_cast<completion_delegate>(delegate), client_data, EOS_EResult::EOS_Success);
}

void sdk_sessions::register_players(const EOS_Sessions_RegisterPlayersOptions* options,
                                    void* client_data,
                                    EOS_Sessions_OnRegisterPlayersCallback delegate) {
    if (delegate == 0) {
        return;
    }
    if (options == 0 || !version_ok(options->ApiVersion, EOS_SESSIONS_REGISTERPLAYERS_API_LATEST) ||
        options->SessionName == 0 ||
        (options->PlayersToRegisterCount > 0 && options->PlayersToRegister == 0)) {
        deliver(cb_register, sizeof(EOS_Sessions_RegisterPlayersCallbackInfo),
                reinterpret_cast<completion_delegate>(delegate), client_data,
                EOS_EResult::EOS_InvalidParameters);
        return;
    }
    session* entry = find_by_name(options->SessionName);
    if (entry == 0) {
        deliver(cb_register, sizeof(EOS_Sessions_RegisterPlayersCallbackInfo),
                reinterpret_cast<completion_delegate>(delegate), client_data,
                EOS_EResult::EOS_NotFound);
        return;
    }
    // Only someone the session has registered may register anyone else.
    if (!contains(entry->infos.registered_players, settings_.product_user_id())) {
        deliver(cb_register, sizeof(EOS_Sessions_RegisterPlayersCallbackInfo),
                reinterpret_cast<completion_delegate>(delegate), client_data,
                EOS_EResult::EOS_Sessions_NotAllowed);
        return;
    }

    std::vector<std::string> changed;
    for (u32 i = 0; i < options->PlayersToRegisterCount; i++) {
        EOS_ProductUserId player = options->PlayersToRegister[i];
        if (player == 0) {
            continue;
        }
        const std::string id = player->id_str;
        if (contains(entry->infos.registered_players, id)) {
            continue;
        }
        entry->infos.registered_players.push_back(id);
        changed.push_back(id);
    }
    entry->infos.open_slots = open_slots_of(entry->infos);

    if (!changed.empty()) {
        session_members notice;
        notice.session_id = entry->infos.session_id;
        notice.player_ids = changed;
        byte_writer writer;
        serialize(writer, notice);
        send_to_members(*entry, message_type::session_register, writer, std::string());
    }

    std::unique_ptr<frame_result> result(new frame_result());
    EOS_Sessions_RegisterPlayersCallbackInfo* info =
        static_cast<EOS_Sessions_RegisterPlayersCallbackInfo*>(
            result->create_callback(cb_register, sizeof(EOS_Sessions_RegisterPlayersCallbackInfo),
                                    reinterpret_cast<completion_delegate>(delegate)));
    info->ResultCode = changed.empty() ? EOS_EResult::EOS_NoChange : EOS_EResult::EOS_Success;
    info->ClientData = client_data;
    info->RegisteredPlayers = alloc_player_array(changed);
    info->RegisteredPlayersCount = static_cast<u32>(changed.size());
    result->set_done(true);
    callbacks_.add_callback(this, std::move(result));
}

void sdk_sessions::unregister_players(const EOS_Sessions_UnregisterPlayersOptions* options,
                                      void* client_data,
                                      EOS_Sessions_OnUnregisterPlayersCallback delegate) {
    if (delegate == 0) {
        return;
    }
    if (options == 0 ||
        !version_ok(options->ApiVersion, EOS_SESSIONS_UNREGISTERPLAYERS_API_LATEST) ||
        options->SessionName == 0 ||
        (options->PlayersToUnregisterCount > 0 && options->PlayersToUnregister == 0)) {
        deliver(cb_unregister, sizeof(EOS_Sessions_UnregisterPlayersCallbackInfo),
                reinterpret_cast<completion_delegate>(delegate), client_data,
                EOS_EResult::EOS_InvalidParameters);
        return;
    }
    session* entry = find_by_name(options->SessionName);
    if (entry == 0) {
        deliver(cb_unregister, sizeof(EOS_Sessions_UnregisterPlayersCallbackInfo),
                reinterpret_cast<completion_delegate>(delegate), client_data,
                EOS_EResult::EOS_NotFound);
        return;
    }
    if (!contains(entry->infos.registered_players, settings_.product_user_id())) {
        deliver(cb_unregister, sizeof(EOS_Sessions_UnregisterPlayersCallbackInfo),
                reinterpret_cast<completion_delegate>(delegate), client_data,
                EOS_EResult::EOS_Sessions_NotAllowed);
        return;
    }

    std::vector<std::string> changed;
    for (u32 i = 0; i < options->PlayersToUnregisterCount; i++) {
        EOS_ProductUserId player = options->PlayersToUnregister[i];
        if (player == 0) {
            continue;
        }
        const std::string id = player->id_str;
        if (!contains(entry->infos.registered_players, id)) {
            continue;
        }
        remove_from(entry->infos.registered_players, id);
        changed.push_back(id);
    }
    entry->infos.open_slots = open_slots_of(entry->infos);

    if (!changed.empty()) {
        session_members notice;
        notice.session_id = entry->infos.session_id;
        notice.player_ids = changed;
        byte_writer writer;
        serialize(writer, notice);
        send_to_members(*entry, message_type::session_unregister, writer, std::string());
    }

    std::unique_ptr<frame_result> result(new frame_result());
    EOS_Sessions_UnregisterPlayersCallbackInfo* info =
        static_cast<EOS_Sessions_UnregisterPlayersCallbackInfo*>(
            result->create_callback(cb_unregister,
                                    sizeof(EOS_Sessions_UnregisterPlayersCallbackInfo),
                                    reinterpret_cast<completion_delegate>(delegate)));
    info->ResultCode = changed.empty() ? EOS_EResult::EOS_NoChange : EOS_EResult::EOS_Success;
    info->ClientData = client_data;
    info->UnregisteredPlayers = alloc_player_array(changed);
    info->UnregisteredPlayersCount = static_cast<u32>(changed.size());
    result->set_done(true);
    callbacks_.add_callback(this, std::move(result));
}

void sdk_sessions::join_session(const EOS_Sessions_JoinSessionOptions* options, void* client_data,
                                EOS_Sessions_OnJoinSessionCallback delegate) {
    if (delegate == 0) {
        return;
    }
    details_object* found = (options != 0) ? details_.find(options->SessionHandle) : 0;
    if (options == 0 || !version_ok(options->ApiVersion, EOS_SESSIONS_JOINSESSION_API_LATEST) ||
        options->SessionName == 0 || options->SessionName[0] == '\0' || found == 0 ||
        options->LocalUserId == 0) {
        deliver(cb_join, sizeof(EOS_Sessions_JoinSessionCallbackInfo),
                reinterpret_cast<completion_delegate>(delegate), client_data,
                EOS_EResult::EOS_InvalidParameters);
        return;
    }
    if (find_by_name(options->SessionName) != 0) {
        deliver(cb_join, sizeof(EOS_Sessions_JoinSessionCallbackInfo),
                reinterpret_cast<completion_delegate>(delegate), client_data,
                EOS_EResult::EOS_Sessions_SessionAlreadyExists);
        return;
    }

    // A match already under way is only joinable if the host said so.
    const i32 state = found->infos.state;
    const bool in_progress = state == static_cast<i32>(EOS_EOnlineSessionState::EOS_OSS_InProgress);
    const bool pending = state == static_cast<i32>(EOS_EOnlineSessionState::EOS_OSS_Pending);
    if (!(pending || (in_progress && found->infos.allow_join_in_progress))) {
        deliver(cb_join, sizeof(EOS_Sessions_JoinSessionCallbackInfo),
                reinterpret_cast<completion_delegate>(delegate), client_data,
                EOS_EResult::EOS_Sessions_NotAllowed);
        return;
    }

    session joining;
    joining.infos = found->infos;
    joining.local_user = options->LocalUserId->id_str;
    joining.local_state = session::joining;
    sessions_[options->SessionName] = joining;

    // We do not decide whether we are in; the host does. The result stays open until it answers.
    session_join_request request;
    request.session_id = found->infos.session_id;
    byte_writer writer;
    serialize(writer, request);
    send_to(found->infos.owner_id, message_type::session_join_request, writer);

    std::unique_ptr<frame_result> result(new frame_result());
    EOS_Sessions_JoinSessionCallbackInfo* info = static_cast<EOS_Sessions_JoinSessionCallbackInfo*>(
        result->create_callback(cb_join, sizeof(EOS_Sessions_JoinSessionCallbackInfo),
                                reinterpret_cast<completion_delegate>(delegate)));
    info->ResultCode = EOS_EResult::EOS_TimedOut; // stands until the host says otherwise
    info->ClientData = client_data;
    frame_result* raw = result.get();
    callbacks_.add_callback(this, std::move(result));

    pending_join waiting;
    waiting.session_id = found->infos.session_id;
    waiting.deadline = std::chrono::steady_clock::now() + join_timeout;
    pending_joins_[raw] = waiting;
}


// --- Search ---

EOS_EResult sdk_sessions::create_session_search(
    const EOS_Sessions_CreateSessionSearchOptions* options, EOS_HSessionSearch* out) {
    if (out == 0) {
        return EOS_EResult::EOS_InvalidParameters;
    }
    *out = 0;
    if (options == 0 ||
        !version_ok(options->ApiVersion, EOS_SESSIONS_CREATESESSIONSEARCH_API_LATEST) ||
        options->MaxSearchResults == 0 ||
        options->MaxSearchResults > EOS_SESSIONS_MAX_SEARCH_RESULTS) {
        return EOS_EResult::EOS_InvalidParameters;
    }
    std::unique_ptr<search_object> object(new search_object());
    object->max_results = options->MaxSearchResults;
    object->searching = false;
    *out = reinterpret_cast<EOS_HSessionSearch>(searches_.add(std::move(object)));
    return EOS_EResult::EOS_Success;
}

EOS_EResult sdk_sessions::search_set_session_id(void* handle,
                                                const EOS_SessionSearch_SetSessionIdOptions* options) {
    search_object* object = searches_.find(handle);
    if (object == 0 || options == 0 ||
        !version_ok(options->ApiVersion, EOS_SESSIONSEARCH_SETSESSIONID_API_LATEST) ||
        options->SessionId == 0) {
        return EOS_EResult::EOS_InvalidParameters;
    }
    object->query.session_id = options->SessionId;
    return EOS_EResult::EOS_Success;
}

EOS_EResult sdk_sessions::search_set_target_user(
    void* handle, const EOS_SessionSearch_SetTargetUserIdOptions* options) {
    search_object* object = searches_.find(handle);
    if (object == 0 || options == 0 ||
        !version_ok(options->ApiVersion, EOS_SESSIONSEARCH_SETTARGETUSERID_API_LATEST) ||
        options->TargetUserId == 0) {
        return EOS_EResult::EOS_InvalidParameters;
    }
    object->query.target_user_id = options->TargetUserId->id_str;
    return EOS_EResult::EOS_Success;
}

EOS_EResult sdk_sessions::search_set_parameter(void* handle,
                                               const EOS_SessionSearch_SetParameterOptions* options) {
    search_object* object = searches_.find(handle);
    if (object == 0 || options == 0 ||
        !version_ok(options->ApiVersion, EOS_SESSIONSEARCH_SETPARAMETER_API_LATEST)) {
        return EOS_EResult::EOS_InvalidParameters;
    }
    search_parameter parameter;
    if (!read_attribute(options->Parameter, parameter.attribute)) {
        return EOS_EResult::EOS_InvalidParameters;
    }
    parameter.comparison_op = static_cast<i32>(options->ComparisonOp);

    // A key may carry several comparisons, all of which must hold, so the same key with the same
    // operator replaces, and with a different operator adds.
    for (std::size_t i = 0; i < object->query.parameters.size(); i++) {
        if (object->query.parameters[i].attribute.key == parameter.attribute.key &&
            object->query.parameters[i].comparison_op == parameter.comparison_op) {
            object->query.parameters[i] = parameter;
            return EOS_EResult::EOS_Success;
        }
    }
    object->query.parameters.push_back(parameter);
    return EOS_EResult::EOS_Success;
}

EOS_EResult sdk_sessions::search_remove_parameter(
    void* handle, const EOS_SessionSearch_RemoveParameterOptions* options) {
    search_object* object = searches_.find(handle);
    if (object == 0 || options == 0 ||
        !version_ok(options->ApiVersion, EOS_SESSIONSEARCH_REMOVEPARAMETER_API_LATEST) ||
        options->Key == 0) {
        return EOS_EResult::EOS_InvalidParameters;
    }
    // Look the key up rather than indexing it: creating an empty entry for a key that was never
    // set would make every session fail to match it, and the search would find nothing.
    for (std::size_t i = 0; i < object->query.parameters.size(); i++) {
        if (object->query.parameters[i].attribute.key == options->Key &&
            object->query.parameters[i].comparison_op == static_cast<i32>(options->ComparisonOp)) {
            object->query.parameters.erase(object->query.parameters.begin() + i);
            return EOS_EResult::EOS_Success;
        }
    }
    return EOS_EResult::EOS_NotFound;
}

EOS_EResult sdk_sessions::search_set_max_results(
    void* handle, const EOS_SessionSearch_SetMaxResultsOptions* options) {
    search_object* object = searches_.find(handle);
    if (object == 0 || options == 0 ||
        !version_ok(options->ApiVersion, EOS_SESSIONSEARCH_SETMAXSEARCHRESULTS_API_LATEST) ||
        options->MaxSearchResults == 0 ||
        options->MaxSearchResults > EOS_SESSIONS_MAX_SEARCH_RESULTS) {
        return EOS_EResult::EOS_InvalidParameters;
    }
    object->max_results = options->MaxSearchResults;
    return EOS_EResult::EOS_Success;
}

void sdk_sessions::search_find(void* handle, const EOS_SessionSearch_FindOptions* options,
                               void* client_data, EOS_SessionSearch_OnFindCallback delegate) {
    if (delegate == 0) {
        return;
    }
    search_object* object = searches_.find(handle);
    if (object == 0 || options == 0 ||
        !version_ok(options->ApiVersion, EOS_SESSIONSEARCH_FIND_API_LATEST) ||
        options->LocalUserId == 0) {
        deliver(cb_find, sizeof(EOS_SessionSearch_FindCallbackInfo),
                reinterpret_cast<completion_delegate>(delegate), client_data,
                EOS_EResult::EOS_InvalidParameters);
        return;
    }
    // A search has to ask something, and it cannot ask two contradictory things at once.
    const bool by_id = !object->query.session_id.empty();
    const bool by_user = !object->query.target_user_id.empty();
    if ((!by_id && !by_user && object->query.parameters.empty()) || (by_id && by_user)) {
        deliver(cb_find, sizeof(EOS_SessionSearch_FindCallbackInfo),
                reinterpret_cast<completion_delegate>(delegate), client_data,
                EOS_EResult::EOS_InvalidParameters);
        return;
    }
    if (object->searching) {
        deliver(cb_find, sizeof(EOS_SessionSearch_FindCallbackInfo),
                reinterpret_cast<completion_delegate>(delegate), client_data,
                EOS_EResult::EOS_AlreadyPending);
        return;
    }

    object->results.clear();
    object->searching = true;
    char id[32];
    std::snprintf(id, sizeof(id), "%llu", static_cast<unsigned long long>(next_search_id_++));
    object->query.search_id = id;
    object->query.max_results = object->max_results;

    // A host finds what it is hosting, but never a session it merely joined: those are the host's
    // to surface, and a searcher should not turn up games it is already in. This is the local twin
    // of the rule that only a host advertises over the network.
    std::map<std::string, session>::const_iterator local = sessions_.begin();
    for (; local != sessions_.end(); ++local) {
        if (object->results.size() >= object->max_results) {
            break;
        }
        if (local->second.local_state == session::hosting &&
            session_matches(local->second.infos, object->query)) {
            object->results.push_back(local->second.infos);
        }
    }

    // Ask every peer. Each one answers, even with nothing, so the search finishes as soon as they
    // all have rather than waiting out the deadline.
    byte_writer writer;
    serialize(writer, object->query);
    const std::vector<std::string> peers = network_.peer_ids();
    object->awaiting.clear();
    for (std::size_t i = 0; i < peers.size(); i++) {
        if (by_user && peers[i] != object->query.target_user_id) {
            continue; // only that player can answer a search aimed at them
        }
        send_to(peers[i], message_type::session_search, writer);
        object->awaiting.insert(peers[i]);
    }
    object->deadline = std::chrono::steady_clock::now() + search_timeout;

    std::unique_ptr<frame_result> result(new frame_result());
    EOS_SessionSearch_FindCallbackInfo* info = static_cast<EOS_SessionSearch_FindCallbackInfo*>(
        result->create_callback(cb_find, sizeof(EOS_SessionSearch_FindCallbackInfo),
                                reinterpret_cast<completion_delegate>(delegate)));
    // A search that finds nothing still succeeded; it just found nothing.
    info->ResultCode = EOS_EResult::EOS_Success;
    info->ClientData = client_data;
    frame_result* raw = result.get();
    callbacks_.add_callback(this, std::move(result));
    pending_finds_[raw] = handle;
}

u32 sdk_sessions::search_result_count(void* handle) const {
    const search_object* object = searches_.find(handle);
    if (object == 0 || object->searching) {
        return 0; // nothing to read until the search has settled
    }
    return static_cast<u32>(object->results.size());
}

EOS_EResult sdk_sessions::search_copy_result(
    void* handle, const EOS_SessionSearch_CopySearchResultByIndexOptions* options,
    EOS_HSessionDetails* out) {
    if (out == 0) {
        return EOS_EResult::EOS_InvalidParameters;
    }
    *out = 0;
    search_object* object = searches_.find(handle);
    if (object == 0 || options == 0 ||
        !version_ok(options->ApiVersion, EOS_SESSIONSEARCH_COPYSEARCHRESULTBYINDEX_API_LATEST)) {
        return EOS_EResult::EOS_InvalidParameters;
    }
    // The header lists only InvalidParameters for a bad index (and there is nothing to copy while
    // the search is still running), so both are InvalidParameters, not NotFound.
    if (object->searching || options->SessionIndex >= object->results.size()) {
        return EOS_EResult::EOS_InvalidParameters;
    }
    std::unique_ptr<details_object> copy(new details_object());
    copy->infos = object->results[options->SessionIndex];
    *out = reinterpret_cast<EOS_HSessionDetails>(details_.add(std::move(copy)));
    return EOS_EResult::EOS_Success;
}

void sdk_sessions::search_release(void* handle) {
    // A search still in flight has a result waiting on it, so forget the link before the data goes.
    std::map<frame_result*, void*>::iterator it = pending_finds_.begin();
    while (it != pending_finds_.end()) {
        if (it->second == handle) {
            pending_finds_.erase(it++);
        } else {
            ++it;
        }
    }
    searches_.release(handle);
}

// Does this session answer that question?
bool sdk_sessions::session_matches(const session_infos& infos, const session_search& query) const {
    // A session nobody may find is not found.
    if (infos.permission_level == static_cast<i32>(EOS_EOnlineSessionPermissionLevel::EOS_OSPF_InviteOnly)) {
        return false;
    }
    // A match already running that the host closed is not joinable, so it is not offered.
    if (infos.state == static_cast<i32>(EOS_EOnlineSessionState::EOS_OSS_InProgress) &&
        !infos.allow_join_in_progress) {
        return false;
    }
    if (!query.session_id.empty() && infos.session_id != query.session_id) {
        return false;
    }
    if (!query.target_user_id.empty() && !contains(infos.registered_players, query.target_user_id)) {
        return false;
    }

    for (std::size_t i = 0; i < query.parameters.size(); i++) {
        const search_parameter& parameter = query.parameters[i];
        const std::string& key = parameter.attribute.key;
        const i32 op = parameter.comparison_op;

        // The well-known keys ask about the session itself rather than about an attribute of it.
        if (key == key_bucket) {
            if (parameter.attribute.value_type != 3 ||
                !compare_ordered<std::string>(infos.bucket_id, parameter.attribute.as_string, op)) {
                return false;
            }
            continue;
        }
        if (key == key_empty_only) {
            if (infos.registered_players.size() > 1) {
                return false; // the host alone still counts as empty
            }
            continue;
        }
        if (key == key_nonempty_only) {
            if (infos.registered_players.size() <= 1) {
                return false;
            }
            continue;
        }
        if (key == key_min_slots) {
            if (!compare_ordered<i64>(static_cast<i64>(open_slots_of(infos)),
                                      parameter.attribute.as_int64,
                                      op == 0 ? 3 : op)) { // an exact-equal minimum means "at least"
                return false;
            }
            continue;
        }

        bool matched = false;
        for (std::size_t a = 0; a < infos.attributes.size(); a++) {
            if (infos.attributes[a].key != key) {
                continue;
            }
            if (!compare_attribute(infos.attributes[a], parameter.attribute, op)) {
                return false;
            }
            matched = true;
            break;
        }
        if (!matched) {
            return false; // the session does not carry the key at all
        }
    }
    return true;
}

// --- SessionDetails ---

EOS_EResult sdk_sessions::details_copy_info(void* handle, EOS_SessionDetails_Info** out) {
    if (out == 0) {
        return EOS_EResult::EOS_InvalidParameters;
    }
    *out = 0;
    details_object* object = details_.find(handle);
    if (object == 0) {
        return EOS_EResult::EOS_InvalidParameters;
    }
    std::unique_ptr<details_info_holder> holder(new details_info_holder());
    fill_details_info(*holder, object->infos);
    EOS_SessionDetails_Info* info = &holder->info;
    {
        std::lock_guard<std::mutex> lock(g_info_mutex);
        g_details_infos[info] = std::move(holder);
    }
    *out = info;
    return EOS_EResult::EOS_Success;
}

u32 sdk_sessions::details_attribute_count(void* handle) const {
    const details_object* object = details_.find(handle);
    return (object != 0) ? static_cast<u32>(object->infos.attributes.size()) : 0;
}

EOS_EResult sdk_sessions::details_copy_attribute_by_index(
    void* handle, const EOS_SessionDetails_CopySessionAttributeByIndexOptions* options,
    EOS_SessionDetails_Attribute** out) {
    if (out == 0) {
        return EOS_EResult::EOS_InvalidParameters;
    }
    *out = 0;
    details_object* object = details_.find(handle);
    if (object == 0 || options == 0 ||
        !version_ok(options->ApiVersion,
                    EOS_SESSIONDETAILS_COPYSESSIONATTRIBUTEBYINDEX_API_LATEST)) {
        return EOS_EResult::EOS_InvalidParameters;
    }
    if (options->AttrIndex >= object->infos.attributes.size()) {
        return EOS_EResult::EOS_NotFound;
    }
    return emit_attribute(object->infos.attributes[options->AttrIndex], out);
}

EOS_EResult sdk_sessions::details_copy_attribute_by_key(
    void* handle, const EOS_SessionDetails_CopySessionAttributeByKeyOptions* options,
    EOS_SessionDetails_Attribute** out) {
    if (out == 0) {
        return EOS_EResult::EOS_InvalidParameters;
    }
    *out = 0;
    details_object* object = details_.find(handle);
    if (object == 0 || options == 0 ||
        !version_ok(options->ApiVersion, EOS_SESSIONDETAILS_COPYSESSIONATTRIBUTEBYKEY_API_LATEST) ||
        options->AttrKey == 0) {
        return EOS_EResult::EOS_InvalidParameters;
    }
    for (std::size_t i = 0; i < object->infos.attributes.size(); i++) {
        if (object->infos.attributes[i].key == options->AttrKey) {
            return emit_attribute(object->infos.attributes[i], out);
        }
    }
    return EOS_EResult::EOS_NotFound;
}

EOS_EResult sdk_sessions::emit_attribute(const session_attribute& from,
                                         EOS_SessionDetails_Attribute** out) {
    std::unique_ptr<attribute_holder> holder(new attribute_holder());
    write_attribute(from, holder->data, holder->key, holder->value);
    holder->attribute.ApiVersion = EOS_SESSIONDETAILS_ATTRIBUTE_API_LATEST;
    holder->attribute.Data = &holder->data;
    holder->attribute.AdvertisementType =
        static_cast<EOS_ESessionAttributeAdvertisementType>(from.advertisement);

    EOS_SessionDetails_Attribute* attribute = &holder->attribute;
    {
        std::lock_guard<std::mutex> lock(g_info_mutex);
        g_attributes[attribute] = std::move(holder);
    }
    *out = attribute;
    return EOS_EResult::EOS_Success;
}

void sdk_sessions::details_release(void* handle) {
    details_.release(handle);
}

// --- ActiveSession ---

EOS_EResult sdk_sessions::copy_active_session_handle(
    const EOS_Sessions_CopyActiveSessionHandleOptions* options, EOS_HActiveSession* out) {
    if (out == 0) {
        return EOS_EResult::EOS_InvalidParameters;
    }
    *out = 0;
    if (options == 0 ||
        !version_ok(options->ApiVersion, EOS_SESSIONS_COPYACTIVESESSIONHANDLE_API_LATEST) ||
        options->SessionName == 0) {
        return EOS_EResult::EOS_InvalidParameters;
    }
    session* entry = find_by_name(options->SessionName);
    if (entry == 0) {
        return EOS_EResult::EOS_NotFound;
    }
    std::unique_ptr<active_object> object(new active_object());
    object->session_name = options->SessionName;
    *out = reinterpret_cast<EOS_HActiveSession>(actives_.add(std::move(object)));
    return EOS_EResult::EOS_Success;
}

EOS_EResult sdk_sessions::active_copy_info(void* handle, EOS_ActiveSession_Info** out) {
    if (out == 0) {
        return EOS_EResult::EOS_InvalidParameters;
    }
    *out = 0;
    const active_object* object = actives_.find(handle);
    if (object == 0) {
        return EOS_EResult::EOS_InvalidParameters;
    }
    // The session can end while the game still holds the handle. The header gives us no NotFound
    // to report here, so the handle simply stops being a usable one.
    const session* entry = find_by_name(object->session_name);
    if (entry == 0) {
        return EOS_EResult::EOS_InvalidParameters;
    }
    std::unique_ptr<active_info_holder> holder(new active_info_holder());
    holder->session_name = object->session_name;
    fill_details_info(holder->details, entry->infos);
    holder->info.ApiVersion = EOS_ACTIVESESSION_INFO_API_LATEST;
    holder->info.SessionName = holder->session_name.c_str();
    holder->info.LocalUserId =
        entry->local_user.empty() ? 0
                                  : id_registry::instance().get_product_user_id(entry->local_user);
    holder->info.State = static_cast<EOS_EOnlineSessionState>(entry->infos.state);
    holder->info.SessionDetails = &holder->details.info;

    EOS_ActiveSession_Info* info = &holder->info;
    {
        std::lock_guard<std::mutex> lock(g_info_mutex);
        g_active_infos[info] = std::move(holder);
    }
    *out = info;
    return EOS_EResult::EOS_Success;
}

u32 sdk_sessions::active_registered_count(void* handle) const {
    const active_object* object = actives_.find(handle);
    const session* entry = (object != 0) ? find_by_name(object->session_name) : 0;
    return (entry != 0) ? static_cast<u32>(entry->infos.registered_players.size()) : 0;
}

EOS_ProductUserId sdk_sessions::active_registered_by_index(
    void* handle, const EOS_ActiveSession_GetRegisteredPlayerByIndexOptions* options) const {
    const active_object* object = actives_.find(handle);
    const session* entry = (object != 0) ? find_by_name(object->session_name) : 0;
    if (entry == 0 || options == 0 ||
        options->PlayerIndex >= entry->infos.registered_players.size()) {
        return 0;
    }
    return id_registry::instance().get_product_user_id(
        entry->infos.registered_players[options->PlayerIndex]);
}

void sdk_sessions::active_release(void* handle) {
    actives_.release(handle);
}

EOS_EResult sdk_sessions::is_user_in_session(
    const EOS_Sessions_IsUserInSessionOptions* options) const {
    if (options == 0 || !version_ok(options->ApiVersion, EOS_SESSIONS_ISUSERINSESSION_API_LATEST) ||
        options->SessionName == 0 || options->TargetUserId == 0) {
        return EOS_EResult::EOS_InvalidParameters;
    }
    std::map<std::string, session>::const_iterator it = sessions_.find(options->SessionName);
    if (it == sessions_.end()) {
        return EOS_EResult::EOS_NotFound;
    }
    return contains(it->second.infos.registered_players, options->TargetUserId->id_str)
               ? EOS_EResult::EOS_Success
               : EOS_EResult::EOS_NotFound;
}

EOS_EResult sdk_sessions::dump_session_state(
    const EOS_Sessions_DumpSessionStateOptions* options) const {
    if (options == 0 || !version_ok(options->ApiVersion, EOS_SESSIONS_DUMPSESSIONSTATE_API_LATEST) ||
        options->SessionName == 0) {
        return EOS_EResult::EOS_InvalidParameters;
    }
    std::map<std::string, session>::const_iterator it = sessions_.find(options->SessionName);
    if (it == sessions_.end()) {
        return EOS_EResult::EOS_NotFound;
    }
    log_info("sessions: '" + it->first + "' id=" + it->second.infos.session_id);
    return EOS_EResult::EOS_Success;
}

// --- The engine hooks ---

bool sdk_sessions::run_callbacks(frame_result& result) {
    const std::chrono::steady_clock::time_point now = std::chrono::steady_clock::now();

    std::map<frame_result*, void*>::iterator find_it = pending_finds_.find(&result);
    if (find_it != pending_finds_.end()) {
        search_object* object = searches_.find(find_it->second);
        if (object == 0) {
            return true; // the game let the search go; nothing left to wait for
        }
        // Every peer answers, so an empty waiting list means the answer is complete.
        if (object->awaiting.empty() || now > object->deadline) {
            object->searching = false;
            return true;
        }
        return false;
    }

    std::map<frame_result*, pending_join>::iterator join_it = pending_joins_.find(&result);
    if (join_it != pending_joins_.end()) {
        if (now > join_it->second.deadline) {
            // The host never answered. The session we optimistically recorded is not ours.
            session* entry = find_by_id(join_it->second.session_id);
            if (entry != 0 && entry->local_state == session::joining) {
                std::map<std::string, session>::iterator it = sessions_.begin();
                for (; it != sessions_.end(); ++it) {
                    if (it->second.infos.session_id == join_it->second.session_id) {
                        sessions_.erase(it);
                        break;
                    }
                }
            }
            return true;
        }
        return false;
    }
    return false;
}

void sdk_sessions::free_callback(frame_result& result) {
    pending_finds_.erase(&result);
    pending_joins_.erase(&result);

    // These callbacks hand the game heap of their own: the update its session strings, and
    // register/unregister the arrays of players they changed. A null (an error path never filled
    // them) deletes safely.
    if (result.type_id() == cb_update) {
        EOS_Sessions_UpdateSessionCallbackInfo* info =
            result.get_callback<EOS_Sessions_UpdateSessionCallbackInfo>();
        delete[] info->SessionName;
        delete[] info->SessionId;
        info->SessionName = 0;
        info->SessionId = 0;
    } else if (result.type_id() == cb_register) {
        EOS_Sessions_RegisterPlayersCallbackInfo* info =
            result.get_callback<EOS_Sessions_RegisterPlayersCallbackInfo>();
        delete[] info->RegisteredPlayers;
        info->RegisteredPlayers = 0;
    } else if (result.type_id() == cb_unregister) {
        EOS_Sessions_UnregisterPlayersCallbackInfo* info =
            result.get_callback<EOS_Sessions_UnregisterPlayersCallbackInfo>();
        delete[] info->UnregisteredPlayers;
        info->UnregisteredPlayers = 0;
    }
}

bool sdk_sessions::cb_run_frame() {
    return false;
}

// --- The network ---

bool sdk_sessions::on_network_message(const net_envelope& message) {
    if (message.source_id.empty() || message.source_id == settings_.product_user_id()) {
        return true;
    }

    // A peer that leaves takes its seat with it, in every session we know about.
    if (message.type_tag == static_cast<u16>(message_type::peer_disconnected)) {
        std::map<std::string, session>::iterator it = sessions_.begin();
        for (; it != sessions_.end(); ++it) {
            remove_from(it->second.infos.registered_players, message.source_id);
            it->second.infos.open_slots = open_slots_of(it->second.infos);
        }
        return true;
    }

    byte_reader reader(message.payload.data(), message.payload.size());

    if (message.type_tag == static_cast<u16>(message_type::session_search)) {
        session_search query;
        if (!deserialize(reader, query)) {
            return true;
        }
        session_search_response answer;
        answer.search_id = query.search_id;

        // The one place a different game is turned away. We answer regardless, though: a searcher
        // waits on every peer it asked, and an empty answer is what lets it stop waiting. We answer
        // only for sessions we host -- a copy of a session we merely joined is the host's to
        // advertise, not ours, or a searcher would see the same session from every member.
        if (message.game_id == settings_.product_id()) {
            std::map<std::string, session>::const_iterator it = sessions_.begin();
            for (; it != sessions_.end(); ++it) {
                if (it->second.local_state == session::hosting &&
                    session_matches(it->second.infos, query)) {
                    answer.sessions.push_back(it->second.infos);
                }
            }
        }
        byte_writer writer;
        serialize(writer, answer);
        send_to(message.source_id, message_type::session_search_response, writer);
        return true;
    }

    if (message.type_tag == static_cast<u16>(message_type::session_search_response)) {
        // Not our game's answer, so not ours to read.
        if (message.game_id != settings_.product_id()) {
            return true;
        }
        session_search_response answer;
        if (!deserialize(reader, answer)) {
            return true;
        }
        std::map<frame_result*, void*>::iterator it = pending_finds_.begin();
        for (; it != pending_finds_.end(); ++it) {
            search_object* object = searches_.find(it->second);
            if (object == 0 || !object->searching || object->query.search_id != answer.search_id) {
                continue;
            }
            // Only a peer we actually asked can answer this search; an unsolicited response, with a
            // guessed search id, is ignored rather than allowed to plant results.
            if (object->awaiting.find(message.source_id) == object->awaiting.end()) {
                continue;
            }
            object->awaiting.erase(message.source_id);
            for (std::size_t s = 0; s < answer.sessions.size(); s++) {
                if (object->results.size() >= object->max_results) {
                    break;
                }
                // Do not trust the responder to have filtered honestly: re-test each session
                // against our own query, so a peer cannot return one that does not match.
                if (session_matches(answer.sessions[s], object->query)) {
                    object->results.push_back(answer.sessions[s]);
                }
            }
        }
        return true;
    }

    if (message.type_tag == static_cast<u16>(message_type::session_join_request)) {
        session_join_request request;
        if (!deserialize(reader, request)) {
            return true;
        }
        session* entry = find_by_id(request.session_id);

        session_join_response answer;
        answer.session_id = request.session_id;
        answer.player_id = message.source_id;
        if (entry == 0 || entry->local_state != session::hosting) {
            answer.reason = static_cast<i32>(EOS_EResult::EOS_NotFound);
        } else if (!connect_.is_known_peer(message.source_id)) {
            // We will not seat a player we have never met.
            answer.reason = static_cast<i32>(EOS_EResult::EOS_Sessions_NotAllowed);
        } else if (contains(entry->infos.registered_players, message.source_id)) {
            answer.reason = static_cast<i32>(EOS_EResult::EOS_Success);
        } else if (entry->infos.registered_players.size() >= entry->infos.max_players) {
            answer.reason = static_cast<i32>(EOS_EResult::EOS_Sessions_TooManyPlayers);
        } else {
            entry->infos.registered_players.push_back(message.source_id);
            entry->infos.open_slots = open_slots_of(entry->infos);
            answer.reason = static_cast<i32>(EOS_EResult::EOS_Success);
        }

        byte_writer writer;
        serialize(writer, answer);
        // Everyone in the session hears the verdict, so they all learn who is in.
        send_to(message.source_id, message_type::session_join_response, writer);
        if (entry != 0 && answer.reason == static_cast<i32>(EOS_EResult::EOS_Success)) {
            send_to_members(*entry, message_type::session_join_response, writer, message.source_id);
            broadcast_session(*entry);
        }
        return true;
    }

    if (message.type_tag == static_cast<u16>(message_type::session_join_response)) {
        session_join_response answer;
        if (!deserialize(reader, answer)) {
            return true;
        }
        session* entry = find_by_id(answer.session_id);
        if (entry == 0) {
            return true;
        }
        // Only the host of a session pronounces on who is in it. A verdict from anyone else -- a
        // peer that does not own this session -- is a forgery, and is ignored rather than allowed
        // to erase the session or push a ghost onto its roster.
        if (message.source_id != entry->infos.owner_id) {
            return true;
        }
        const EOS_EResult code = static_cast<EOS_EResult>(answer.reason);

        // A verdict about someone else just updates our picture of who is in.
        if (answer.player_id != settings_.product_user_id()) {
            if (code == EOS_EResult::EOS_Success &&
                !contains(entry->infos.registered_players, answer.player_id)) {
                entry->infos.registered_players.push_back(answer.player_id);
                entry->infos.open_slots = open_slots_of(entry->infos);
            }
            return true;
        }

        // A verdict about us means something only while we are waiting on a join we started. An
        // unsolicited one -- about a session we host, or already joined -- is ignored, so it cannot
        // tear a live session out from under the game.
        bool waiting = false;
        std::map<frame_result*, pending_join>::iterator it = pending_joins_.begin();
        for (; it != pending_joins_.end(); ++it) {
            if (it->second.session_id != answer.session_id) {
                continue;
            }
            EOS_Sessions_JoinSessionCallbackInfo* info =
                it->first->get_callback<EOS_Sessions_JoinSessionCallbackInfo>();
            info->ResultCode = code;
            it->first->set_done(true);
            waiting = true;
            break;
        }
        if (!waiting) {
            return true;
        }
        if (code == EOS_EResult::EOS_Success) {
            entry->local_state = session::joined;
            if (!contains(entry->infos.registered_players, settings_.product_user_id())) {
                entry->infos.registered_players.push_back(settings_.product_user_id());
            }
            entry->infos.open_slots = open_slots_of(entry->infos);
        } else if (entry->local_state == session::joining) {
            // The host refused. Drop the session we optimistically recorded when we asked.
            std::map<std::string, session>::iterator dead = sessions_.begin();
            for (; dead != sessions_.end(); ++dead) {
                if (dead->second.infos.session_id == answer.session_id) {
                    sessions_.erase(dead);
                    break;
                }
            }
        }
        return true;
    }

    if (message.type_tag == static_cast<u16>(message_type::session_infos)) {
        session_infos infos;
        if (!deserialize(reader, infos)) {
            return true;
        }
        session* entry = find_by_id(infos.session_id);
        // Only the host it belongs to may rewrite a session, and never one we host ourselves.
        if (entry != 0 && entry->local_state != session::hosting &&
            entry->infos.owner_id == message.source_id) {
            const session::local_state_kind keep = entry->local_state;
            const std::string keep_user = entry->local_user;
            entry->infos = infos;
            entry->local_state = keep;
            entry->local_user = keep_user;
        }
        return true;
    }

    if (message.type_tag == static_cast<u16>(message_type::session_destroy)) {
        session_destroy notice;
        if (!deserialize(reader, notice)) {
            return true;
        }
        std::map<std::string, session>::iterator it = sessions_.begin();
        for (; it != sessions_.end(); ++it) {
            if (it->second.infos.session_id == notice.session_id &&
                it->second.local_state != session::hosting &&
                it->second.infos.owner_id == message.source_id) {
                sessions_.erase(it);
                break;
            }
        }
        return true;
    }

    if (message.type_tag == static_cast<u16>(message_type::session_register) ||
        message.type_tag == static_cast<u16>(message_type::session_unregister)) {
        session_members notice;
        if (!deserialize(reader, notice)) {
            return true;
        }
        session* entry = find_by_id(notice.session_id);
        if (entry == 0) {
            return true;
        }
        const bool adding = message.type_tag == static_cast<u16>(message_type::session_register);
        const bool from_owner = message.source_id == entry->infos.owner_id;
        // The host manages the whole roster; anyone else may only take itself out (leaving). A
        // register from a non-host, or an unregister of anyone but the sender, is refused -- else
        // a participant could evict the host, drop other players, or pad the roster with ghosts.
        if (!from_owner) {
            if (adding) {
                return true;
            }
            for (std::size_t i = 0; i < notice.player_ids.size(); i++) {
                if (notice.player_ids[i] != message.source_id) {
                    return true;
                }
            }
        }
        for (std::size_t i = 0; i < notice.player_ids.size(); i++) {
            const std::string& player = notice.player_ids[i];
            if (adding) {
                if (!contains(entry->infos.registered_players, player) &&
                    entry->infos.registered_players.size() < entry->infos.max_players) {
                    entry->infos.registered_players.push_back(player);
                }
            } else if (player != entry->infos.owner_id) {
                // The host keeps its seat; it leaves by destroying the session, not unregistering.
                remove_from(entry->infos.registered_players, player);
            }
        }
        entry->infos.open_slots = open_slots_of(entry->infos);
        return true;
    }

    return false;
}

// --- The structs the game frees ---

void release_session_details_info(EOS_SessionDetails_Info* info) {
    if (info == 0) {
        return;
    }
    std::lock_guard<std::mutex> lock(g_info_mutex);
    g_details_infos.erase(info);
}

void release_active_session_info(EOS_ActiveSession_Info* info) {
    if (info == 0) {
        return;
    }
    std::lock_guard<std::mutex> lock(g_info_mutex);
    g_active_infos.erase(info);
}

void release_session_details_attribute(EOS_SessionDetails_Attribute* attribute) {
    if (attribute == 0) {
        return;
    }
    std::lock_guard<std::mutex> lock(g_info_mutex);
    g_attributes.erase(attribute);
}

} // namespace eosr
