#include "interfaces/lobby.h"

#include <cstring>
#include <memory>
#include <mutex>

#include "common/ids.h"
#include "core/callback_manager.h"
#include "core/settings.h"
#include "interfaces/connect.h"
#include "net/message_router.h"
#include "net/wire.h"
#include "platform/rng.h"

namespace eosr {

namespace {

const callback_type_id cb_create = 1;
const callback_type_id cb_destroy = 2;
const callback_type_id cb_join = 3;
const callback_type_id cb_leave = 4;
const callback_type_id cb_update = 5;
const callback_type_id cb_promote = 6;
const callback_type_id cb_kick = 7;
const callback_type_id cb_find = 8;
const callback_type_id cb_stub = 9;
const callback_type_id cb_notification = 10;
const callback_type_id cb_notify_update = 11;
const callback_type_id cb_notify_member_update = 12;
const callback_type_id cb_notify_member_status = 13;

const std::chrono::milliseconds search_timeout(5000);
const std::chrono::milliseconds join_timeout(5000);

const char* const key_bucket = EOS_LOBBY_SEARCH_BUCKET_ID;
const char* const key_min_members = EOS_LOBBY_SEARCH_MINCURRENTMEMBERS;
const char* const key_min_slots = EOS_LOBBY_SEARCH_MINSLOTSAVAILABLE;

struct common_completion_prefix {
    EOS_EResult result_code;
    void* client_data;
};

// The structs CopyInfo / CopyMemberInfo / CopyAttribute hand the game, and the storage behind them.
struct details_info_holder {
    EOS_LobbyDetails_Info info;
    std::string lobby_id;
    std::string bucket_id;
};

struct member_info_holder {
    EOS_LobbyDetails_MemberInfo info;
};

struct attribute_holder {
    EOS_Lobby_Attribute attribute;
    EOS_Lobby_AttributeData data;
    std::string key;
    std::string value;
};

std::mutex g_info_mutex;
std::map<void*, std::unique_ptr<details_info_holder> > g_details_infos;
std::map<void*, std::unique_ptr<member_info_holder> > g_member_infos;
std::map<void*, std::unique_ptr<attribute_holder> > g_attributes;

const char* duplicate(const std::string& text) {
    char* copy = new char[text.size() + 1];
    std::memcpy(copy, text.c_str(), text.size() + 1);
    return copy;
}

std::string generate_lobby_id() {
    static const char digits[] = "0123456789abcdef";
    u8 bytes[16];
    if (!platform::random_bytes(bytes, sizeof(bytes))) {
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

// The ApiVersion at which each EOS_Lobby_CreateLobbyOptions field arrived. A game built against an
// older SDK passes a *shorter* struct, so reading a field it does not have reads the game's own
// memory -- and BucketId and LobbyId are pointers we would then build a std::string from.
//
// Epic's current headers carry only _API_LATEST, so these come from the struct as it stood in the
// SDKs that shipped each version: 4 fields at v1 and 5 at v2 (the numbered structs, which stop at
// SDK 1.8), 11 at v7, and 13 at v8. That pins every field we read.
//
// Note v6 added *two* fields at once (bEnableRTCRoom and its LocalRTCOptions), which is why the
// count and the version part ways from there -- and why guessing a cutoff from a field's position
// gets LobbyId and bEnableJoinById wrong by one.
const i32 createlobby_with_allow_invites = 3;          // field 6
const i32 createlobby_with_bucket_id = 4;              // field 7
const i32 createlobby_with_disable_host_migration = 5; // field 8
const i32 createlobby_with_rtc_room = 6;               // fields 9 and 10
const i32 createlobby_with_lobby_id = 7;               // field 11
const i32 createlobby_with_join_by_id = 8;             // fields 12 and 13

template <class T>
bool compare_ordered(const T& mine, const T& theirs, i32 op) {
    // EOS_EComparisonOp: 0 EQUAL, 1 NOTEQUAL, 2 GREATERTHAN, 3 GREATERTHANOREQUAL, 4 LESSTHAN,
    // 5 LESSTHANOREQUAL. The remaining ops (distance/any-of) are not answerable here and never match.
    switch (op) {
        case 0: return mine == theirs;
        case 1: return !(mine == theirs);
        case 2: return mine > theirs;
        case 3: return mine >= theirs;
        case 4: return mine < theirs;
        case 5: return mine <= theirs;
        default: return false;
    }
}

bool read_attribute(const EOS_Lobby_AttributeData* data, session_attribute& out) {
    if (data == 0 || data->Key == 0 || data->Key[0] == '\0') {
        return false;
    }
    if (std::strlen(data->Key) > EOS_LOBBYMODIFICATION_MAX_ATTRIBUTE_LENGTH) {
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

void write_attribute(const session_attribute& from, EOS_Lobby_AttributeData& out,
                     std::string& key_storage, std::string& value_storage) {
    key_storage = from.key;
    out.ApiVersion = EOS_LOBBY_ATTRIBUTEDATA_API_LATEST;
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

bool compare_attribute(const session_attribute& mine, const session_attribute& theirs, i32 op) {
    if (mine.value_type != theirs.value_type) {
        return false;
    }
    switch (mine.value_type) {
        case 0: return compare_ordered<i64>(mine.as_bool ? 1 : 0, theirs.as_bool ? 1 : 0, op);
        case 1: return compare_ordered<i64>(mine.as_int64, theirs.as_int64, op);
        case 2: return compare_ordered<f64>(mine.as_double, theirs.as_double, op);
        default: return compare_ordered<std::string>(mine.as_string, theirs.as_string, op);
    }
}

const session_attribute* find_attribute(const std::vector<session_attribute>& attrs,
                                        const std::string& key) {
    for (std::size_t i = 0; i < attrs.size(); i++) {
        if (attrs[i].key == key) {
            return &attrs[i];
        }
    }
    return 0;
}

void upsert_attribute(std::vector<session_attribute>& attrs, const session_attribute& value) {
    for (std::size_t i = 0; i < attrs.size(); i++) {
        if (attrs[i].key == value.key) {
            attrs[i] = value;
            return;
        }
    }
    attrs.push_back(value);
}

// Who inherits a lobby when its owner leaves: the smallest remaining member id. Every member runs
// the same rule on the same roster, so they all agree on the heir without a round trip. Empty when
// no one is left to take it.
std::string elect_owner(const std::vector<lobby_member>& members, const std::string& leaving) {
    std::string chosen;
    for (std::size_t i = 0; i < members.size(); i++) {
        const std::string& id = members[i].user_id;
        if (id == leaving) {
            continue;
        }
        if (chosen.empty() || id < chosen) {
            chosen = id;
        }
    }
    return chosen;
}

void remove_attribute(std::vector<session_attribute>& attrs, const std::string& key) {
    for (std::size_t i = 0; i < attrs.size();) {
        if (attrs[i].key == key) {
            attrs.erase(attrs.begin() + i);
        } else {
            i++;
        }
    }
}

void fill_details_info(details_info_holder& holder, const lobby_infos& infos) {
    holder.lobby_id = infos.lobby_id;
    holder.bucket_id = infos.bucket_id;
    holder.info.ApiVersion = EOS_LOBBYDETAILS_INFO_API_LATEST;
    holder.info.LobbyId = holder.lobby_id.c_str();
    holder.info.LobbyOwnerUserId =
        infos.owner_id.empty() ? 0 : id_registry::instance().get_product_user_id(infos.owner_id);
    holder.info.PermissionLevel = static_cast<EOS_ELobbyPermissionLevel>(infos.permission_level);
    holder.info.AvailableSlots =
        infos.max_members > infos.members.size()
            ? infos.max_members - static_cast<u32>(infos.members.size())
            : 0;
    holder.info.MaxMembers = infos.max_members;
    holder.info.bAllowInvites = infos.allow_invites ? EOS_TRUE : EOS_FALSE;
    holder.info.BucketId = holder.bucket_id.c_str();
    holder.info.bAllowHostMigration = infos.allow_host_migration ? EOS_TRUE : EOS_FALSE;
    holder.info.bRTCRoomEnabled = infos.rtc_enabled ? EOS_TRUE : EOS_FALSE;
    holder.info.bAllowJoinById = infos.allow_join_by_id ? EOS_TRUE : EOS_FALSE;
    holder.info.bRejoinAfterKickRequiresInvite = EOS_FALSE;
    holder.info.bPresenceEnabled = EOS_FALSE;
    holder.info.AllowedPlatformIds = 0;
    holder.info.AllowedPlatformIdsCount = 0;
}

} // namespace

sdk_lobby::sdk_lobby(sdk_settings& settings, callback_manager& callbacks, message_router& network,
                     sdk_connect& connect)
    : settings_(settings), callbacks_(callbacks), network_(network), connect_(connect),
      next_search_id_(1), registered_(false) {}

sdk_lobby::~sdk_lobby() {
    emu_deinit();
}

void sdk_lobby::emu_init() {
    if (registered_) {
        return;
    }
    callbacks_.register_frame(this);
    callbacks_.register_callbacks(this);
    network_.register_listener(message_type::lobby_search, this);
    network_.register_listener(message_type::lobby_search_response, this);
    network_.register_listener(message_type::lobby_join_request, this);
    network_.register_listener(message_type::lobby_join_response, this);
    network_.register_listener(message_type::lobby_infos, this);
    network_.register_listener(message_type::lobby_leave, this);
    network_.register_listener(message_type::lobby_member_update, this);
    network_.register_listener(message_type::lobby_destroy, this);
    network_.register_listener(message_type::peer_disconnected, this);
    registered_ = true;
}

void sdk_lobby::emu_deinit() {
    if (!registered_) {
        return;
    }
    network_.unregister_listener(message_type::lobby_search, this);
    network_.unregister_listener(message_type::lobby_search_response, this);
    network_.unregister_listener(message_type::lobby_join_request, this);
    network_.unregister_listener(message_type::lobby_join_response, this);
    network_.unregister_listener(message_type::lobby_infos, this);
    network_.unregister_listener(message_type::lobby_leave, this);
    network_.unregister_listener(message_type::lobby_member_update, this);
    network_.unregister_listener(message_type::lobby_destroy, this);
    network_.unregister_listener(message_type::peer_disconnected, this);
    callbacks_.unregister_callbacks(this);
    callbacks_.unregister_frame(this);

    lobbies_.clear();
    modifications_.clear();
    searches_.clear();
    details_.clear();
    pending_finds_.clear();
    pending_joins_.clear();
    registered_ = false;
}

sdk_lobby::lobby* sdk_lobby::find_lobby(const std::string& lobby_id) {
    std::map<std::string, lobby>::iterator it = lobbies_.find(lobby_id);
    return (it != lobbies_.end()) ? &it->second : 0;
}

const sdk_lobby::lobby* sdk_lobby::find_lobby(const std::string& lobby_id) const {
    std::map<std::string, lobby>::const_iterator it = lobbies_.find(lobby_id);
    return (it != lobbies_.end()) ? &it->second : 0;
}

lobby_member* sdk_lobby::find_member(lobby_infos& infos, const std::string& user_id) {
    for (std::size_t i = 0; i < infos.members.size(); i++) {
        if (infos.members[i].user_id == user_id) {
            return &infos.members[i];
        }
    }
    return 0;
}

u32 sdk_lobby::open_slots_of(const lobby_infos& infos) const {
    return infos.max_members > infos.members.size()
               ? infos.max_members - static_cast<u32>(infos.members.size())
               : 0;
}

void sdk_lobby::send_to(const std::string& peer, message_type type, const byte_writer& payload) {
    net_envelope envelope;
    envelope.type_tag = static_cast<u16>(type);
    envelope.source_id = settings_.product_user_id();
    envelope.dest_id = peer;
    envelope.game_id = settings_.product_id();
    envelope.payload = payload.data();
    network_.send(envelope);
}

void sdk_lobby::broadcast_to_members(const lobby& entry, message_type type,
                                     const byte_writer& payload, const std::string& except) {
    for (std::size_t i = 0; i < entry.infos.members.size(); i++) {
        const std::string& member = entry.infos.members[i].user_id;
        if (member == settings_.product_user_id() || member == except) {
            continue;
        }
        send_to(member, type, payload);
    }
}

void sdk_lobby::broadcast_lobby(const lobby& entry) {
    byte_writer writer;
    serialize(writer, entry.infos);
    broadcast_to_members(entry, message_type::lobby_infos, writer, std::string());
}

void sdk_lobby::close_hosted_lobby(lobby& entry) {
    const std::string heir =
        entry.infos.allow_host_migration
            ? elect_owner(entry.infos.members, settings_.product_user_id())
            : std::string();
    if (heir.empty()) {
        // No one to hand it to: the lobby closes for everyone.
        lobby_destroy notice;
        notice.lobby_id = entry.infos.lobby_id;
        notice.reason = static_cast<i32>(EOS_ELobbyMemberStatus::EOS_LMS_CLOSED);
        byte_writer writer;
        serialize(writer, notice);
        broadcast_to_members(entry, message_type::lobby_destroy, writer, std::string());
        return;
    }
    // Migrate: name the heir, drop ourselves, and broadcast the new state as the outgoing owner --
    // recipients accept it because we are still the owner they know, and the heir becomes the host.
    entry.infos.owner_id = heir;
    for (std::size_t i = 0; i < entry.infos.members.size(); i++) {
        if (entry.infos.members[i].user_id == settings_.product_user_id()) {
            entry.infos.members.erase(entry.infos.members.begin() + i);
            break;
        }
    }
    entry.infos.available_slots = open_slots_of(entry.infos);
    broadcast_lobby(entry);
}

void sdk_lobby::deliver_id(callback_type_id type, std::size_t info_size,
                           completion_delegate delegate, void* client_data, EOS_EResult code,
                           const std::string& lobby_id) {
    if (delegate == 0) {
        return;
    }
    std::unique_ptr<frame_result> result(new frame_result());
    void* payload = result->create_callback(type, info_size, delegate);
    common_completion_prefix* prefix = static_cast<common_completion_prefix*>(payload);
    prefix->result_code = code;
    prefix->client_data = client_data;
    // Every id-bearing lobby callback puts the LobbyId right after the common prefix.
    char** id_field = reinterpret_cast<char**>(static_cast<char*>(payload) +
                                               sizeof(common_completion_prefix));
    *id_field = const_cast<char*>(duplicate(lobby_id));
    result->set_done(true);
    callbacks_.add_callback(this, std::move(result));
}

// A completion that carries only the common prefix and no LobbyId -- the LobbySearch Find callback,
// whose struct has no id field. Writing an id past its two fields would overflow it.
void sdk_lobby::deliver_result(callback_type_id type, std::size_t info_size,
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

bool sdk_lobby::lobby_matches(const lobby_infos& infos, const lobby_search& query) const {
    const bool by_id = !query.lobby_id.empty();
    const bool by_user = !query.target_user_id.empty();

    if (by_id && infos.lobby_id != query.lobby_id) {
        return false;
    }
    if (by_user) {
        bool present = false;
        for (std::size_t i = 0; i < infos.members.size(); i++) {
            if (infos.members[i].user_id == query.target_user_id) {
                present = true;
                break;
            }
        }
        if (!present) {
            return false;
        }
    }
    // An open attribute search only turns up lobbies anyone may advertise into. A search aimed at a
    // specific id or player is looking for that one and is not gated by permission.
    if (!by_id && !by_user &&
        infos.permission_level != static_cast<i32>(EOS_ELobbyPermissionLevel::EOS_LPL_PUBLICADVERTISED)) {
        return false;
    }

    for (std::size_t i = 0; i < query.parameters.size(); i++) {
        const search_parameter& parameter = query.parameters[i];
        const std::string& key = parameter.attribute.key;
        const i32 op = parameter.comparison_op;

        if (key == key_bucket) {
            if (parameter.attribute.value_type != 3 ||
                !compare_ordered<std::string>(infos.bucket_id, parameter.attribute.as_string, op)) {
                return false;
            }
            continue;
        }
        if (key == key_min_members) {
            if (parameter.attribute.value_type != 1 || // these keys are a minimum count: int64
                !compare_ordered<i64>(static_cast<i64>(infos.members.size()),
                                      parameter.attribute.as_int64, op == 0 ? 3 : op)) {
                return false;
            }
            continue;
        }
        if (key == key_min_slots) {
            if (parameter.attribute.value_type != 1 ||
                !compare_ordered<i64>(static_cast<i64>(open_slots_of(infos)),
                                      parameter.attribute.as_int64, op == 0 ? 3 : op)) {
                return false;
            }
            continue;
        }

        const session_attribute* attribute = find_attribute(infos.attributes, key);
        if (attribute == 0 || !compare_attribute(*attribute, parameter.attribute, op)) {
            return false;
        }
    }
    return true;
}

EOS_EResult sdk_lobby::emit_attribute(const session_attribute& from, EOS_Lobby_Attribute** out) {
    std::unique_ptr<attribute_holder> holder(new attribute_holder());
    write_attribute(from, holder->data, holder->key, holder->value);
    holder->attribute.ApiVersion = EOS_LOBBY_ATTRIBUTE_API_LATEST;
    holder->attribute.Data = &holder->data;
    holder->attribute.Visibility = static_cast<EOS_ELobbyAttributeVisibility>(from.advertisement);

    EOS_Lobby_Attribute* attribute = &holder->attribute;
    {
        std::lock_guard<std::mutex> lock(g_info_mutex);
        g_attributes[attribute] = std::move(holder);
    }
    *out = attribute;
    return EOS_EResult::EOS_Success;
}

// Fire a lobby-changed notification (the one a game re-reads its view on).
void fire_lobby_update(callback_manager& callbacks, i_run_callback* owner,
                       const std::string& lobby_id) {
    std::vector<EOS_NotificationId> ids = callbacks.notification_ids(owner, cb_notify_update);
    for (std::size_t n = 0; n < ids.size(); n++) {
        frame_result* note = callbacks.find_notification(owner, ids[n]);
        if (note == 0) {
            continue;
        }
        EOS_Lobby_LobbyUpdateReceivedCallbackInfo* info =
            note->get_callback<EOS_Lobby_LobbyUpdateReceivedCallbackInfo>();
        info->LobbyId = lobby_id.c_str(); // valid for the duration of the synchronous fire
        note->fire();
    }
}

void fire_member_update(callback_manager& callbacks, i_run_callback* owner,
                        const std::string& lobby_id, const std::string& target) {
    std::vector<EOS_NotificationId> ids = callbacks.notification_ids(owner, cb_notify_member_update);
    for (std::size_t n = 0; n < ids.size(); n++) {
        frame_result* note = callbacks.find_notification(owner, ids[n]);
        if (note == 0) {
            continue;
        }
        EOS_Lobby_LobbyMemberUpdateReceivedCallbackInfo* info =
            note->get_callback<EOS_Lobby_LobbyMemberUpdateReceivedCallbackInfo>();
        info->LobbyId = lobby_id.c_str();
        info->TargetUserId = id_registry::instance().get_product_user_id(target);
        note->fire();
    }
}

// Two members carry the same attributes? Compared in order, which is how they are applied, so a
// spurious extra notification is the worst a reorder could cause.
bool same_member_attributes(const lobby_member& a, const lobby_member& b) {
    if (a.attributes.size() != b.attributes.size()) {
        return false;
    }
    for (std::size_t i = 0; i < a.attributes.size(); i++) {
        const session_attribute& x = a.attributes[i];
        const session_attribute& y = b.attributes[i];
        if (x.key != y.key || x.value_type != y.value_type) {
            return false;
        }
        bool same = false;
        switch (x.value_type) {
            case 0: same = x.as_bool == y.as_bool; break;
            case 1: same = x.as_int64 == y.as_int64; break;
            case 2: same = x.as_double == y.as_double; break;
            default: same = x.as_string == y.as_string; break;
        }
        if (!same) {
            return false;
        }
    }
    return true;
}

const lobby_member* find_member_const(const std::vector<lobby_member>& members,
                                      const std::string& user_id) {
    for (std::size_t i = 0; i < members.size(); i++) {
        if (members[i].user_id == user_id) {
            return &members[i];
        }
    }
    return 0;
}

void fire_member_status(callback_manager& callbacks, i_run_callback* owner,
                        const std::string& lobby_id, const std::string& target,
                        EOS_ELobbyMemberStatus status) {
    std::vector<EOS_NotificationId> ids = callbacks.notification_ids(owner, cb_notify_member_status);
    for (std::size_t n = 0; n < ids.size(); n++) {
        frame_result* note = callbacks.find_notification(owner, ids[n]);
        if (note == 0) {
            continue;
        }
        EOS_Lobby_LobbyMemberStatusReceivedCallbackInfo* info =
            note->get_callback<EOS_Lobby_LobbyMemberStatusReceivedCallbackInfo>();
        info->LobbyId = lobby_id.c_str();
        info->TargetUserId =
            target.empty() ? 0 : id_registry::instance().get_product_user_id(target);
        info->CurrentStatus = status;
        note->fire();
    }
}

// --- Lobby lifecycle ---

void sdk_lobby::create_lobby(const EOS_Lobby_CreateLobbyOptions* options, void* client_data,
                             EOS_Lobby_OnCreateLobbyCallback delegate) {
    if (delegate == 0) {
        return;
    }
    if (options == 0 || !version_ok(options->ApiVersion, EOS_LOBBY_CREATELOBBY_API_LATEST) ||
        options->LocalUserId == 0 || options->MaxLobbyMembers == 0) {
        deliver_id(cb_create, sizeof(EOS_Lobby_CreateLobbyCallbackInfo),
                   reinterpret_cast<completion_delegate>(delegate), client_data,
                   EOS_EResult::EOS_InvalidParameters, std::string());
        return;
    }
    // Every field past the first four is read only once the caller's version says it is there. The
    // fallbacks are what the SDK did before each field existed, so an older game gets the behavior
    // its own SDK would have given it.
    const i32 version = options->ApiVersion;
    const char* requested_id =
        (version >= createlobby_with_lobby_id) ? options->LobbyId : 0;

    std::string lobby_id = (requested_id != 0) ? requested_id : generate_lobby_id();
    if (lobby_id.empty()) {
        deliver_id(cb_create, sizeof(EOS_Lobby_CreateLobbyCallbackInfo),
                   reinterpret_cast<completion_delegate>(delegate), client_data,
                   EOS_EResult::EOS_UnexpectedError, std::string());
        return;
    }
    if (find_lobby(lobby_id) != 0) {
        deliver_id(cb_create, sizeof(EOS_Lobby_CreateLobbyCallbackInfo),
                   reinterpret_cast<completion_delegate>(delegate), client_data,
                   EOS_EResult::EOS_Lobby_PresenceLobbyExists, lobby_id);
        return;
    }

    lobby created;
    created.local_user = options->LocalUserId->id_str;
    created.local_state = lobby::hosting;
    created.infos.lobby_id = lobby_id;
    created.infos.owner_id = created.local_user;
    const char* bucket =
        (version >= createlobby_with_bucket_id) ? options->BucketId : 0;
    created.infos.bucket_id = (bucket != 0) ? bucket : std::string();
    created.infos.permission_level = static_cast<i32>(options->PermissionLevel);
    created.infos.max_members = options->MaxLobbyMembers;
    // Invites were allowed before a game could say otherwise, and migration was not something a
    // game could turn off, so an older struct means both stay on.
    created.infos.allow_invites = (version < createlobby_with_allow_invites) ||
                                  (options->bAllowInvites == EOS_TRUE);
    created.infos.allow_host_migration =
        (version < createlobby_with_disable_host_migration) ||
        (options->bDisableHostMigration != EOS_TRUE);
    // RTC rooms and join-by-id did not exist at all before their fields did, so an older struct
    // means off.
    created.infos.allow_join_by_id = (version >= createlobby_with_join_by_id) &&
                                     (options->bEnableJoinById == EOS_TRUE);
    created.infos.rtc_enabled = (version >= createlobby_with_rtc_room) &&
                                (options->bEnableRTCRoom == EOS_TRUE);
    lobby_member owner;
    owner.user_id = created.local_user;
    created.infos.members.push_back(owner); // the host is the first member
    created.infos.available_slots = open_slots_of(created.infos);
    lobbies_[lobby_id] = created;

    deliver_id(cb_create, sizeof(EOS_Lobby_CreateLobbyCallbackInfo),
               reinterpret_cast<completion_delegate>(delegate), client_data,
               EOS_EResult::EOS_Success, lobby_id);
}

void sdk_lobby::destroy_lobby(const EOS_Lobby_DestroyLobbyOptions* options, void* client_data,
                              EOS_Lobby_OnDestroyLobbyCallback delegate) {
    if (delegate == 0) {
        return;
    }
    if (options == 0 || options->LobbyId == 0 || options->LocalUserId == 0) {
        deliver_id(cb_destroy, sizeof(EOS_Lobby_DestroyLobbyCallbackInfo),
                   reinterpret_cast<completion_delegate>(delegate), client_data,
                   EOS_EResult::EOS_InvalidParameters, std::string());
        return;
    }
    const std::string lobby_id = options->LobbyId;
    lobby* entry = find_lobby(lobby_id);
    if (entry == 0) {
        deliver_id(cb_destroy, sizeof(EOS_Lobby_DestroyLobbyCallbackInfo),
                   reinterpret_cast<completion_delegate>(delegate), client_data,
                   EOS_EResult::EOS_NotFound, lobby_id);
        return;
    }
    // Destroying a lobby we host ends it for everyone. Destroying one we joined is us leaving, so
    // we give our seat back instead.
    byte_writer writer;
    if (entry->local_state == lobby::hosting) {
        close_hosted_lobby(*entry);
    } else {
        lobby_member_update leaving;
        leaving.lobby_id = lobby_id;
        leaving.member.user_id = settings_.product_user_id();
        serialize(writer, leaving);
        send_to(entry->infos.owner_id, message_type::lobby_leave, writer);
    }
    lobbies_.erase(lobby_id);
    deliver_id(cb_destroy, sizeof(EOS_Lobby_DestroyLobbyCallbackInfo),
               reinterpret_cast<completion_delegate>(delegate), client_data,
               EOS_EResult::EOS_Success, lobby_id);
}

void sdk_lobby::leave_lobby(const EOS_Lobby_LeaveLobbyOptions* options, void* client_data,
                            EOS_Lobby_OnLeaveLobbyCallback delegate) {
    if (delegate == 0) {
        return;
    }
    if (options == 0 || options->LobbyId == 0 || options->LocalUserId == 0) {
        deliver_id(cb_leave, sizeof(EOS_Lobby_LeaveLobbyCallbackInfo),
                   reinterpret_cast<completion_delegate>(delegate), client_data,
                   EOS_EResult::EOS_InvalidParameters, std::string());
        return;
    }
    const std::string lobby_id = options->LobbyId;
    lobby* entry = find_lobby(lobby_id);
    if (entry == 0) {
        deliver_id(cb_leave, sizeof(EOS_Lobby_LeaveLobbyCallbackInfo),
                   reinterpret_cast<completion_delegate>(delegate), client_data,
                   EOS_EResult::EOS_NotFound, lobby_id);
        return;
    }
    byte_writer writer;
    if (entry->local_state == lobby::hosting) {
        close_hosted_lobby(*entry);
    } else {
        lobby_member_update leaving;
        leaving.lobby_id = lobby_id;
        leaving.member.user_id = settings_.product_user_id();
        serialize(writer, leaving);
        send_to(entry->infos.owner_id, message_type::lobby_leave, writer);
    }
    lobbies_.erase(lobby_id);
    deliver_id(cb_leave, sizeof(EOS_Lobby_LeaveLobbyCallbackInfo),
               reinterpret_cast<completion_delegate>(delegate), client_data,
               EOS_EResult::EOS_Success, lobby_id);
}

EOS_EResult sdk_lobby::update_lobby_modification(
    const EOS_Lobby_UpdateLobbyModificationOptions* options, EOS_HLobbyModification* out) {
    if (out == 0) {
        return EOS_EResult::EOS_InvalidParameters;
    }
    *out = 0;
    if (options == 0 || options->LobbyId == 0 || options->LocalUserId == 0) {
        return EOS_EResult::EOS_InvalidParameters;
    }
    lobby* entry = find_lobby(options->LobbyId);
    if (entry == 0) {
        return EOS_EResult::EOS_NotFound;
    }
    std::unique_ptr<modification_object> object(new modification_object());
    object->lobby_id = options->LobbyId;
    object->local_user = options->LocalUserId->id_str;
    object->creating = false;
    object->infos = entry->infos;
    *out = reinterpret_cast<EOS_HLobbyModification>(modifications_.add(std::move(object)));
    return EOS_EResult::EOS_Success;
}

void sdk_lobby::update_lobby(const EOS_Lobby_UpdateLobbyOptions* options, void* client_data,
                             EOS_Lobby_OnUpdateLobbyCallback delegate) {
    if (delegate == 0) {
        return;
    }
    modification_object* object =
        (options != 0) ? modifications_.find(options->LobbyModificationHandle) : 0;
    if (object == 0 || !version_ok(options->ApiVersion, EOS_LOBBY_UPDATELOBBY_API_LATEST)) {
        deliver_id(cb_update, sizeof(EOS_Lobby_UpdateLobbyCallbackInfo),
                   reinterpret_cast<completion_delegate>(delegate), client_data,
                   EOS_EResult::EOS_InvalidParameters, std::string());
        return;
    }
    lobby* entry = find_lobby(object->lobby_id);
    if (entry == 0) {
        deliver_id(cb_update, sizeof(EOS_Lobby_UpdateLobbyCallbackInfo),
                   reinterpret_cast<completion_delegate>(delegate), client_data,
                   EOS_EResult::EOS_NotFound, object->lobby_id);
        return;
    }
    // Only the host may change lobby-wide settings; anyone may change their own member attributes.
    const bool is_owner = entry->infos.owner_id == settings_.product_user_id();
    if (is_owner) {
        entry->infos.bucket_id = object->infos.bucket_id;
        entry->infos.permission_level = object->infos.permission_level;
        entry->infos.max_members = object->infos.max_members;
        entry->infos.allow_invites = object->infos.allow_invites;
        entry->infos.attributes = object->infos.attributes;
    }
    lobby_member* self = find_member(entry->infos, settings_.product_user_id());
    if (self != 0) {
        for (std::size_t i = 0; i < object->member_deleted.size(); i++) {
            remove_attribute(self->attributes, object->member_deleted[i]);
        }
        for (std::size_t i = 0; i < object->member_set.size(); i++) {
            upsert_attribute(self->attributes, object->member_set[i]);
        }
    }
    entry->infos.available_slots = open_slots_of(entry->infos);

    if (is_owner) {
        broadcast_lobby(*entry);
    } else {
        // A member's own change goes to the host, which is the source of truth and rebroadcasts.
        lobby_member_update update;
        update.lobby_id = entry->infos.lobby_id;
        if (self != 0) {
            update.member = *self;
        } else {
            update.member.user_id = settings_.product_user_id();
        }
        byte_writer writer;
        serialize(writer, update);
        send_to(entry->infos.owner_id, message_type::lobby_member_update, writer);
    }
    deliver_id(cb_update, sizeof(EOS_Lobby_UpdateLobbyCallbackInfo),
               reinterpret_cast<completion_delegate>(delegate), client_data,
               EOS_EResult::EOS_Success, entry->infos.lobby_id);
}

void sdk_lobby::promote_member(const EOS_Lobby_PromoteMemberOptions* options, void* client_data,
                               EOS_Lobby_OnPromoteMemberCallback delegate) {
    if (delegate == 0) {
        return;
    }
    if (options == 0 || options->LobbyId == 0 || options->LocalUserId == 0 ||
        options->TargetUserId == 0) {
        deliver_id(cb_promote, sizeof(EOS_Lobby_PromoteMemberCallbackInfo),
                   reinterpret_cast<completion_delegate>(delegate), client_data,
                   EOS_EResult::EOS_InvalidParameters, std::string());
        return;
    }
    const std::string lobby_id = options->LobbyId;
    lobby* entry = find_lobby(lobby_id);
    if (entry == 0) {
        deliver_id(cb_promote, sizeof(EOS_Lobby_PromoteMemberCallbackInfo),
                   reinterpret_cast<completion_delegate>(delegate), client_data,
                   EOS_EResult::EOS_NotFound, lobby_id);
        return;
    }
    if (entry->local_state != lobby::hosting) {
        deliver_id(cb_promote, sizeof(EOS_Lobby_PromoteMemberCallbackInfo),
                   reinterpret_cast<completion_delegate>(delegate), client_data,
                   EOS_EResult::EOS_Lobby_NotOwner, lobby_id);
        return;
    }
    const std::string target = options->TargetUserId->id_str;
    if (find_member(entry->infos, target) == 0) {
        deliver_id(cb_promote, sizeof(EOS_Lobby_PromoteMemberCallbackInfo),
                   reinterpret_cast<completion_delegate>(delegate), client_data,
                   EOS_EResult::EOS_NotFound, lobby_id);
        return;
    }
    // The former host stays a member; the lobby simply has a new owner, which we no longer are.
    entry->infos.owner_id = target;
    entry->local_state = lobby::joined;
    broadcast_lobby(*entry);
    fire_member_status(callbacks_, this, lobby_id, target, EOS_ELobbyMemberStatus::EOS_LMS_PROMOTED);
    deliver_id(cb_promote, sizeof(EOS_Lobby_PromoteMemberCallbackInfo),
               reinterpret_cast<completion_delegate>(delegate), client_data,
               EOS_EResult::EOS_Success, lobby_id);
}

void sdk_lobby::kick_member(const EOS_Lobby_KickMemberOptions* options, void* client_data,
                            EOS_Lobby_OnKickMemberCallback delegate) {
    if (delegate == 0) {
        return;
    }
    if (options == 0 || options->LobbyId == 0 || options->LocalUserId == 0 ||
        options->TargetUserId == 0) {
        deliver_id(cb_kick, sizeof(EOS_Lobby_KickMemberCallbackInfo),
                   reinterpret_cast<completion_delegate>(delegate), client_data,
                   EOS_EResult::EOS_InvalidParameters, std::string());
        return;
    }
    const std::string lobby_id = options->LobbyId;
    lobby* entry = find_lobby(lobby_id);
    if (entry == 0 || entry->local_state != lobby::hosting) {
        deliver_id(cb_kick, sizeof(EOS_Lobby_KickMemberCallbackInfo),
                   reinterpret_cast<completion_delegate>(delegate), client_data,
                   entry == 0 ? EOS_EResult::EOS_NotFound : EOS_EResult::EOS_Lobby_NotOwner,
                   lobby_id);
        return;
    }
    const std::string target = options->TargetUserId->id_str;
    if (target == entry->infos.owner_id || find_member(entry->infos, target) == 0) {
        deliver_id(cb_kick, sizeof(EOS_Lobby_KickMemberCallbackInfo),
                   reinterpret_cast<completion_delegate>(delegate), client_data,
                   EOS_EResult::EOS_InvalidParameters, lobby_id);
        return;
    }
    for (std::size_t i = 0; i < entry->infos.members.size(); i++) {
        if (entry->infos.members[i].user_id == target) {
            entry->infos.members.erase(entry->infos.members.begin() + i);
            break;
        }
    }
    entry->infos.available_slots = open_slots_of(entry->infos);
    // Tell the kicked player it is out, then bring the rest up to date.
    lobby_destroy gone;
    gone.lobby_id = lobby_id;
    byte_writer writer;
    serialize(writer, gone);
    send_to(target, message_type::lobby_destroy, writer);
    broadcast_lobby(*entry);
    fire_member_status(callbacks_, this, lobby_id, target, EOS_ELobbyMemberStatus::EOS_LMS_KICKED);
    deliver_id(cb_kick, sizeof(EOS_Lobby_KickMemberCallbackInfo),
               reinterpret_cast<completion_delegate>(delegate), client_data,
               EOS_EResult::EOS_Success, lobby_id);
}

// --- Joining ---

void sdk_lobby::join_lobby(const EOS_Lobby_JoinLobbyOptions* options, void* client_data,
                           EOS_Lobby_OnJoinLobbyCallback delegate) {
    if (delegate == 0) {
        return;
    }
    details_object* details =
        (options != 0) ? details_.find(options->LobbyDetailsHandle) : 0;
    if (details == 0 || options->LocalUserId == 0) {
        deliver_id(cb_join, sizeof(EOS_Lobby_JoinLobbyCallbackInfo),
                   reinterpret_cast<completion_delegate>(delegate), client_data,
                   EOS_EResult::EOS_InvalidParameters, std::string());
        return;
    }
    const lobby_infos& target = details->infos;
    if (find_lobby(target.lobby_id) != 0) {
        deliver_id(cb_join, sizeof(EOS_Lobby_JoinLobbyCallbackInfo),
                   reinterpret_cast<completion_delegate>(delegate), client_data,
                   EOS_EResult::EOS_Lobby_PresenceLobbyExists, target.lobby_id);
        return;
    }
    // We only join a host we have actually met on the mesh.
    if (!connect_.is_known_peer(target.owner_id)) {
        deliver_id(cb_join, sizeof(EOS_Lobby_JoinLobbyCallbackInfo),
                   reinterpret_cast<completion_delegate>(delegate), client_data,
                   EOS_EResult::EOS_Lobby_NotOwner, target.lobby_id);
        return;
    }

    lobby joining;
    joining.local_user = options->LocalUserId->id_str;
    joining.local_state = lobby::joining;
    joining.infos = target; // an optimistic copy, replaced by the host's authoritative broadcast
    lobbies_[target.lobby_id] = joining;

    std::unique_ptr<frame_result> result(new frame_result());
    void* payload = result->create_callback(cb_join, sizeof(EOS_Lobby_JoinLobbyCallbackInfo),
                                            reinterpret_cast<completion_delegate>(delegate));
    common_completion_prefix* prefix = static_cast<common_completion_prefix*>(payload);
    prefix->client_data = client_data;
    char** id_field = reinterpret_cast<char**>(static_cast<char*>(payload) +
                                               sizeof(common_completion_prefix));
    *id_field = const_cast<char*>(duplicate(target.lobby_id));
    pending_join pending;
    pending.lobby_id = target.lobby_id;
    pending.deadline = std::chrono::steady_clock::now() + join_timeout;
    pending_joins_[result.get()] = pending;
    callbacks_.add_callback(this, std::move(result));

    lobby_join_request request;
    request.lobby_id = target.lobby_id;
    request.member.user_id = joining.local_user;
    byte_writer writer;
    serialize(writer, request);
    send_to(target.owner_id, message_type::lobby_join_request, writer);
}

void sdk_lobby::join_lobby_by_id(const EOS_Lobby_JoinLobbyByIdOptions* options, void* client_data,
                                 EOS_Lobby_OnJoinLobbyByIdCallback delegate) {
    if (delegate == 0) {
        return;
    }
    if (options == 0 || options->LobbyId == 0 || options->LocalUserId == 0) {
        deliver_id(cb_join, sizeof(EOS_Lobby_JoinLobbyByIdCallbackInfo),
                   reinterpret_cast<completion_delegate>(delegate), client_data,
                   EOS_EResult::EOS_InvalidParameters, std::string());
        return;
    }
    const std::string lobby_id = options->LobbyId;
    if (find_lobby(lobby_id) != 0) {
        deliver_id(cb_join, sizeof(EOS_Lobby_JoinLobbyByIdCallbackInfo),
                   reinterpret_cast<completion_delegate>(delegate), client_data,
                   EOS_EResult::EOS_Lobby_PresenceLobbyExists, lobby_id);
        return;
    }

    lobby joining;
    joining.local_user = options->LocalUserId->id_str;
    joining.local_state = lobby::joining;
    joining.infos.lobby_id = lobby_id;
    lobbies_[lobby_id] = joining;

    std::unique_ptr<frame_result> result(new frame_result());
    void* payload = result->create_callback(cb_join, sizeof(EOS_Lobby_JoinLobbyByIdCallbackInfo),
                                            reinterpret_cast<completion_delegate>(delegate));
    common_completion_prefix* prefix = static_cast<common_completion_prefix*>(payload);
    prefix->client_data = client_data;
    char** id_field = reinterpret_cast<char**>(static_cast<char*>(payload) +
                                               sizeof(common_completion_prefix));
    *id_field = const_cast<char*>(duplicate(lobby_id));
    pending_join pending;
    pending.lobby_id = lobby_id;
    pending.deadline = std::chrono::steady_clock::now() + join_timeout;
    pending_joins_[result.get()] = pending;
    callbacks_.add_callback(this, std::move(result));

    // We do not know which peer hosts this id, so we ask them all; only its owner answers.
    lobby_join_request request;
    request.lobby_id = lobby_id;
    request.by_id = true;
    request.member.user_id = joining.local_user;
    byte_writer writer;
    serialize(writer, request);
    const std::vector<std::string> peers = network_.peer_ids();
    for (std::size_t i = 0; i < peers.size(); i++) {
        send_to(peers[i], message_type::lobby_join_request, writer);
    }
}

// --- Search ---

EOS_EResult sdk_lobby::create_lobby_search(const EOS_Lobby_CreateLobbySearchOptions* options,
                                           EOS_HLobbySearch* out) {
    if (out == 0) {
        return EOS_EResult::EOS_InvalidParameters;
    }
    *out = 0;
    if (options == 0 || !version_ok(options->ApiVersion, EOS_LOBBY_CREATELOBBYSEARCH_API_LATEST)) {
        return EOS_EResult::EOS_InvalidParameters;
    }
    std::unique_ptr<search_object> object(new search_object());
    object->max_results = options->MaxResults > 0 ? options->MaxResults : 1;
    object->searching = false;
    *out = reinterpret_cast<EOS_HLobbySearch>(searches_.add(std::move(object)));
    return EOS_EResult::EOS_Success;
}

EOS_EResult sdk_lobby::search_set_lobby_id(void* handle,
                                           const EOS_LobbySearch_SetLobbyIdOptions* options) {
    search_object* object = searches_.find(handle);
    if (object == 0 || options == 0 || options->LobbyId == 0) {
        return EOS_EResult::EOS_InvalidParameters;
    }
    if (!version_ok(options->ApiVersion, EOS_LOBBYSEARCH_SETLOBBYID_API_LATEST)) {
        return EOS_EResult::EOS_IncompatibleVersion;
    }
    object->query.lobby_id = options->LobbyId;
    return EOS_EResult::EOS_Success;
}

EOS_EResult sdk_lobby::search_set_target_user(void* handle,
                                              const EOS_LobbySearch_SetTargetUserIdOptions* options) {
    search_object* object = searches_.find(handle);
    if (object == 0 || options == 0 || options->TargetUserId == 0) {
        return EOS_EResult::EOS_InvalidParameters;
    }
    if (!version_ok(options->ApiVersion, EOS_LOBBYSEARCH_SETTARGETUSERID_API_LATEST)) {
        return EOS_EResult::EOS_IncompatibleVersion;
    }
    object->query.target_user_id = options->TargetUserId->id_str;
    return EOS_EResult::EOS_Success;
}

EOS_EResult sdk_lobby::search_set_parameter(void* handle,
                                            const EOS_LobbySearch_SetParameterOptions* options) {
    search_object* object = searches_.find(handle);
    if (object == 0 || options == 0 || options->Parameter == 0) {
        return EOS_EResult::EOS_InvalidParameters;
    }
    if (!version_ok(options->ApiVersion, EOS_LOBBYSEARCH_SETPARAMETER_API_LATEST)) {
        return EOS_EResult::EOS_IncompatibleVersion;
    }
    search_parameter parameter;
    if (!read_attribute(options->Parameter, parameter.attribute)) {
        return EOS_EResult::EOS_InvalidParameters;
    }
    parameter.comparison_op = static_cast<i32>(options->ComparisonOp);
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

EOS_EResult sdk_lobby::search_remove_parameter(void* handle,
                                               const EOS_LobbySearch_RemoveParameterOptions* options) {
    search_object* object = searches_.find(handle);
    if (object == 0 || options == 0 || options->Key == 0) {
        return EOS_EResult::EOS_InvalidParameters;
    }
    if (!version_ok(options->ApiVersion, EOS_LOBBYSEARCH_REMOVEPARAMETER_API_LATEST)) {
        return EOS_EResult::EOS_IncompatibleVersion;
    }
    const i32 op = static_cast<i32>(options->ComparisonOp);
    for (std::size_t i = 0; i < object->query.parameters.size(); i++) {
        if (object->query.parameters[i].attribute.key == options->Key &&
            object->query.parameters[i].comparison_op == op) {
            object->query.parameters.erase(object->query.parameters.begin() + i);
            return EOS_EResult::EOS_Success;
        }
    }
    return EOS_EResult::EOS_NotFound;
}

EOS_EResult sdk_lobby::search_set_max_results(void* handle,
                                              const EOS_LobbySearch_SetMaxResultsOptions* options) {
    search_object* object = searches_.find(handle);
    if (object == 0 || options == 0) {
        return EOS_EResult::EOS_InvalidParameters;
    }
    if (!version_ok(options->ApiVersion, EOS_LOBBYSEARCH_SETMAXRESULTS_API_LATEST)) {
        return EOS_EResult::EOS_IncompatibleVersion;
    }
    if (options->MaxResults == 0 || options->MaxResults > EOS_LOBBY_MAX_SEARCH_RESULTS) {
        return EOS_EResult::EOS_InvalidParameters;
    }
    object->max_results = options->MaxResults;
    return EOS_EResult::EOS_Success;
}

void sdk_lobby::search_find(void* handle, const EOS_LobbySearch_FindOptions* options,
                            void* client_data, EOS_LobbySearch_OnFindCallback delegate) {
    if (delegate == 0) {
        return;
    }
    search_object* object = searches_.find(handle);
    if (object == 0 || options == 0 || options->LocalUserId == 0) {
        deliver_result(cb_find, sizeof(EOS_LobbySearch_FindCallbackInfo),
                       reinterpret_cast<completion_delegate>(delegate), client_data,
                       EOS_EResult::EOS_InvalidParameters);
        return;
    }
    // Exactly one search mode may be set: by lobby id, by target user, or by attribute parameters.
    // The header says Find fails when they are combined, and equally when none is given.
    const bool by_id = !object->query.lobby_id.empty();
    const bool by_user = !object->query.target_user_id.empty();
    const bool by_params = !object->query.parameters.empty();
    const int modes = (by_id ? 1 : 0) + (by_user ? 1 : 0) + (by_params ? 1 : 0);
    if (modes != 1) {
        deliver_result(cb_find, sizeof(EOS_LobbySearch_FindCallbackInfo),
                       reinterpret_cast<completion_delegate>(delegate), client_data,
                       EOS_EResult::EOS_InvalidParameters);
        return;
    }
    if (object->searching) {
        deliver_result(cb_find, sizeof(EOS_LobbySearch_FindCallbackInfo),
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

    // We only ever surface lobbies we host; a joined copy is the host's to advertise.
    std::map<std::string, lobby>::const_iterator local = lobbies_.begin();
    for (; local != lobbies_.end(); ++local) {
        if (object->results.size() >= object->max_results) {
            break;
        }
        if (local->second.local_state == lobby::hosting &&
            lobby_matches(local->second.infos, object->query)) {
            object->results.push_back(local->second.infos);
        }
    }

    byte_writer writer;
    serialize(writer, object->query);
    const std::vector<std::string> peers = network_.peer_ids();
    object->awaiting.clear();
    for (std::size_t i = 0; i < peers.size(); i++) {
        if (by_user && peers[i] != object->query.target_user_id) {
            continue;
        }
        send_to(peers[i], message_type::lobby_search, writer);
        object->awaiting.insert(peers[i]);
    }
    object->deadline = std::chrono::steady_clock::now() + search_timeout;

    std::unique_ptr<frame_result> result(new frame_result());
    void* payload = result->create_callback(cb_find, sizeof(EOS_LobbySearch_FindCallbackInfo),
                                            reinterpret_cast<completion_delegate>(delegate));
    common_completion_prefix* prefix = static_cast<common_completion_prefix*>(payload);
    prefix->result_code = EOS_EResult::EOS_Success;
    prefix->client_data = client_data;
    pending_finds_[result.get()] = handle;
    callbacks_.add_callback(this, std::move(result));
}

u32 sdk_lobby::search_result_count(void* handle) const {
    const search_object* object = searches_.find(handle);
    return (object != 0 && !object->searching) ? static_cast<u32>(object->results.size()) : 0;
}

EOS_EResult sdk_lobby::search_copy_result(
    void* handle, const EOS_LobbySearch_CopySearchResultByIndexOptions* options,
    EOS_HLobbyDetails* out) {
    if (out == 0) {
        return EOS_EResult::EOS_InvalidParameters;
    }
    *out = 0;
    search_object* object = searches_.find(handle);
    if (object == 0 || options == 0) {
        return EOS_EResult::EOS_InvalidParameters;
    }
    if (object->searching || options->LobbyIndex >= object->results.size()) {
        return EOS_EResult::EOS_InvalidParameters;
    }
    std::unique_ptr<details_object> copy(new details_object());
    copy->infos = object->results[options->LobbyIndex];
    *out = reinterpret_cast<EOS_HLobbyDetails>(details_.add(std::move(copy)));
    return EOS_EResult::EOS_Success;
}

void sdk_lobby::search_release(void* handle) {
    searches_.release(handle);
}

EOS_EResult sdk_lobby::copy_lobby_details_handle(
    const EOS_Lobby_CopyLobbyDetailsHandleOptions* options, EOS_HLobbyDetails* out) {
    if (out == 0) {
        return EOS_EResult::EOS_InvalidParameters;
    }
    *out = 0;
    if (options == 0 || options->LobbyId == 0) {
        return EOS_EResult::EOS_InvalidParameters;
    }
    const lobby* entry = find_lobby(options->LobbyId);
    if (entry == 0) {
        return EOS_EResult::EOS_NotFound;
    }
    std::unique_ptr<details_object> copy(new details_object());
    copy->infos = entry->infos;
    *out = reinterpret_cast<EOS_HLobbyDetails>(details_.add(std::move(copy)));
    return EOS_EResult::EOS_Success;
}

EOS_EResult sdk_lobby::get_rtc_room_name(const EOS_Lobby_GetRTCRoomNameOptions* options,
                                         char* out_buffer, u32* inout_buffer_length) const {
    if (inout_buffer_length == 0) {
        return EOS_EResult::EOS_InvalidParameters;
    }
    if (options == 0 || options->LobbyId == 0) {
        return EOS_EResult::EOS_InvalidParameters;
    }
    const lobby* entry = find_lobby(options->LobbyId);
    if (entry == 0) {
        return EOS_EResult::EOS_NotFound;
    }
    // The room name is the lobby id with a fixed prefix; the voice room itself is not carried.
    const std::string name = std::string("rtc:") + entry->infos.lobby_id;
    const u32 needed = static_cast<u32>(name.size()) + 1;
    if (out_buffer == 0 || *inout_buffer_length < needed) {
        *inout_buffer_length = needed;
        return EOS_EResult::EOS_LimitExceeded;
    }
    std::memcpy(out_buffer, name.c_str(), name.size() + 1);
    *inout_buffer_length = needed;
    return EOS_EResult::EOS_Success;
}

EOS_Bool sdk_lobby::is_rtc_room_connected(const EOS_Lobby_IsRTCRoomConnectedOptions*) const {
    return EOS_FALSE; // voice rooms are named but not carried
}

// --- Notifications ---

EOS_NotificationId sdk_lobby::add_notify_lobby_update_received(
    void* client_data, EOS_Lobby_OnLobbyUpdateReceivedCallback delegate) {
    if (delegate == 0) {
        return EOS_INVALID_NOTIFICATIONID;
    }
    std::unique_ptr<frame_result> result(new frame_result());
    void* payload = result->create_callback(cb_notify_update,
                                            sizeof(EOS_Lobby_LobbyUpdateReceivedCallbackInfo),
                                            reinterpret_cast<completion_delegate>(delegate));
    *static_cast<void**>(payload) = client_data;
    return callbacks_.add_notification(this, std::move(result), "LobbyUpdateReceived");
}

EOS_NotificationId sdk_lobby::add_notify_lobby_member_update_received(
    void* client_data, EOS_Lobby_OnLobbyMemberUpdateReceivedCallback delegate) {
    if (delegate == 0) {
        return EOS_INVALID_NOTIFICATIONID;
    }
    std::unique_ptr<frame_result> result(new frame_result());
    void* payload = result->create_callback(cb_notify_member_update,
                                            sizeof(EOS_Lobby_LobbyMemberUpdateReceivedCallbackInfo),
                                            reinterpret_cast<completion_delegate>(delegate));
    *static_cast<void**>(payload) = client_data;
    return callbacks_.add_notification(this, std::move(result), "LobbyMemberUpdateReceived");
}

EOS_NotificationId sdk_lobby::add_notify_lobby_member_status_received(
    void* client_data, EOS_Lobby_OnLobbyMemberStatusReceivedCallback delegate) {
    if (delegate == 0) {
        return EOS_INVALID_NOTIFICATIONID;
    }
    std::unique_ptr<frame_result> result(new frame_result());
    void* payload = result->create_callback(cb_notify_member_status,
                                            sizeof(EOS_Lobby_LobbyMemberStatusReceivedCallbackInfo),
                                            reinterpret_cast<completion_delegate>(delegate));
    *static_cast<void**>(payload) = client_data;
    return callbacks_.add_notification(this, std::move(result), "LobbyMemberStatusReceived");
}

void sdk_lobby::remove_notify(EOS_NotificationId id) {
    callbacks_.remove_notification(this, id);
}

EOS_NotificationId sdk_lobby::add_stub_notification(void* client_data, completion_delegate delegate,
                                                    std::size_t info_size, const char* event) {
    if (delegate == 0) {
        return EOS_INVALID_NOTIFICATIONID;
    }
    std::unique_ptr<frame_result> result(new frame_result());
    void* payload = result->create_callback(cb_notification, info_size, delegate);
    *static_cast<void**>(payload) = client_data;
    return callbacks_.add_notification(this, std::move(result), event);
}

void sdk_lobby::queue_stub_result(void* client_data, completion_delegate delegate,
                                  std::size_t info_size) {
    if (delegate == 0) {
        return;
    }
    std::unique_ptr<frame_result> result(new frame_result());
    void* payload = result->create_callback(cb_stub, info_size, delegate);
    common_completion_prefix* prefix = static_cast<common_completion_prefix*>(payload);
    prefix->result_code = EOS_EResult::EOS_NotImplemented;
    prefix->client_data = client_data;
    result->set_done(true);
    callbacks_.add_callback(this, std::move(result));
}

// --- LobbyModification sub-handle ---

EOS_EResult sdk_lobby::modification_set_bucket_id(
    void* handle, const EOS_LobbyModification_SetBucketIdOptions* options) {
    modification_object* object = modifications_.find(handle);
    if (object == 0 || options == 0 || options->BucketId == 0) {
        return EOS_EResult::EOS_InvalidParameters;
    }
    object->infos.bucket_id = options->BucketId;
    return EOS_EResult::EOS_Success;
}

EOS_EResult sdk_lobby::modification_set_permission_level(
    void* handle, const EOS_LobbyModification_SetPermissionLevelOptions* options) {
    modification_object* object = modifications_.find(handle);
    if (object == 0 || options == 0) {
        return EOS_EResult::EOS_InvalidParameters;
    }
    object->infos.permission_level = static_cast<i32>(options->PermissionLevel);
    return EOS_EResult::EOS_Success;
}

EOS_EResult sdk_lobby::modification_set_max_members(
    void* handle, const EOS_LobbyModification_SetMaxMembersOptions* options) {
    modification_object* object = modifications_.find(handle);
    if (object == 0 || options == 0 || options->MaxMembers == 0) {
        return EOS_EResult::EOS_InvalidParameters;
    }
    object->infos.max_members = options->MaxMembers;
    return EOS_EResult::EOS_Success;
}

EOS_EResult sdk_lobby::modification_set_invites_allowed(
    void* handle, const EOS_LobbyModification_SetInvitesAllowedOptions* options) {
    modification_object* object = modifications_.find(handle);
    if (object == 0 || options == 0) {
        return EOS_EResult::EOS_InvalidParameters;
    }
    object->infos.allow_invites = (options->bInvitesAllowed == EOS_TRUE);
    return EOS_EResult::EOS_Success;
}

EOS_EResult sdk_lobby::modification_set_allowed_platform_ids(
    void* handle, const EOS_LobbyModification_SetAllowedPlatformIdsOptions* options) {
    modification_object* object = modifications_.find(handle);
    if (object == 0 || options == 0) {
        return EOS_EResult::EOS_InvalidParameters;
    }
    return EOS_EResult::EOS_Success; // platform gating is accepted and ignored
}

EOS_EResult sdk_lobby::modification_add_attribute(
    void* handle, const EOS_LobbyModification_AddAttributeOptions* options) {
    modification_object* object = modifications_.find(handle);
    if (object == 0 || options == 0) {
        return EOS_EResult::EOS_InvalidParameters;
    }
    session_attribute attribute;
    if (!read_attribute(options->Attribute, attribute)) {
        return EOS_EResult::EOS_InvalidParameters;
    }
    if (object->infos.attributes.size() >= EOS_LOBBYMODIFICATION_MAX_ATTRIBUTES &&
        find_attribute(object->infos.attributes, attribute.key) == 0) {
        return EOS_EResult::EOS_LimitExceeded;
    }
    attribute.advertisement = static_cast<i32>(options->Visibility);
    upsert_attribute(object->infos.attributes, attribute);
    return EOS_EResult::EOS_Success;
}

EOS_EResult sdk_lobby::modification_remove_attribute(
    void* handle, const EOS_LobbyModification_RemoveAttributeOptions* options) {
    modification_object* object = modifications_.find(handle);
    if (object == 0 || options == 0 || options->Key == 0) {
        return EOS_EResult::EOS_InvalidParameters;
    }
    remove_attribute(object->infos.attributes, options->Key);
    return EOS_EResult::EOS_Success;
}

EOS_EResult sdk_lobby::modification_add_member_attribute(
    void* handle, const EOS_LobbyModification_AddMemberAttributeOptions* options) {
    modification_object* object = modifications_.find(handle);
    if (object == 0 || options == 0) {
        return EOS_EResult::EOS_InvalidParameters;
    }
    session_attribute attribute;
    if (!read_attribute(options->Attribute, attribute)) {
        return EOS_EResult::EOS_InvalidParameters;
    }
    attribute.advertisement = static_cast<i32>(options->Visibility);
    upsert_attribute(object->member_set, attribute);
    return EOS_EResult::EOS_Success;
}

EOS_EResult sdk_lobby::modification_remove_member_attribute(
    void* handle, const EOS_LobbyModification_RemoveMemberAttributeOptions* options) {
    modification_object* object = modifications_.find(handle);
    if (object == 0 || options == 0 || options->Key == 0) {
        return EOS_EResult::EOS_InvalidParameters;
    }
    object->member_deleted.push_back(options->Key);
    return EOS_EResult::EOS_Success;
}

void sdk_lobby::modification_release(void* handle) {
    modifications_.release(handle);
}

// --- LobbyDetails sub-handle ---

EOS_EResult sdk_lobby::details_copy_info(void* handle, EOS_LobbyDetails_Info** out) {
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
    EOS_LobbyDetails_Info* info = &holder->info;
    {
        std::lock_guard<std::mutex> lock(g_info_mutex);
        g_details_infos[info] = std::move(holder);
    }
    *out = info;
    return EOS_EResult::EOS_Success;
}

EOS_ProductUserId sdk_lobby::details_get_lobby_owner(
    void* handle, const EOS_LobbyDetails_GetLobbyOwnerOptions*) {
    const details_object* object = details_.find(handle);
    if (object == 0 || object->infos.owner_id.empty()) {
        return 0;
    }
    return id_registry::instance().get_product_user_id(object->infos.owner_id);
}

u32 sdk_lobby::details_attribute_count(void* handle) const {
    const details_object* object = details_.find(handle);
    return (object != 0) ? static_cast<u32>(object->infos.attributes.size()) : 0;
}

EOS_EResult sdk_lobby::details_copy_attribute_by_index(
    void* handle, const EOS_LobbyDetails_CopyAttributeByIndexOptions* options,
    EOS_Lobby_Attribute** out) {
    if (out == 0) {
        return EOS_EResult::EOS_InvalidParameters;
    }
    *out = 0;
    details_object* object = details_.find(handle);
    if (object == 0 || options == 0) {
        return EOS_EResult::EOS_InvalidParameters;
    }
    if (options->AttrIndex >= object->infos.attributes.size()) {
        return EOS_EResult::EOS_NotFound;
    }
    return emit_attribute(object->infos.attributes[options->AttrIndex], out);
}

EOS_EResult sdk_lobby::details_copy_attribute_by_key(
    void* handle, const EOS_LobbyDetails_CopyAttributeByKeyOptions* options,
    EOS_Lobby_Attribute** out) {
    if (out == 0) {
        return EOS_EResult::EOS_InvalidParameters;
    }
    *out = 0;
    details_object* object = details_.find(handle);
    if (object == 0 || options == 0 || options->AttrKey == 0) {
        return EOS_EResult::EOS_InvalidParameters;
    }
    const session_attribute* attribute = find_attribute(object->infos.attributes, options->AttrKey);
    if (attribute == 0) {
        return EOS_EResult::EOS_NotFound;
    }
    return emit_attribute(*attribute, out);
}

u32 sdk_lobby::details_member_count(void* handle,
                                    const EOS_LobbyDetails_GetMemberCountOptions*) const {
    const details_object* object = details_.find(handle);
    return (object != 0) ? static_cast<u32>(object->infos.members.size()) : 0;
}

EOS_ProductUserId sdk_lobby::details_member_by_index(
    void* handle, const EOS_LobbyDetails_GetMemberByIndexOptions* options) const {
    const details_object* object = details_.find(handle);
    if (object == 0 || options == 0 || options->MemberIndex >= object->infos.members.size()) {
        return 0;
    }
    return id_registry::instance().get_product_user_id(
        object->infos.members[options->MemberIndex].user_id);
}

EOS_EResult sdk_lobby::details_copy_member_info(
    void* handle, const EOS_LobbyDetails_CopyMemberInfoOptions* options,
    EOS_LobbyDetails_MemberInfo** out) {
    if (out == 0) {
        return EOS_EResult::EOS_InvalidParameters;
    }
    *out = 0;
    details_object* object = details_.find(handle);
    if (object == 0 || options == 0 || options->TargetUserId == 0) {
        return EOS_EResult::EOS_InvalidParameters;
    }
    const std::string target = options->TargetUserId->id_str;
    for (std::size_t i = 0; i < object->infos.members.size(); i++) {
        if (object->infos.members[i].user_id != target) {
            continue;
        }
        std::unique_ptr<member_info_holder> holder(new member_info_holder());
        holder->info.ApiVersion = EOS_LOBBYDETAILS_MEMBERINFO_API_LATEST;
        holder->info.UserId = id_registry::instance().get_product_user_id(target);
        holder->info.Platform = static_cast<EOS_OnlinePlatformType>(object->infos.members[i].platform);
        holder->info.bAllowsCrossplay = EOS_TRUE;
        EOS_LobbyDetails_MemberInfo* info = &holder->info;
        {
            std::lock_guard<std::mutex> lock(g_info_mutex);
            g_member_infos[info] = std::move(holder);
        }
        *out = info;
        return EOS_EResult::EOS_Success;
    }
    return EOS_EResult::EOS_NotFound;
}

u32 sdk_lobby::details_member_attribute_count(
    void* handle, const EOS_LobbyDetails_GetMemberAttributeCountOptions* options) const {
    const details_object* object = details_.find(handle);
    if (object == 0 || options == 0 || options->TargetUserId == 0) {
        return 0;
    }
    const std::string target = options->TargetUserId->id_str;
    for (std::size_t i = 0; i < object->infos.members.size(); i++) {
        if (object->infos.members[i].user_id == target) {
            return static_cast<u32>(object->infos.members[i].attributes.size());
        }
    }
    return 0;
}

EOS_EResult sdk_lobby::details_copy_member_attribute_by_index(
    void* handle, const EOS_LobbyDetails_CopyMemberAttributeByIndexOptions* options,
    EOS_Lobby_Attribute** out) {
    if (out == 0) {
        return EOS_EResult::EOS_InvalidParameters;
    }
    *out = 0;
    details_object* object = details_.find(handle);
    if (object == 0 || options == 0 || options->TargetUserId == 0) {
        return EOS_EResult::EOS_InvalidParameters;
    }
    const std::string target = options->TargetUserId->id_str;
    for (std::size_t i = 0; i < object->infos.members.size(); i++) {
        if (object->infos.members[i].user_id != target) {
            continue;
        }
        if (options->AttrIndex >= object->infos.members[i].attributes.size()) {
            return EOS_EResult::EOS_NotFound;
        }
        return emit_attribute(object->infos.members[i].attributes[options->AttrIndex], out);
    }
    return EOS_EResult::EOS_NotFound;
}

EOS_EResult sdk_lobby::details_copy_member_attribute_by_key(
    void* handle, const EOS_LobbyDetails_CopyMemberAttributeByKeyOptions* options,
    EOS_Lobby_Attribute** out) {
    if (out == 0) {
        return EOS_EResult::EOS_InvalidParameters;
    }
    *out = 0;
    details_object* object = details_.find(handle);
    if (object == 0 || options == 0 || options->TargetUserId == 0 || options->AttrKey == 0) {
        return EOS_EResult::EOS_InvalidParameters;
    }
    const std::string target = options->TargetUserId->id_str;
    for (std::size_t i = 0; i < object->infos.members.size(); i++) {
        if (object->infos.members[i].user_id != target) {
            continue;
        }
        const session_attribute* attribute =
            find_attribute(object->infos.members[i].attributes, options->AttrKey);
        if (attribute == 0) {
            return EOS_EResult::EOS_NotFound;
        }
        return emit_attribute(*attribute, out);
    }
    return EOS_EResult::EOS_NotFound;
}

void sdk_lobby::details_release(void* handle) {
    details_.release(handle);
}

// --- network ---

bool sdk_lobby::on_network_message(const net_envelope& message) {
    if (message.source_id.empty() || message.source_id == settings_.product_user_id()) {
        return true;
    }

    if (message.type_tag == static_cast<u16>(message_type::peer_disconnected)) {
        // A peer that leaves takes its membership with it. If it owned a lobby we are in, that
        // lobby is gone; if it was a member of one we host, we drop it and tell the rest. We work
        // from a snapshot of the lobby ids and re-look-up each, because a notification fired here
        // may re-enter and change the lobby map, which would invalidate a live iterator.
        std::vector<std::string> ids;
        ids.reserve(lobbies_.size());
        std::map<std::string, lobby>::const_iterator scan = lobbies_.begin();
        for (; scan != lobbies_.end(); ++scan) {
            ids.push_back(scan->first);
        }
        for (std::size_t k = 0; k < ids.size(); k++) {
            lobby* entry = find_lobby(ids[k]);
            if (entry == 0) {
                continue; // a fired notification already removed it
            }
            if (entry->local_state != lobby::hosting &&
                entry->infos.owner_id == message.source_id) {
                // The owner is gone. Drop it from the roster, then either hand the lobby to a
                // surviving member (if migration is on) or let it close. Every member elects the
                // same heir from the same roster, so they converge without a round trip.
                for (std::size_t i = 0; i < entry->infos.members.size(); i++) {
                    if (entry->infos.members[i].user_id == message.source_id) {
                        entry->infos.members.erase(entry->infos.members.begin() + i);
                        break;
                    }
                }
                const std::string heir =
                    entry->infos.allow_host_migration
                        ? elect_owner(entry->infos.members, message.source_id)
                        : std::string();
                if (heir.empty()) {
                    lobbies_.erase(ids[k]);
                    fire_member_status(callbacks_, this, ids[k], message.source_id,
                                       EOS_ELobbyMemberStatus::EOS_LMS_CLOSED);
                } else {
                    entry->infos.owner_id = heir;
                    entry->infos.available_slots = open_slots_of(entry->infos);
                    if (heir == settings_.product_user_id()) {
                        entry->local_state = lobby::hosting;
                        broadcast_lobby(*entry); // the new host solidifies the state
                    } else {
                        entry->local_state = lobby::joined;
                    }
                    fire_member_status(callbacks_, this, ids[k], heir,
                                       EOS_ELobbyMemberStatus::EOS_LMS_PROMOTED);
                    fire_lobby_update(callbacks_, this, ids[k]);
                }
            } else if (entry->local_state == lobby::hosting &&
                       find_member(entry->infos, message.source_id) != 0) {
                for (std::size_t i = 0; i < entry->infos.members.size(); i++) {
                    if (entry->infos.members[i].user_id == message.source_id) {
                        entry->infos.members.erase(entry->infos.members.begin() + i);
                        break;
                    }
                }
                entry->infos.available_slots = open_slots_of(entry->infos);
                broadcast_lobby(*entry);
                fire_member_status(callbacks_, this, ids[k], message.source_id,
                                   EOS_ELobbyMemberStatus::EOS_LMS_DISCONNECTED);
            }
        }
        return true;
    }

    if (message.game_id != settings_.product_id()) {
        return true;
    }

    byte_reader reader(message.payload.data(), message.payload.size());

    if (message.type_tag == static_cast<u16>(message_type::lobby_search)) {
        lobby_search query;
        if (!deserialize(reader, query)) {
            return true;
        }
        lobby_search_response answer;
        answer.search_id = query.search_id;
        std::map<std::string, lobby>::const_iterator it = lobbies_.begin();
        for (; it != lobbies_.end(); ++it) {
            if (it->second.local_state == lobby::hosting &&
                lobby_matches(it->second.infos, query)) {
                answer.lobbies.push_back(it->second.infos);
            }
        }
        byte_writer writer;
        serialize(writer, answer);
        send_to(message.source_id, message_type::lobby_search_response, writer);
        return true;
    }

    if (message.type_tag == static_cast<u16>(message_type::lobby_search_response)) {
        lobby_search_response answer;
        if (!deserialize(reader, answer)) {
            return true;
        }
        std::map<frame_result*, void*>::iterator it = pending_finds_.begin();
        for (; it != pending_finds_.end(); ++it) {
            search_object* object = searches_.find(it->second);
            if (object == 0 || !object->searching || object->query.search_id != answer.search_id) {
                continue;
            }
            if (object->awaiting.find(message.source_id) == object->awaiting.end()) {
                continue;
            }
            object->awaiting.erase(message.source_id);
            for (std::size_t l = 0; l < answer.lobbies.size(); l++) {
                if (object->results.size() >= object->max_results) {
                    break;
                }
                // Only a lobby the responder actually hosts may come back on its connection; a
                // result naming another owner would redirect a later JoinLobby to an unrelated peer.
                if (answer.lobbies[l].owner_id != message.source_id) {
                    continue;
                }
                if (lobby_matches(answer.lobbies[l], object->query)) {
                    object->results.push_back(answer.lobbies[l]);
                }
            }
        }
        return true;
    }

    if (message.type_tag == static_cast<u16>(message_type::lobby_join_request)) {
        lobby_join_request request;
        if (!deserialize(reader, request)) {
            return true;
        }
        lobby* entry = find_lobby(request.lobby_id);
        lobby_join_response answer;
        answer.lobby_id = request.lobby_id;
        answer.player_id = message.source_id;
        if (entry == 0 || entry->local_state != lobby::hosting) {
            return true; // not our lobby to admit anyone into
        }
        if (!connect_.is_known_peer(message.source_id)) {
            answer.reason = static_cast<i32>(EOS_EResult::EOS_Lobby_NotOwner);
        } else if (request.by_id && !entry->infos.allow_join_by_id) {
            answer.reason = static_cast<i32>(EOS_EResult::EOS_Lobby_NotAllowed);
        } else if (find_member(entry->infos, message.source_id) != 0) {
            answer.reason = static_cast<i32>(EOS_EResult::EOS_Success);
        } else if (entry->infos.members.size() >= entry->infos.max_members) {
            answer.reason = static_cast<i32>(EOS_EResult::EOS_Lobby_TooManyPlayers);
        } else {
            lobby_member joined = request.member;
            joined.user_id = message.source_id; // the connection decides who this is, not the payload
            entry->infos.members.push_back(joined);
            entry->infos.available_slots = open_slots_of(entry->infos);
            answer.reason = static_cast<i32>(EOS_EResult::EOS_Success);
        }
        byte_writer writer;
        serialize(writer, answer);
        send_to(message.source_id, message_type::lobby_join_response, writer);
        if (answer.reason == static_cast<i32>(EOS_EResult::EOS_Success)) {
            broadcast_lobby(*entry);
            fire_member_status(callbacks_, this, entry->infos.lobby_id, message.source_id,
                               EOS_ELobbyMemberStatus::EOS_LMS_JOINED);
        }
        return true;
    }

    if (message.type_tag == static_cast<u16>(message_type::lobby_join_response)) {
        lobby_join_response answer;
        if (!deserialize(reader, answer)) {
            return true;
        }
        lobby* entry = find_lobby(answer.lobby_id);
        if (entry == 0) {
            return true;
        }
        // The host pronounces on a join. A join by id does not yet know who hosts it, so the first
        // authenticated peer to answer is the owner we record; a join from details already knows the
        // owner, and only that owner may answer.
        if (!entry->infos.owner_id.empty() && entry->infos.owner_id != message.source_id) {
            return true;
        }
        if (answer.player_id != settings_.product_user_id()) {
            return true;
        }
        bool waiting = false;
        std::map<frame_result*, pending_join>::iterator it = pending_joins_.begin();
        for (; it != pending_joins_.end(); ++it) {
            if (it->second.lobby_id != answer.lobby_id) {
                continue;
            }
            common_completion_prefix* prefix =
                it->first->get_callback<common_completion_prefix>();
            prefix->result_code = static_cast<EOS_EResult>(answer.reason);
            it->first->set_done(true);
            waiting = true;
            break;
        }
        if (!waiting) {
            return true;
        }
        if (answer.reason == static_cast<i32>(EOS_EResult::EOS_Success)) {
            entry->infos.owner_id = message.source_id; // learned, for a by-id join
            entry->local_state = lobby::joined;
        } else if (entry->local_state == lobby::joining) {
            lobbies_.erase(answer.lobby_id);
        }
        return true;
    }

    if (message.type_tag == static_cast<u16>(message_type::lobby_infos)) {
        lobby_infos infos;
        if (!deserialize(reader, infos)) {
            return true;
        }
        lobby* entry = find_lobby(infos.lobby_id);
        if (entry == 0 || entry->local_state == lobby::hosting) {
            return true;
        }
        // Only our lobby's current owner speaks for its state -- checked against the owner we know,
        // not the one the message claims. This refuses a peer trying to seize a lobby we are in, and
        // still accepts the owner's own promotion broadcast (whose new owner_id names someone else).
        if (message.source_id != entry->infos.owner_id) {
            return true;
        }
        // Diff the roster the owner sent against what we held, so a member that joined, left, had
        // its attributes changed, or was promoted raises the specific notification a game listens
        // for -- not only the generic lobby-changed one.
        const lobby_infos previous = entry->infos;
        const std::string keep_user = entry->local_user;
        entry->infos = infos;
        entry->local_user = keep_user;
        // A promotion can hand us ownership; from then on we are the one advertising it.
        entry->local_state =
            (infos.owner_id == settings_.product_user_id()) ? lobby::hosting : lobby::joined;

        for (std::size_t i = 0; i < infos.members.size(); i++) {
            const lobby_member* was = find_member_const(previous.members, infos.members[i].user_id);
            if (was == 0) {
                fire_member_status(callbacks_, this, infos.lobby_id, infos.members[i].user_id,
                                   EOS_ELobbyMemberStatus::EOS_LMS_JOINED);
            } else if (!same_member_attributes(*was, infos.members[i])) {
                fire_member_update(callbacks_, this, infos.lobby_id, infos.members[i].user_id);
            }
        }
        for (std::size_t i = 0; i < previous.members.size(); i++) {
            if (find_member_const(infos.members, previous.members[i].user_id) == 0) {
                fire_member_status(callbacks_, this, infos.lobby_id, previous.members[i].user_id,
                                   EOS_ELobbyMemberStatus::EOS_LMS_LEFT);
            }
        }
        if (!infos.owner_id.empty() && infos.owner_id != previous.owner_id) {
            fire_member_status(callbacks_, this, infos.lobby_id, infos.owner_id,
                               EOS_ELobbyMemberStatus::EOS_LMS_PROMOTED);
        }
        fire_lobby_update(callbacks_, this, infos.lobby_id);
        return true;
    }

    if (message.type_tag == static_cast<u16>(message_type::lobby_leave)) {
        lobby_member_update notice;
        if (!deserialize(reader, notice)) {
            return true;
        }
        lobby* entry = find_lobby(notice.lobby_id);
        if (entry == 0 || entry->local_state != lobby::hosting) {
            return true;
        }
        // A member may only take itself out; the connection says who is asking.
        const std::string leaver = message.source_id;
        bool removed = false;
        for (std::size_t i = 0; i < entry->infos.members.size(); i++) {
            if (entry->infos.members[i].user_id == leaver) {
                entry->infos.members.erase(entry->infos.members.begin() + i);
                removed = true;
                break;
            }
        }
        if (removed) {
            entry->infos.available_slots = open_slots_of(entry->infos);
            broadcast_lobby(*entry);
            fire_member_status(callbacks_, this, entry->infos.lobby_id, leaver,
                               EOS_ELobbyMemberStatus::EOS_LMS_LEFT);
        }
        return true;
    }

    if (message.type_tag == static_cast<u16>(message_type::lobby_member_update)) {
        lobby_member_update notice;
        if (!deserialize(reader, notice)) {
            return true;
        }
        lobby* entry = find_lobby(notice.lobby_id);
        if (entry == 0 || entry->local_state != lobby::hosting) {
            return true;
        }
        // A member sets only its own attributes; the connection, not the payload, names it.
        lobby_member* member = find_member(entry->infos, message.source_id);
        if (member == 0) {
            return true;
        }
        member->attributes = notice.member.attributes;
        broadcast_lobby(*entry);
        return true;
    }

    if (message.type_tag == static_cast<u16>(message_type::lobby_destroy)) {
        lobby_destroy notice;
        if (!deserialize(reader, notice)) {
            return true;
        }
        lobby* entry = find_lobby(notice.lobby_id);
        if (entry == 0 || entry->local_state == lobby::hosting ||
            entry->infos.owner_id != message.source_id) {
            return true;
        }
        lobbies_.erase(notice.lobby_id);
        // The reason on the wire tells us whether this was a kick aimed at us or the whole lobby
        // closing, so the game hears the one that happened.
        fire_member_status(callbacks_, this, notice.lobby_id, settings_.product_user_id(),
                           static_cast<EOS_ELobbyMemberStatus>(notice.reason));
        return true;
    }

    return false;
}

// --- i_run_callback ---

bool sdk_lobby::cb_run_frame() {
    return true;
}

bool sdk_lobby::run_callbacks(frame_result& result) {
    const std::chrono::steady_clock::time_point now = std::chrono::steady_clock::now();

    std::map<frame_result*, void*>::iterator find_it = pending_finds_.find(&result);
    if (find_it != pending_finds_.end()) {
        search_object* object = searches_.find(find_it->second);
        if (object == 0) {
            return true;
        }
        if (object->awaiting.empty() || now > object->deadline) {
            object->searching = false;
            return true;
        }
        return false;
    }

    std::map<frame_result*, pending_join>::iterator join_it = pending_joins_.find(&result);
    if (join_it != pending_joins_.end()) {
        if (result.done()) {
            return true;
        }
        if (now > join_it->second.deadline) {
            common_completion_prefix* prefix = result.get_callback<common_completion_prefix>();
            prefix->result_code = EOS_EResult::EOS_TimedOut;
            lobby* entry = find_lobby(join_it->second.lobby_id);
            if (entry != 0 && entry->local_state == lobby::joining) {
                lobbies_.erase(join_it->second.lobby_id);
            }
            return true;
        }
        return false;
    }
    return false;
}

void sdk_lobby::free_callback(frame_result& result) {
    pending_finds_.erase(&result);
    pending_joins_.erase(&result);

    const callback_type_id type = result.type_id();
    if (type == cb_create || type == cb_destroy || type == cb_join || type == cb_leave ||
        type == cb_update || type == cb_promote || type == cb_kick) {
        // Every id-bearing lobby callback owns a duplicated LobbyId right after the common prefix.
        char** id_field = reinterpret_cast<char**>(
            reinterpret_cast<char*>(result.get_callback<common_completion_prefix>()) +
            sizeof(common_completion_prefix));
        delete[] *id_field;
        *id_field = 0;
    }
}

// --- the structs the game frees ---

void release_lobby_details_info(EOS_LobbyDetails_Info* info) {
    if (info == 0) {
        return;
    }
    std::lock_guard<std::mutex> lock(g_info_mutex);
    g_details_infos.erase(info);
}

void release_lobby_details_member_info(EOS_LobbyDetails_MemberInfo* info) {
    if (info == 0) {
        return;
    }
    std::lock_guard<std::mutex> lock(g_info_mutex);
    g_member_infos.erase(info);
}

void release_lobby_attribute(EOS_Lobby_Attribute* attribute) {
    if (attribute == 0) {
        return;
    }
    std::lock_guard<std::mutex> lock(g_info_mutex);
    g_attributes.erase(attribute);
}

} // namespace eosr
