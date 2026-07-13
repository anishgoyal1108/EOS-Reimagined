#include "interfaces/presence.h"

#include <cstring>
#include <memory>
#include <mutex>

#include "common/ids.h"
#include "core/callback_manager.h"
#include "core/settings.h"
#include "net/message_router.h"
#include "net/wire.h"

namespace eosr {

namespace {

const callback_type_id cb_query = 1;
const callback_type_id cb_set = 2;
const callback_type_id cb_presence_changed = 3;
const callback_type_id cb_join_accepted = 4;

// A query that never hears back cannot hang forever.
const std::chrono::milliseconds query_timeout(1000);

// The header caps on rich-presence data. A game that overruns them is refused rather than trusted.
const std::size_t max_data_keys = EOS_PRESENCE_DATA_MAX_KEYS;
const std::size_t max_key_length = EOS_PRESENCE_DATA_MAX_KEY_LENGTH;
const std::size_t max_value_length = EOS_PRESENCE_DATA_MAX_VALUE_LENGTH;
const std::size_t max_rich_text_length = EOS_PRESENCE_RICH_TEXT_MAX_VALUE_LENGTH;
const std::size_t max_join_info_length = EOS_PRESENCEMODIFICATION_JOININFO_MAX_LENGTH;

struct common_completion_prefix {
    EOS_EResult result_code;
    void* client_data;
};

// The struct CopyPresence hands out, and the strings it points at. It is freed through the free
// function below, so it outlives any one platform and lives here.
struct presence_info_holder {
    EOS_Presence_Info info;
    std::string product_id;
    std::string product_version;
    std::string platform;
    std::string rich_text;
    std::string product_name;
    std::string integrated_platform;
    std::vector<EOS_Presence_DataRecord> records;
    std::vector<std::string> keys;
    std::vector<std::string> values;
};

std::mutex g_info_mutex;
std::map<void*, std::unique_ptr<presence_info_holder> > g_presence_infos;

bool version_ok(i32 version, i32 latest) {
    return version > 0 && version <= latest;
}

std::string text_or_empty(const char* value) {
    return (value != 0) ? value : std::string();
}

// Reject a presence a well-behaved peer could never have sent: fields past the ABI caps, a status
// outside the enum, or more data records than allowed. The same emulator enforces all of these
// before it broadcasts, so a violation means the message is malformed or hostile.
bool presence_within_caps(const presence_info& info) {
    if (info.status < static_cast<i32>(EOS_Presence_EStatus::EOS_PS_Offline) ||
        info.status > static_cast<i32>(EOS_Presence_EStatus::EOS_PS_DoNotDisturb)) {
        return false;
    }
    if (info.rich_text.size() > max_rich_text_length ||
        info.join_info.size() > max_join_info_length) {
        return false;
    }
    if (info.records.size() > max_data_keys) {
        return false;
    }
    for (std::size_t i = 0; i < info.records.size(); i++) {
        if (info.records[i].key.size() > max_key_length ||
            info.records[i].value.size() > max_value_length) {
            return false;
        }
    }
    return true;
}

bool presence_equal(const presence_info& a, const presence_info& b) {
    if (a.status != b.status || a.rich_text != b.rich_text || a.join_info != b.join_info ||
        a.product_id != b.product_id || a.product_version != b.product_version ||
        a.platform != b.platform || a.product_name != b.product_name ||
        a.integrated_platform != b.integrated_platform ||
        a.records.size() != b.records.size()) {
        return false;
    }
    for (std::size_t i = 0; i < a.records.size(); i++) {
        if (a.records[i].key != b.records[i].key || a.records[i].value != b.records[i].value) {
            return false;
        }
    }
    return true;
}

// Copy our internal presence into the EOS_Presence_Info the game frees. Every string it points at
// is owned by the holder so it lives exactly as long as the struct does.
EOS_Presence_Info* build_presence_info(const presence_info& from) {
    std::unique_ptr<presence_info_holder> holder(new presence_info_holder());
    holder->product_id = from.product_id;
    holder->product_version = from.product_version;
    holder->platform = from.platform;
    holder->rich_text = from.rich_text;
    holder->product_name = from.product_name;
    holder->integrated_platform = from.integrated_platform;

    holder->keys.reserve(from.records.size());
    holder->values.reserve(from.records.size());
    for (std::size_t i = 0; i < from.records.size(); i++) {
        holder->keys.push_back(from.records[i].key);
        holder->values.push_back(from.records[i].value);
    }
    // Fill the record array only after the string vectors have stopped growing, so the pointers
    // into them cannot dangle.
    holder->records.resize(from.records.size());
    for (std::size_t i = 0; i < from.records.size(); i++) {
        holder->records[i].ApiVersion = EOS_PRESENCE_DATARECORD_API_LATEST;
        holder->records[i].Key = holder->keys[i].c_str();
        holder->records[i].Value = holder->values[i].c_str();
    }

    holder->info.ApiVersion = EOS_PRESENCE_INFO_API_LATEST;
    holder->info.Status = static_cast<EOS_Presence_EStatus>(from.status);
    holder->info.UserId = id_registry::instance().get_epic_account_id(from.epic_id);
    holder->info.ProductId = holder->product_id.c_str();
    holder->info.ProductVersion = holder->product_version.c_str();
    holder->info.Platform = holder->platform.c_str();
    holder->info.RichText = holder->rich_text.c_str();
    holder->info.RecordsCount = static_cast<i32>(holder->records.size());
    holder->info.Records = holder->records.empty() ? 0 : holder->records.data();
    holder->info.ProductName = holder->product_name.c_str();
    holder->info.IntegratedPlatform = holder->integrated_platform.c_str();

    EOS_Presence_Info* info = &holder->info;
    {
        std::lock_guard<std::mutex> lock(g_info_mutex);
        g_presence_infos[info] = std::move(holder);
    }
    return info;
}

} // namespace

sdk_presence::sdk_presence(sdk_settings& settings, callback_manager& callbacks,
                           message_router& network)
    : settings_(settings), callbacks_(callbacks), network_(network), registered_(false) {}

sdk_presence::~sdk_presence() {
    emu_deinit();
}

void sdk_presence::emu_init() {
    if (registered_) {
        return;
    }
    callbacks_.register_frame(this);
    callbacks_.register_callbacks(this);
    network_.register_listener(message_type::presence_request, this);
    network_.register_listener(message_type::presence_info, this);
    network_.register_listener(message_type::peer_connected, this);
    network_.register_listener(message_type::peer_disconnected, this);
    seed_myself();
    registered_ = true;
}

void sdk_presence::emu_deinit() {
    if (!registered_) {
        return;
    }
    network_.unregister_listener(message_type::presence_request, this);
    network_.unregister_listener(message_type::presence_info, this);
    network_.unregister_listener(message_type::peer_connected, this);
    network_.unregister_listener(message_type::peer_disconnected, this);
    callbacks_.unregister_callbacks(this);
    callbacks_.unregister_frame(this);

    presences_.clear();
    epic_owner_.clear();
    modifications_.clear();
    pending_queries_.clear();
    registered_ = false;
}

// Our own presence starts as "online", playing this product. A game overrides the rest with
// SetPresence; until then this is what a peer that asks about us gets.
void sdk_presence::seed_myself() {
    const std::string& me = settings_.epic_account_id();
    if (me.empty()) {
        return;
    }
    presence_info self;
    self.epic_id = me;
    self.status = static_cast<i32>(EOS_Presence_EStatus::EOS_PS_Online);
    self.product_id = settings_.product_id();
    self.product_name = settings_.product_id();
    self.product_version = "1.0";
#if defined(_WIN32)
    self.platform = "Windows";
#else
    self.platform = "Linux";
#endif
    presences_[me] = self;
}

presence_info* sdk_presence::find_presence(const std::string& epic_id) {
    std::map<std::string, presence_info>::iterator it = presences_.find(epic_id);
    return (it != presences_.end()) ? &it->second : 0;
}

const presence_info* sdk_presence::find_presence(const std::string& epic_id) const {
    std::map<std::string, presence_info>::const_iterator it = presences_.find(epic_id);
    return (it != presences_.end()) ? &it->second : 0;
}

void sdk_presence::query_presence(const EOS_Presence_QueryPresenceOptions* options,
                                  void* client_data,
                                  EOS_Presence_OnQueryPresenceCompleteCallback delegate) {
    if (delegate == 0) {
        return;
    }
    if (options == 0 || !version_ok(options->ApiVersion, EOS_PRESENCE_QUERYPRESENCE_API_LATEST) ||
        options->LocalUserId == 0 || options->TargetUserId == 0 ||
        !options->LocalUserId->valid || !options->TargetUserId->valid) {
        deliver_query(client_data, delegate, std::string(), std::string(),
                      EOS_EResult::EOS_InvalidParameters);
        return;
    }
    const std::string local = options->LocalUserId->id_str;
    const std::string target = options->TargetUserId->id_str;
    if (local != settings_.epic_account_id()) {
        deliver_query(client_data, delegate, local, target, EOS_EResult::EOS_InvalidUser);
        return;
    }

    // A query about someone whose presence we already hold settles at once -- ourselves always, and
    // any peer we have heard from. The header says a query need not be made once HasPresence is
    // true, so answering from cache is what keeps the two from disagreeing (a cached-but-departed
    // friend would otherwise time out to NotFound while HasPresence still says yes).
    if (find_presence(target) != 0) {
        deliver_query(client_data, delegate, local, target, EOS_EResult::EOS_Success);
        return;
    }

    // Ask whoever owns that account to send its presence. The answer, or a timeout, settles it.
    std::unique_ptr<frame_result> result(new frame_result());
    void* payload = result->create_callback(
        cb_query, sizeof(EOS_Presence_QueryPresenceCallbackInfo),
        reinterpret_cast<completion_delegate>(delegate));
    EOS_Presence_QueryPresenceCallbackInfo* info =
        static_cast<EOS_Presence_QueryPresenceCallbackInfo*>(payload);
    info->ClientData = client_data;
    info->LocalUserId = options->LocalUserId;
    info->TargetUserId = options->TargetUserId;

    pending_query pending;
    pending.local_id = local;
    pending.target_id = target;
    pending.deadline = std::chrono::steady_clock::now() + query_timeout;
    frame_result* handle = result.get();
    pending_queries_[handle] = pending;
    callbacks_.add_callback(this, std::move(result));

    presence_request request;
    request.target_epic_id = target;
    byte_writer writer;
    serialize(writer, request);
    const std::vector<std::string> peers = network_.peer_ids();
    for (std::size_t i = 0; i < peers.size(); i++) {
        net_envelope envelope;
        envelope.type_tag = static_cast<u16>(message_type::presence_request);
        envelope.source_id = settings_.product_user_id();
        envelope.dest_id = peers[i];
        envelope.game_id = settings_.product_id();
        envelope.payload = writer.data();
        network_.send(envelope);
    }
}

void sdk_presence::deliver_query(void* client_data,
                                 EOS_Presence_OnQueryPresenceCompleteCallback delegate,
                                 const std::string& local, const std::string& target,
                                 EOS_EResult code) {
    if (delegate == 0) {
        return;
    }
    std::unique_ptr<frame_result> result(new frame_result());
    void* payload = result->create_callback(cb_query,
                                            sizeof(EOS_Presence_QueryPresenceCallbackInfo),
                                            reinterpret_cast<completion_delegate>(delegate));
    EOS_Presence_QueryPresenceCallbackInfo* info =
        static_cast<EOS_Presence_QueryPresenceCallbackInfo*>(payload);
    info->ResultCode = code;
    info->ClientData = client_data;
    info->LocalUserId = local.empty() ? 0 : id_registry::instance().get_epic_account_id(local);
    info->TargetUserId = target.empty() ? 0 : id_registry::instance().get_epic_account_id(target);
    result->set_done(true);
    callbacks_.add_callback(this, std::move(result));
}

EOS_Bool sdk_presence::has_presence(const EOS_Presence_HasPresenceOptions* options) const {
    if (options == 0 || !version_ok(options->ApiVersion, EOS_PRESENCE_HASPRESENCE_API_LATEST) ||
        options->LocalUserId == 0 || options->TargetUserId == 0) {
        return EOS_FALSE;
    }
    return (find_presence(options->TargetUserId->id_str) != 0) ? EOS_TRUE : EOS_FALSE;
}

EOS_EResult sdk_presence::copy_presence(const EOS_Presence_CopyPresenceOptions* options,
                                        EOS_Presence_Info** out) const {
    if (out == 0) {
        return EOS_EResult::EOS_InvalidParameters;
    }
    *out = 0;
    if (options == 0 || !version_ok(options->ApiVersion, EOS_PRESENCE_COPYPRESENCE_API_LATEST) ||
        options->LocalUserId == 0 || options->TargetUserId == 0) {
        return EOS_EResult::EOS_InvalidParameters;
    }
    const presence_info* found = find_presence(options->TargetUserId->id_str);
    if (found == 0) {
        return EOS_EResult::EOS_NotFound;
    }
    *out = build_presence_info(*found);
    return EOS_EResult::EOS_Success;
}

EOS_EResult sdk_presence::create_presence_modification(
    const EOS_Presence_CreatePresenceModificationOptions* options, EOS_HPresenceModification* out) {
    if (out == 0) {
        return EOS_EResult::EOS_InvalidParameters;
    }
    *out = 0;
    if (options == 0 ||
        !version_ok(options->ApiVersion, EOS_PRESENCE_CREATEPRESENCEMODIFICATION_API_LATEST) ||
        options->LocalUserId == 0) {
        return EOS_EResult::EOS_InvalidParameters;
    }
    if (options->LocalUserId->id_str != settings_.epic_account_id()) {
        return EOS_EResult::EOS_InvalidUser;
    }
    std::unique_ptr<modification_object> object(new modification_object());
    object->set_status = false;
    object->status = 0;
    object->set_rich_text = false;
    object->set_join_info = false;
    *out = reinterpret_cast<EOS_HPresenceModification>(modifications_.add(std::move(object)));
    return EOS_EResult::EOS_Success;
}

void sdk_presence::set_presence(const EOS_Presence_SetPresenceOptions* options, void* client_data,
                                EOS_Presence_SetPresenceCompleteCallback delegate) {
    if (delegate == 0) {
        return;
    }
    EOS_EResult code = EOS_EResult::EOS_Success;
    std::string local;
    if (options == 0 || !version_ok(options->ApiVersion, EOS_PRESENCE_SETPRESENCE_API_LATEST) ||
        options->LocalUserId == 0) {
        code = EOS_EResult::EOS_InvalidParameters;
    } else {
        local = options->LocalUserId->id_str;
        modification_object* staged = modifications_.find(options->PresenceModificationHandle);
        if (local != settings_.epic_account_id()) {
            code = EOS_EResult::EOS_InvalidUser;
        } else if (staged == 0) {
            code = EOS_EResult::EOS_InvalidParameters;
        } else {
            presence_info* self = find_presence(local);
            if (self == 0) {
                seed_myself();
                self = find_presence(local);
            }
            if (self != 0) {
                // Merge the staged changes into a copy first. The records have to stay within the
                // ABI cap as a whole, not just per SetData call -- a presence with more than
                // EOS_PRESENCE_DATA_MAX_KEYS records is one no peer can decode, so it would silently
                // vanish from the mesh. If the merge would overflow, nothing is changed.
                std::vector<presence_data_record> records = self->records;
                for (std::size_t i = 0; i < staged->data_deleted.size(); i++) {
                    for (std::size_t r = 0; r < records.size();) {
                        if (records[r].key == staged->data_deleted[i]) {
                            records.erase(records.begin() + r);
                        } else {
                            r++;
                        }
                    }
                }
                for (std::size_t i = 0; i < staged->data_set.size(); i++) {
                    bool replaced = false;
                    for (std::size_t r = 0; r < records.size(); r++) {
                        if (records[r].key == staged->data_set[i].key) {
                            records[r].value = staged->data_set[i].value;
                            replaced = true;
                            break;
                        }
                    }
                    if (!replaced) {
                        records.push_back(staged->data_set[i]);
                    }
                }

                if (records.size() > max_data_keys) {
                    code = EOS_EResult::EOS_LimitExceeded;
                } else {
                    if (staged->set_status) {
                        self->status = staged->status;
                    }
                    if (staged->set_rich_text) {
                        self->rich_text = staged->rich_text;
                    }
                    if (staged->set_join_info) {
                        self->join_info = staged->join_info;
                    }
                    self->records = records;
                    // Everyone we know should see the change; a fresh peer gets it on connect.
                    broadcast_my_presence(std::string());
                }
            }
        }
    }

    std::unique_ptr<frame_result> result(new frame_result());
    void* payload = result->create_callback(cb_set, sizeof(EOS_Presence_SetPresenceCallbackInfo),
                                            reinterpret_cast<completion_delegate>(delegate));
    EOS_Presence_SetPresenceCallbackInfo* info =
        static_cast<EOS_Presence_SetPresenceCallbackInfo*>(payload);
    info->ResultCode = code;
    info->ClientData = client_data;
    info->LocalUserId = local.empty() ? 0 : id_registry::instance().get_epic_account_id(local);
    info->RichPresenceResultCode = code;
    result->set_done(true);
    callbacks_.add_callback(this, std::move(result));
}

EOS_EResult sdk_presence::get_join_info(const EOS_Presence_GetJoinInfoOptions* options,
                                        char* out_buffer, i32* inout_buffer_length) const {
    if (out_buffer == 0 || inout_buffer_length == 0) {
        return EOS_EResult::EOS_InvalidParameters;
    }
    if (options == 0 || !version_ok(options->ApiVersion, EOS_PRESENCE_GETJOININFO_API_LATEST) ||
        options->LocalUserId == 0 || options->TargetUserId == 0) {
        return EOS_EResult::EOS_InvalidParameters;
    }
    const presence_info* found = find_presence(options->TargetUserId->id_str);
    if (found == 0 || found->join_info.empty()) {
        return EOS_EResult::EOS_NotFound;
    }
    // The header is explicit here: a buffer too small to hold the string (with its terminator) is
    // LimitExceeded, and the required length is written back. This is the opposite of P2P's
    // truncate-and-succeed, so we honour it exactly.
    const i32 needed = static_cast<i32>(found->join_info.size()) + 1;
    if (*inout_buffer_length < needed) {
        *inout_buffer_length = needed;
        return EOS_EResult::EOS_LimitExceeded;
    }
    std::memcpy(out_buffer, found->join_info.c_str(), found->join_info.size() + 1);
    *inout_buffer_length = needed;
    return EOS_EResult::EOS_Success;
}

EOS_NotificationId sdk_presence::add_notify_on_presence_changed(
    void* client_data, EOS_Presence_OnPresenceChangedCallback delegate) {
    if (delegate == 0) {
        return EOS_INVALID_NOTIFICATIONID;
    }
    std::unique_ptr<frame_result> result(new frame_result());
    void* payload = result->create_callback(cb_presence_changed,
                                            sizeof(EOS_Presence_PresenceChangedCallbackInfo),
                                            reinterpret_cast<completion_delegate>(delegate));
    void** client = static_cast<void**>(payload);
    *client = client_data;
    return callbacks_.add_notification(this, std::move(result));
}

void sdk_presence::remove_notify_on_presence_changed(EOS_NotificationId id) {
    callbacks_.remove_notification(this, id);
}

// The overlay is where a friend clicks "join game"; without it this notification has no trigger, so
// it is registered (a game must be able to) but never fires. Documented in docs/presence.md.
EOS_NotificationId sdk_presence::add_notify_join_game_accepted(
    void* client_data, EOS_Presence_OnJoinGameAcceptedCallback delegate) {
    if (delegate == 0) {
        return EOS_INVALID_NOTIFICATIONID;
    }
    std::unique_ptr<frame_result> result(new frame_result());
    void* payload = result->create_callback(cb_join_accepted,
                                            sizeof(EOS_Presence_JoinGameAcceptedCallbackInfo),
                                            reinterpret_cast<completion_delegate>(delegate));
    void** client = static_cast<void**>(payload);
    *client = client_data;
    return callbacks_.add_notification(this, std::move(result));
}

void sdk_presence::remove_notify_join_game_accepted(EOS_NotificationId id) {
    callbacks_.remove_notification(this, id);
}

// --- PresenceModification sub-handle ---

EOS_EResult sdk_presence::modification_set_status(
    void* handle, const EOS_PresenceModification_SetStatusOptions* options) {
    modification_object* object = modifications_.find(handle);
    if (object == 0 || options == 0 ||
        !version_ok(options->ApiVersion, EOS_PRESENCEMODIFICATION_SETSTATUS_API_LATEST)) {
        return EOS_EResult::EOS_InvalidParameters;
    }
    object->set_status = true;
    object->status = static_cast<i32>(options->Status);
    return EOS_EResult::EOS_Success;
}

EOS_EResult sdk_presence::modification_set_raw_rich_text(
    void* handle, const EOS_PresenceModification_SetRawRichTextOptions* options) {
    modification_object* object = modifications_.find(handle);
    if (object == 0 || options == 0 ||
        !version_ok(options->ApiVersion, EOS_PRESENCEMODIFICATION_SETRAWRICHTEXT_API_LATEST) ||
        options->RichText == 0) {
        return EOS_EResult::EOS_InvalidParameters;
    }
    const std::string text = options->RichText;
    if (text.size() > max_rich_text_length) {
        return EOS_EResult::EOS_LimitExceeded;
    }
    object->set_rich_text = true;
    object->rich_text = text;
    return EOS_EResult::EOS_Success;
}

EOS_EResult sdk_presence::modification_set_data(
    void* handle, const EOS_PresenceModification_SetDataOptions* options) {
    modification_object* object = modifications_.find(handle);
    if (object == 0 || options == 0 ||
        !version_ok(options->ApiVersion, EOS_PRESENCEMODIFICATION_SETDATA_API_LATEST) ||
        (options->RecordsCount > 0 && options->Records == 0)) {
        return EOS_EResult::EOS_InvalidParameters;
    }
    if (options->RecordsCount < 0 ||
        static_cast<std::size_t>(options->RecordsCount) > max_data_keys) {
        return EOS_EResult::EOS_LimitExceeded;
    }
    for (i32 i = 0; i < options->RecordsCount; i++) {
        const EOS_Presence_DataRecord& record = options->Records[i];
        if (record.Key == 0 || record.Value == 0) {
            return EOS_EResult::EOS_InvalidParameters;
        }
        if (std::strlen(record.Key) > max_key_length ||
            std::strlen(record.Value) > max_value_length) {
            return EOS_EResult::EOS_LimitExceeded;
        }
    }
    // Only touch the staged records once every input has passed, so a bad record late in the list
    // does not leave a half-applied change behind.
    for (i32 i = 0; i < options->RecordsCount; i++) {
        presence_data_record staged;
        staged.key = options->Records[i].Key;
        staged.value = options->Records[i].Value;
        object->data_set.push_back(staged);
    }
    return EOS_EResult::EOS_Success;
}

EOS_EResult sdk_presence::modification_delete_data(
    void* handle, const EOS_PresenceModification_DeleteDataOptions* options) {
    modification_object* object = modifications_.find(handle);
    if (object == 0 || options == 0 ||
        !version_ok(options->ApiVersion, EOS_PRESENCEMODIFICATION_DELETEDATA_API_LATEST) ||
        (options->RecordsCount > 0 && options->Records == 0)) {
        return EOS_EResult::EOS_InvalidParameters;
    }
    if (options->RecordsCount < 0 ||
        static_cast<std::size_t>(options->RecordsCount) > max_data_keys) {
        return EOS_EResult::EOS_LimitExceeded;
    }
    for (i32 i = 0; i < options->RecordsCount; i++) {
        if (options->Records[i].Key == 0) {
            return EOS_EResult::EOS_InvalidParameters;
        }
    }
    for (i32 i = 0; i < options->RecordsCount; i++) {
        object->data_deleted.push_back(options->Records[i].Key);
    }
    return EOS_EResult::EOS_Success;
}

EOS_EResult sdk_presence::modification_set_join_info(
    void* handle, const EOS_PresenceModification_SetJoinInfoOptions* options) {
    modification_object* object = modifications_.find(handle);
    if (object == 0 || options == 0 ||
        !version_ok(options->ApiVersion, EOS_PRESENCEMODIFICATION_SETJOININFO_API_LATEST)) {
        return EOS_EResult::EOS_InvalidParameters;
    }
    // A null JoinInfo clears it; that is how a game says "I am no longer joinable".
    const std::string info = text_or_empty(options->JoinInfo);
    if (info.size() > max_join_info_length) {
        return EOS_EResult::EOS_LimitExceeded;
    }
    object->set_join_info = true;
    object->join_info = info;
    return EOS_EResult::EOS_Success;
}

// The template setters drive Epic's server-rendered rich presence, which has no local equivalent
// here. We accept a well-formed call so a game that uses them still runs; there is nothing to store.
EOS_EResult sdk_presence::modification_set_template_id(
    void* handle, const EOS_PresenceModification_SetTemplateIdOptions* options) {
    modification_object* object = modifications_.find(handle);
    if (object == 0 || options == 0 ||
        !version_ok(options->ApiVersion, EOS_PRESENCEMODIFICATION_SETTEMPLATEID_API_LATEST) ||
        options->TemplateId == 0) {
        return EOS_EResult::EOS_InvalidParameters;
    }
    return EOS_EResult::EOS_Success;
}

EOS_EResult sdk_presence::modification_set_template_data(
    void* handle, const EOS_PresenceModification_SetTemplateDataOptions* options) {
    modification_object* object = modifications_.find(handle);
    if (object == 0 || options == 0 ||
        !version_ok(options->ApiVersion, EOS_PRESENCEMODIFICATION_SETTEMPLATEDATA_API_LATEST) ||
        options->Key == 0) {
        return EOS_EResult::EOS_InvalidParameters;
    }
    return EOS_EResult::EOS_Success;
}

void sdk_presence::modification_release(void* handle) {
    modifications_.release(handle);
}

// --- network ---

void sdk_presence::broadcast_my_presence(const std::string& to_peer) {
    const presence_info* self = find_presence(settings_.epic_account_id());
    if (self == 0) {
        return;
    }
    byte_writer writer;
    serialize(writer, *self);

    std::vector<std::string> peers;
    if (!to_peer.empty()) {
        peers.push_back(to_peer);
    } else {
        peers = network_.peer_ids();
    }
    for (std::size_t i = 0; i < peers.size(); i++) {
        net_envelope envelope;
        envelope.type_tag = static_cast<u16>(message_type::presence_info);
        envelope.source_id = settings_.product_user_id();
        envelope.dest_id = peers[i];
        envelope.game_id = settings_.product_id();
        envelope.payload = writer.data();
        network_.send(envelope);
    }
}

void sdk_presence::fire_presence_changed(const std::string& changed_epic_id) {
    EOS_EpicAccountId local = id_registry::instance().get_epic_account_id(settings_.epic_account_id());
    EOS_EpicAccountId changed = id_registry::instance().get_epic_account_id(changed_epic_id);

    // Re-look-up each notification by id before firing: a fired callback may remove another.
    std::vector<EOS_NotificationId> ids = callbacks_.notification_ids(this, cb_presence_changed);
    for (std::size_t n = 0; n < ids.size(); n++) {
        frame_result* note = callbacks_.find_notification(this, ids[n]);
        if (note == 0) {
            continue;
        }
        EOS_Presence_PresenceChangedCallbackInfo* info =
            note->get_callback<EOS_Presence_PresenceChangedCallbackInfo>();
        info->LocalUserId = local;
        info->PresenceUserId = changed;
        note->fire();
    }
}

bool sdk_presence::on_network_message(const net_envelope& message) {
    if (message.source_id.empty() || message.source_id == settings_.product_user_id()) {
        return true;
    }

    // A peer we just met has never heard our presence; tell it, so its first query resolves.
    if (message.type_tag == static_cast<u16>(message_type::peer_connected)) {
        if (message.game_id.empty() || message.game_id == settings_.product_id()) {
            broadcast_my_presence(message.source_id);
        }
        return true;
    }

    // A peer that leaves takes its presence and its account bindings with it, so the accounts it
    // spoke for are free for whoever holds them next, and a game no longer sees a friend who is
    // gone as present.
    if (message.type_tag == static_cast<u16>(message_type::peer_disconnected)) {
        std::map<std::string, std::string>::iterator owner = epic_owner_.begin();
        while (owner != epic_owner_.end()) {
            if (owner->second == message.source_id) {
                presences_.erase(owner->first);
                epic_owner_.erase(owner++);
            } else {
                ++owner;
            }
        }
        return true;
    }

    // A different game's presence is not ours to cache or answer.
    if (message.game_id != settings_.product_id()) {
        return true;
    }

    byte_reader reader(message.payload.data(), message.payload.size());

    if (message.type_tag == static_cast<u16>(message_type::presence_request)) {
        presence_request request;
        if (!deserialize(reader, request)) {
            return true;
        }
        // Only the owner of the account answers for it.
        if (request.target_epic_id == settings_.epic_account_id()) {
            broadcast_my_presence(message.source_id);
        }
        return true;
    }

    if (message.type_tag == static_cast<u16>(message_type::presence_info)) {
        presence_info incoming;
        if (!deserialize(reader, incoming)) {
            return true;
        }
        // We speak for our own account, so a peer claiming to be us is ignored.
        if (incoming.epic_id.empty() || incoming.epic_id == settings_.epic_account_id()) {
            return true;
        }
        // Reject a malformed message before it can leave any trace. Validating first is what keeps
        // a bad announcement from claiming an account (below) and locking out the real owner.
        if (!presence_within_caps(incoming)) {
            return true;
        }
        // A peer may speak only for its own account. Each Epic id is bound to the peer that
        // announced it, and anyone else claiming it is refused, so a peer cannot forge a friend's
        // presence or join string. Identity on the mesh is self-asserted, so this binds accounts to
        // sources among cooperating peers rather than defending against one that spoofs another's
        // id; a peer's binding is released when it disconnects (see peer_disconnected) so a departed
        // account can be re-announced by whoever holds it next.
        std::map<std::string, std::string>::iterator owner = epic_owner_.find(incoming.epic_id);
        if (owner == epic_owner_.end()) {
            epic_owner_[incoming.epic_id] = message.source_id;
        } else if (owner->second != message.source_id) {
            return true;
        }

        // Only a real change is worth telling the game about; identical re-broadcasts (a peer
        // answering a request, or re-announcing on connect) do not fire the notification.
        const presence_info* existing = find_presence(incoming.epic_id);
        const bool changed = (existing == 0) || !presence_equal(*existing, incoming);
        presences_[incoming.epic_id] = incoming;
        if (changed) {
            fire_presence_changed(incoming.epic_id);
        }

        // Settle any query waiting on this account, changed or not: the game asked to be sure.
        std::map<frame_result*, pending_query>::iterator it = pending_queries_.begin();
        for (; it != pending_queries_.end(); ++it) {
            if (it->second.target_id != incoming.epic_id) {
                continue;
            }
            EOS_Presence_QueryPresenceCallbackInfo* info =
                it->first->get_callback<EOS_Presence_QueryPresenceCallbackInfo>();
            info->ResultCode = EOS_EResult::EOS_Success;
            it->first->set_done(true);
        }
        return true;
    }

    return false;
}

// --- i_run_callback ---

bool sdk_presence::cb_run_frame() {
    return true;
}

bool sdk_presence::run_callbacks(frame_result& result) {
    std::map<frame_result*, pending_query>::iterator it = pending_queries_.find(&result);
    if (it == pending_queries_.end()) {
        return false;
    }
    if (result.done()) {
        return true; // an answer arrived and marked it done
    }
    if (std::chrono::steady_clock::now() > it->second.deadline) {
        // Nobody answered. The account may simply not be reachable; report it as not found.
        EOS_Presence_QueryPresenceCallbackInfo* info =
            result.get_callback<EOS_Presence_QueryPresenceCallbackInfo>();
        info->ResultCode = EOS_EResult::EOS_NotFound;
        return true;
    }
    return false;
}

void sdk_presence::free_callback(frame_result& result) {
    pending_queries_.erase(&result);
}

// --- the struct the game frees ---

void release_presence_info(EOS_Presence_Info* info) {
    if (info == 0) {
        return;
    }
    std::lock_guard<std::mutex> lock(g_info_mutex);
    g_presence_infos.erase(info);
}

} // namespace eosr
