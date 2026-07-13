#include "interfaces/connect.h"

#include <memory>

#include "common/ids.h"
#include "common/log.h"
#include "common/types.h"
#include "core/callback_manager.h"
#include "core/frame_result.h"
#include "core/settings.h"
#include "net/messages.h"
#include "net/message_router.h"
#include "net/wire.h"

namespace eosr {

namespace {

// Distinct payload tags for the Connect callback-info types, unique within this interface.
const callback_type_id cb_login = 1;
const callback_type_id cb_logout = 2;
const callback_type_id cb_query_mappings = 3;
const callback_type_id cb_login_status_changed = 4;
const callback_type_id cb_auth_expiration = 5;
const callback_type_id cb_stub = 6;

// Every EOS async completion info begins with this common initial sequence, so we can fill the
// two universal fields of any of them through this view.
struct common_completion_prefix {
    EOS_EResult result_code;
    void* client_data;
};

// Validate the login request before we mutate any state. We do not authenticate the token, but
// we do reject malformed input: unsupported option/credential versions, a missing token, or a
// credential type outside the known range.
bool login_options_are_valid(const EOS_Connect_LoginOptions* options) {
    if (options == 0 || options->Credentials == 0) {
        return false;
    }
    if (options->ApiVersion <= 0 || options->ApiVersion > EOS_CONNECT_LOGIN_API_LATEST) {
        return false;
    }
    const EOS_Connect_Credentials* credentials = options->Credentials;
    if (credentials->ApiVersion <= 0 || credentials->ApiVersion > EOS_CONNECT_CREDENTIALS_API_LATEST) {
        return false;
    }
    if (credentials->Token == 0 || credentials->Token[0] == '\0') {
        return false;
    }
    const i32 type = static_cast<i32>(credentials->Type);
    const i32 lowest = static_cast<i32>(EOS_EExternalCredentialType::EOS_ECT_EPIC);
    const i32 highest = static_cast<i32>(EOS_EExternalCredentialType::EOS_ECT_VIVEPORT_USER_TOKEN);
    if (type < lowest || type > highest) {
        return false;
    }
    return true;
}

} // namespace

sdk_connect::sdk_connect(sdk_settings& settings, callback_manager& callbacks, message_router& network)
    : settings_(settings), callbacks_(callbacks), network_(network), registered_(false) {
}

sdk_connect::~sdk_connect() {
    emu_deinit();
}

void sdk_connect::emu_init() {
    if (registered_) {
        return;
    }
    callbacks_.register_frame(this);
    callbacks_.register_callbacks(this);
    network_.register_listener(message_type::connect_request, this);
    network_.register_listener(message_type::connect_response, this);
    // The mesh tells us when a peer joins or leaves; that is what the roster is built from.
    network_.register_listener(message_type::peer_connected, this);
    network_.register_listener(message_type::peer_disconnected, this);
    registered_ = true;
}

void sdk_connect::emu_deinit() {
    if (!registered_) {
        return;
    }
    network_.unregister_listener(message_type::connect_request, this);
    network_.unregister_listener(message_type::connect_response, this);
    network_.unregister_listener(message_type::peer_connected, this);
    network_.unregister_listener(message_type::peer_disconnected, this);
    callbacks_.unregister_callbacks(this);
    callbacks_.unregister_frame(this);
    local_users_.clear();
    peers_.clear();
    pending_status_changes_.clear();
    registered_ = false;
}

EOS_ProductUserId sdk_connect::local_user() const {
    return local_users_.empty() ? 0 : local_users_[0];
}

void sdk_connect::deliver_login_result(EOS_EResult result_code, EOS_ProductUserId user,
                                       void* client_data, EOS_Connect_OnLoginCallback delegate) {
    std::unique_ptr<frame_result> result(new frame_result());
    EOS_Connect_LoginCallbackInfo* info = static_cast<EOS_Connect_LoginCallbackInfo*>(
        result->create_callback(cb_login, sizeof(EOS_Connect_LoginCallbackInfo),
                                reinterpret_cast<completion_delegate>(delegate)));
    info->ResultCode = result_code;
    info->ClientData = client_data;
    info->LocalUserId = user;
    info->ContinuanceToken = 0;
    result->set_done(true);
    callbacks_.add_callback(this, std::move(result));
}

void sdk_connect::login(const EOS_Connect_LoginOptions* options, void* client_data,
                        EOS_Connect_OnLoginCallback delegate) {
    if (delegate == 0) {
        return;
    }
    if (!login_options_are_valid(options)) {
        deliver_login_result(EOS_EResult::EOS_InvalidParameters, 0, client_data, delegate);
        return;
    }

    // The emulator does not authenticate the credential token: any supported type maps to the
    // one stable local ProductUserId derived from the configured user.
    EOS_ProductUserId self =
        id_registry::instance().get_product_user_id(settings_.product_user_id());
    const bool was_logged_in = is_logged_in();
    if (!was_logged_in) {
        local_users_.push_back(self);
    }

    deliver_login_result(EOS_EResult::EOS_Success, self, client_data, delegate);

    if (!was_logged_in) {
        status_transition change;
        change.user = self;
        change.previous = EOS_ELoginStatus::EOS_LS_NotLoggedIn;
        change.current = EOS_ELoginStatus::EOS_LS_LoggedIn;
        pending_status_changes_.push_back(change);
        log_info("connect: logged in " + settings_.product_user_id());
    }
}

void sdk_connect::logout(const EOS_Connect_LogoutOptions* options, void* client_data,
                         EOS_Connect_OnLogoutCallback delegate) {
    if (delegate == 0) {
        return;
    }
    EOS_ProductUserId user = (options != 0) ? options->LocalUserId : 0;
    const bool was_logged_in = is_logged_in();
    const bool matches_self = was_logged_in && user == local_user();

    std::unique_ptr<frame_result> result(new frame_result());
    EOS_Connect_LogoutCallbackInfo* info = static_cast<EOS_Connect_LogoutCallbackInfo*>(
        result->create_callback(cb_logout, sizeof(EOS_Connect_LogoutCallbackInfo),
                                reinterpret_cast<completion_delegate>(delegate)));
    info->ResultCode = matches_self ? EOS_EResult::EOS_Success : EOS_EResult::EOS_InvalidUser;
    info->ClientData = client_data;
    info->LocalUserId = user;
    result->set_done(true);
    callbacks_.add_callback(this, std::move(result));

    if (matches_self) {
        local_users_.clear();
        status_transition change;
        change.user = user;
        change.previous = EOS_ELoginStatus::EOS_LS_LoggedIn;
        change.current = EOS_ELoginStatus::EOS_LS_NotLoggedIn;
        pending_status_changes_.push_back(change);
        log_info("connect: logged out");
    }
}

i32 sdk_connect::logged_in_users_count() const {
    return static_cast<i32>(local_users_.size());
}

EOS_ProductUserId sdk_connect::logged_in_user_by_index(i32 index) const {
    if (index < 0 || index >= static_cast<i32>(local_users_.size())) {
        return 0;
    }
    return local_users_[static_cast<std::size_t>(index)];
}

EOS_ELoginStatus sdk_connect::login_status(EOS_ProductUserId local_user_id) const {
    if (is_logged_in() && local_user_id == local_user()) {
        return EOS_ELoginStatus::EOS_LS_LoggedIn;
    }
    return EOS_ELoginStatus::EOS_LS_NotLoggedIn;
}

void sdk_connect::query_product_user_id_mappings(
    const EOS_Connect_QueryProductUserIdMappingsOptions* options, void* client_data,
    EOS_Connect_OnQueryProductUserIdMappingsCallback delegate) {
    if (delegate == 0) {
        return;
    }
    EOS_ProductUserId local = (options != 0) ? options->LocalUserId : 0;

    // For the local case the queried peers are already in the roster, so the query completes
    // immediately; the copy surface reads whatever the roster holds.
    std::unique_ptr<frame_result> result(new frame_result());
    EOS_Connect_QueryProductUserIdMappingsCallbackInfo* info =
        static_cast<EOS_Connect_QueryProductUserIdMappingsCallbackInfo*>(
            result->create_callback(cb_query_mappings,
                                    sizeof(EOS_Connect_QueryProductUserIdMappingsCallbackInfo),
                                    reinterpret_cast<completion_delegate>(delegate)));
    info->ResultCode = (options == 0) ? EOS_EResult::EOS_InvalidParameters : EOS_EResult::EOS_Success;
    info->ClientData = client_data;
    info->LocalUserId = local;
    result->set_done(true);
    callbacks_.add_callback(this, std::move(result));
}

EOS_EResult sdk_connect::get_product_user_id_mapping(
    const EOS_Connect_GetProductUserIdMappingOptions* options, char* out_buffer,
    i32* in_out_buffer_length) const {
    if (options == 0 || options->TargetProductUserId == 0 || out_buffer == 0 ||
        in_out_buffer_length == 0) {
        return EOS_EResult::EOS_InvalidParameters;
    }
    // This returns a peer's external account id for the requested account type. We do not yet
    // learn peers' external accounts (that arrives with the identity handshake), so no mapping
    // is cached and the correct answer is NotFound rather than a stand-in like the display name.
    return EOS_EResult::EOS_NotFound;
}

std::size_t sdk_connect::known_peer_count() const {
    return peers_.size();
}

bool sdk_connect::is_known_peer(const std::string& product_user_id) const {
    return peers_.find(product_user_id) != peers_.end();
}

EOS_NotificationId sdk_connect::add_notify_login_status_changed(
    void* client_data, EOS_Connect_OnLoginStatusChangedCallback delegate) {
    if (delegate == 0) {
        return 0;
    }
    std::unique_ptr<frame_result> result(new frame_result());
    EOS_Connect_LoginStatusChangedCallbackInfo* info =
        static_cast<EOS_Connect_LoginStatusChangedCallbackInfo*>(
            result->create_callback(cb_login_status_changed,
                                    sizeof(EOS_Connect_LoginStatusChangedCallbackInfo),
                                    reinterpret_cast<completion_delegate>(delegate)));
    info->ClientData = client_data;
    info->LocalUserId = 0;
    info->PreviousStatus = EOS_ELoginStatus::EOS_LS_NotLoggedIn;
    info->CurrentStatus = EOS_ELoginStatus::EOS_LS_NotLoggedIn;
    return callbacks_.add_notification(this, std::move(result));
}

void sdk_connect::remove_notify_login_status_changed(EOS_NotificationId id) {
    callbacks_.remove_notification(this, id);
}

EOS_NotificationId sdk_connect::add_notify_auth_expiration(
    void* client_data, EOS_Connect_OnAuthExpirationCallback delegate) {
    if (delegate == 0) {
        return 0;
    }
    std::unique_ptr<frame_result> result(new frame_result());
    EOS_Connect_AuthExpirationCallbackInfo* info =
        static_cast<EOS_Connect_AuthExpirationCallbackInfo*>(
            result->create_callback(cb_auth_expiration,
                                    sizeof(EOS_Connect_AuthExpirationCallbackInfo),
                                    reinterpret_cast<completion_delegate>(delegate)));
    info->ClientData = client_data;
    info->LocalUserId = 0;
    return callbacks_.add_notification(this, std::move(result));
}

void sdk_connect::remove_notify_auth_expiration(EOS_NotificationId id) {
    callbacks_.remove_notification(this, id);
}

void sdk_connect::queue_stub_result(void* client_data, completion_delegate delegate,
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

bool sdk_connect::cb_run_frame() {
    if (pending_status_changes_.empty()) {
        return false;
    }
    std::vector<status_transition> changes;
    changes.swap(pending_status_changes_);
    for (std::size_t i = 0; i < changes.size(); i++) {
        // Re-look-up each notification by id right before firing it: a callback fired here may
        // remove another notification, so a snapshot of raw pointers would dangle.
        std::vector<EOS_NotificationId> ids =
            callbacks_.notification_ids(this, cb_login_status_changed);
        for (std::size_t n = 0; n < ids.size(); n++) {
            frame_result* note = callbacks_.find_notification(this, ids[n]);
            if (note == 0) {
                continue;
            }
            EOS_Connect_LoginStatusChangedCallbackInfo* info =
                note->get_callback<EOS_Connect_LoginStatusChangedCallbackInfo>();
            info->LocalUserId = changes[i].user;
            info->PreviousStatus = changes[i].previous;
            info->CurrentStatus = changes[i].current;
            note->fire();
        }
    }
    return false;
}

bool sdk_connect::run_callbacks(frame_result&) {
    // Every Connect result completes immediately (set_done(true)), so there is no extra
    // readiness to report here.
    return false;
}

void sdk_connect::free_callback(frame_result&) {
    // Connect payloads hold only ids owned by the registry and plain values, so nothing the
    // result stashed needs releasing.
}

bool sdk_connect::on_network_message(const net_envelope& message) {
    // The transport loops our own messages back through the self-pipe, so drop anything we
    // originated before it can pollute the peer roster.
    if (message.source_id == settings_.product_user_id()) {
        return true;
    }

    // A peer joining the mesh is the roster's cue to introduce itself: we tell it who we are, and
    // its reply tells us who it is. A peer leaving is dropped from the roster outright.
    if (message.type_tag == static_cast<u16>(message_type::peer_connected)) {
        if (is_logged_in()) {
            announce_to(message.source_id);
        }
        return true;
    }
    if (message.type_tag == static_cast<u16>(message_type::peer_disconnected)) {
        peers_.erase(message.source_id);
        return true;
    }

    // Both a peer's request (announcing itself, wanting ours) and its response carry the peer's
    // Connect infos, so we record the peer from either. Replying to a request and advertising
    // ourselves need a real send-to-peer path over the TCP mesh, which lands with the networked
    // discovery milestone; until then the roster is populated by whatever arrives.
    const bool is_connect =
        message.type_tag == static_cast<u16>(message_type::connect_request) ||
        message.type_tag == static_cast<u16>(message_type::connect_response);
    if (!is_connect) {
        return false;
    }

    connect_infos infos;
    byte_reader reader(message.payload.data(), message.payload.size());
    if (deserialize(reader, infos) && !infos.product_user_id.empty()) {
        peers_[infos.product_user_id] = infos.display_name;
    }
    // A peer asking who we are gets an answer; a peer answering us does not need another.
    if (message.type_tag == static_cast<u16>(message_type::connect_request) && is_logged_in()) {
        announce_to(message.source_id);
    }
    return true;
}

// Tell one peer who we are, so its roster can name us.
void sdk_connect::announce_to(const std::string& peer_id) {
    connect_infos self;
    self.product_user_id = settings_.product_user_id();
    self.display_name = settings_.username();
    byte_writer writer;
    serialize(writer, self);

    net_envelope envelope;
    envelope.type_tag = static_cast<u16>(message_type::connect_response);
    envelope.source_id = settings_.product_user_id();
    envelope.dest_id = peer_id;
    envelope.game_id = settings_.product_id();
    envelope.payload = writer.data();
    network_.send(envelope);
}

} // namespace eosr
