# Network Protocol & Transport

> Wire protocol of the emulator's peer-to-peer LAN networking, reconstructed from the 2020 `proto/network_proto.proto` + `network.cpp`, to be reconciled against the newer binary's `network/` submodule. This is the interop contract two reimplemented instances must share.

## Design in one line
One protobuf envelope (`Network_Message_pb`) carried over **UDP-broadcast discovery + TCP mesh + UDP datagrams**, with a background RX thread and frame-loop dispatch keyed off the protobuf `oneof` discriminator. Every game-state interface is a listener on one `oneof` case.

## Transport
- **IPv4**, UDP + TCP. (IPv6 awareness exists in `is_lan_ip` but transport is IPv4-only in 2020 — a likely growth point.)
- **UDP discovery port**: first free in `[55789, 55799)`. `SO_BROADCAST`. Anchor constant **55789** (0xD9ED).
- **TCP mesh port**: random `30000..60000`, `listen(32)`; the chosen port is advertised over UDP.
- **Self-pipe**: a loopback TCP connection so a node delivers to itself through the same path (local peer ids map to the self-send socket).
- **RX model**: background thread (`network_thread`) polls (500 ms) UDP + TCP-listen + self-recv; does `do_advertise` + `process_udp`/`process_tcp_*`. It **parses + enqueues** into `_pending_network_msgs` under a mutex.
- **Dispatch model**: `Callback_Manager::run_frames()` → `Network::CBRunFrame(0)` (game thread) moves `_pending → _network_msgs` and invokes handlers. Two-stage queue across the thread boundary.
- **Optional zstd compression** behind `NETWORK_COMPRESS` (off by default; branch reserved on every send path). Binary links protobuf; check whether it also links zstd.

## Peer model
- **Peer identity = ProductUserId string** (`peer_t = std::string`). Envelope carries `source_id`/`dest_id` as these strings.
- Maps: `_my_peer_ids: set<peer_t>`, `_udp_addrs: map<peer_t, ipv4_addr>` (learned on inbound UDP), `_tcp_peers: map<peer_t, tcp_socket*>` (reliable route), plus waiting-connect/out queues and `_default_channels` (all channel 0 today).
- **Discovery**: `do_advertise()` every **2000 ms** broadcasts `Network_Advertise_pb{ port }` to every NIC broadcast addr across the whole port range. On receiving an advertise, an unknown peer triggers `connect_to_peer` → TCP connect + peer-list exchange handshake → full **TCP mesh** keyed by peer id. Peer connect/disconnect surface as synthetic `Network_Advertise_pb` messages queued to every channel.

## Framing
- **TCP**: 4-byte **big-endian** length prefix + serialized message (`next_packet_size_t = uint32_t`, net-swapped). Reassembled in `process_tcp_data`.
- **UDP**: one datagram = one serialized `Network_Message_pb`, no prefix, 4096-byte RX buffer.

## Envelope (`Network_Message_pb`)
```proto
message Network_Message_pb {
  string source_id = 1; string dest_id = 2; string game_id = 3; int64 timestamp = 4;
  oneof messages {
    Network_Advertise_pb       network_advertise = 5;
    // Friends_Message_pb      friends           = 6;   // commented out in 2020 — likely re-enabled in newer build
    Presence_Message_pb        presence          = 7;
    UserInfo_Message_pb        userinfo          = 8;
    Session_Message_pb         session           = 9;
    P2P_Message_pb             p2p               = 10;
    Connect_Message_pb         connect           = 11;
    Sessions_Search_Message_pb sessions_search   = 12;
    Lobby_Message_pb           lobby             = 13;
    Lobbies_Search_Message_pb  lobbies_search    = 14;
  }
}
```
C++ discriminator `MessagesCase` (`kNetworkAdvertise, kPresence, kUserinfo, kSession, kP2P, kConnect, kSessionsSearch, kLobby, kLobbiesSearch`) is the routing key.

## Sub-message catalog (2020 baseline)
- **Network_Advertise_pb**: port(`Network_Port_pb{uint32 port}`), peer(`Network_Peer_pb{repeated string peer_ids}`), accept, peer_connect, peer_disconnect (last three empty markers).
- **Presence_Message_pb**: presence_info_request, presence_info(`Presence_Info_pb{userid,status,productid,productversion,platform,richtext,map<string,string> records,productname}`).
- **UserInfo_Message_pb**: userinfo_info_request, userinfo_info(`{country,displayname,preferredlanguage,nickname}`).
- **Connect_Message_pb**: request, infos(`Connect_Infos_pb{userid, map<string,string> sessions, displayname}`).
- **Session_Message_pb**: sessions_request, session_infos, session_destroy, session_join_request, session_join_response, session_invite, session_invite_response, session_register, session_unregister. `Session_Infos_pb{session_id,bucket_id,max_players,players[],registered_players[],presence_allowed,host_address,permission_level,join_in_progress_allowed,invites_allowed,map<string,Session_Attribute> attributes,state}`.
- **Sessions_Search_Message_pb**: search(`{search_id,session_id,target_id,map<string,Session_Search_Parameter>,max_results}`), search_response(`{search_id, repeated Session_Infos_pb}`).
- **P2P_Message_pb**: connect_request(`{socket_name}`), connect_response(`{accepted}`), data_message(`{bytes data,int32 channel,socket_name,user_id}`), data_acknowledge(`{channel,accepted}`), connection_close.
- **Lobby_Message_pb**: lobby_update, lobby_join_request, lobby_join_response, lobby_invite, member_update, member_join, member_leave, member_promote. `Lobby_Infos_pb{lobby_id,max_lobby_member,permission_level,attributes,owner_id,map<string,Lobby_Member_Infos_pb> members}`.
- **Lobbies_Search_Message_pb**: search, search_response.

## Send primitives
`SendBroadcast` (UDP, dest must be empty), `UDPSendToAllPeers`/`UDPSendTo` (via `_udp_addrs`), `TCPSendToAllPeers`/`TCPSendTo` (via `_tcp_peers`). Every send stamps `timestamp` and asserts `source_id != ""`. **Convention: TCP for control/state, UDP for bulk/data.**

## Routing (two-level)
1. `Network::CBRunFrame` selects `_network_listeners[MessagesCase][channel]` and calls each `RunNetwork`/`OnNetworkMessage`.
2. Each interface switches on its own sub-`oneof` `message_case()` → `on_<event>(msg, submsg)`.

Handlers first **drop self-originated** messages (`source_id == my productuserid`). Then: upsert local cache, resolve pending async `res` (set `ResultCode`, `done=true`, erase), fire notifications, and/or reply inline.

| MessagesCase | Interface |
|---|---|
| kNetworkAdvertise, kConnect | Connect (roster backbone) |
| kPresence | Presence |
| kUserinfo | UserInfo |
| kSession, kSessionsSearch | Sessions |
| kLobby, kLobbiesSearch | Lobby |
| kP2P | P2P |

Search-response cases are handled by transient `EOSSDK_SessionSearch`/`EOSSDK_LobbySearch`.

## Higher-interface usage
- **Connect** = membership/roster backbone: advertises peer id at login, exchanges `Connect_Infos`, owns peer connect/disconnect fan-out to Presence/Lobby/Sessions/P2P, fires Friends updates.
- **Presence/Sessions/Lobby** ride the TCP mesh (unicast to members/peers). "Session/lobby discovery" is app-level query/response (`*_search` → host reply), not a broadcast; the UDP broadcast only discovers *peers*.
- **P2P**: control (connect/close) over TCP, data + acks over UDP; per-channel in-queues; state machine `requesting/connecting/connected/connection_loss/closed`.

## Newer-build deltas to verify in the binary
- Friends `oneof` case likely re-enabled (tag 6).
- `game_id`/appid filtering likely live (2020 left `//msg.set_appid` scaffolding dead).
- `timestamp` staleness drop may be live (stubbed in 2020).
- Possible zstd compression, encryption (platform holds unused `_encryption_key`), multi-channel, IPv6.
- RTC voice transport (new) — check whether it rides this envelope or a separate path.

---

# RECOVERED newer-build schema (from embedded protobuf descriptors)

> Reconstructed from the binary's embedded protobuf-lite type/field name strings (`Epic*_pb.*`). This is the **actual wire schema of this build (~EOS 1.17.1)** and supersedes the 2020 layout above where they differ. Resolves the auth/connect/userinfo/custominvites/emu-info follow-ups.

**Structural change:** the single 2020 `Network_Message_pb{oneof}` was refactored into **per-interface top-level `Epic<X>_pb` envelopes**, each carrying explicit `source_product_id` / `destination_product_id` (Lobby spells it `destination_productId`) plus a oneof of that interface's sub-messages. Routing is by which `Epic<X>_pb` arrived.

## Top-level envelopes (`Epic<X>_pb`) and their sub-messages

| Envelope | New? | Sub-messages / key fields |
|---|---|---|
| `EpicEmu_pb` | **NEW** | `EpicEmu_Infos_Request_pb`, `EpicEmu_Infos_Response_pb`, `EpicEmu_Infos_pb{emulator, appid, country, language, username}` |
| `EpicAuth_pb` | **NEW** | `AuthRequest_pb`, `AuthResponse_pb`, `AuthInfos_pb{id, name}` |
| `EpicConnect_pb{source, destination}` | renamed | `ConnectRequest_pb`, `ConnectResponse_pb`, `ConnectInfos_pb{id}`, `ConnectMappingInfos_pb{id, name}` |
| `EpicPresence_pb` | + field | `Presence_Info_Request_pb`, `Presence_Info_pb{productid, productversion, platform, richtext, records, productname, integratedplatform}` |
| `EpicP2P_pb{source_product_id, destination_product_id}` | same | `P2P_Connect_Request_pb{socket_name}`, `P2P_Connect_Response_pb{socket_name}`, `P2P_Data_Message_pb{socket_name}`, `P2P_Data_Acknowledge_pb{socket_name}`, `P2P_Connection_Close_pb{socket_name}` |
| `EpicSessions_pb{source_product_id, destination_product_id}` | + fields | `Session_Infos_Request_pb{session_id}`, `Session_Infos_pb{session_id, bucket_id, host_address, owner_user_id, owner_server_id}`, `Session_Destroy_pb`, `Session_Join_Request_pb`, `Session_Join_Response_pb{session_id, user_id}`, `Session_Invite_pb`, `Session_Register_pb{session_id, member_ids}`, `Session_Unregister_pb`; `Session_Attribute_pb{key}`, `Session_Attr_Value_pb{s,...}`, `Session_Player_pb{product_user_id}` |
| `EpicSessionsSearch_pb` | + field | `Sessions_Search_pb{app_id, session_id, target_id}`, `Sessions_Search_response_pb` |
| `EpicLobby_pb{source_product_id, destination_productId}` | + field | `Lobby_Update_pb`, `Lobby_Member_Update_pb`, `Lobby_Join_Request_pb`, `Lobby_Join_Response_pb`, `Lobby_Invite_pb`, `Lobby_Destroy_pb`, `Lobby_Member_Join/Leave/Promote_pb`; `Lobby_Infos_pb{lobby_id, owner_id, bucket_id, rtc_room_name}`, `Lobby_Member_Infos_pb{member_id}` |
| `EpicLobbySearch_pb{source, destination}` | renamed | `Lobbies_Search_pb`, `Lobbies_Search_response_pb` |
| `EpicCustomInvites_pb` | **NEW** | `CustomInvites_Invite_pb{id, payload}` |
| `EpicOverlay_pb` | **NEW** | overlay peer message (fields TBD; ties to the overlay module) |

**Notably absent: there is NO `EpicUserInfo_pb` and NO `EpicFriends_pb`.** This confirms:
- **UserInfo has no network path in this build** — a peer's display name/username arrives via `EpicEmu_Infos_pb.username` (the emu-info handshake) and `EpicPresence_pb`. (Resolves the `userinfo.md` open question.)
- **Friends has no network path** — derived entirely from the Connect roster via `OnUserEvent` (see `friends.md`).

## Emu-info handshake (`EpicEmu_pb`) — resolves `client.md` follow-up
On peer connect, `EOSSDK_Client` exchanges `EpicEmu_Infos_Request_pb` → `EpicEmu_Infos_Response_pb` carrying `EpicEmu_Infos_pb{emulator (version), appid, country, language, username}`. This is how peers learn each other's **appid** (to filter to same-game peers), locale, and username. `_SendEmuInfosRequest`/`_OnEmuInfosRequest`/`_SendEmuInfosResponse`/`_OnEmuInfosResponse` in `EOSSDK_Client`.

## New-field deltas vs 2020
- **Presence:** `+integratedplatform` (which platform the peer plays on).
- **Lobby:** `+rtc_room_name` (voice-room binding — matches `Lobby::JoinRTCRoom`).
- **Sessions:** `+owner_server_id` (dedicated-server sessions), search `+app_id` (per-game filtering).
- **Auth:** entirely new `EpicAuth_pb` network exchange (2020 had none) — peers advertise `AuthInfos_pb{id, name}` (Epic account id + display name).
- Per-interface `source/destination_product_id` now on each `Epic<X>_pb` (was on the single 2020 envelope).
