# Modules: `EOSSDK_Achievements`, `EOSSDK_Stats`, `EOSSDK_Leaderboards`

Tier A. Local progression trio, backed by the per-user savepath. Stats feed Achievements; Leaderboards are largely local/stubbed. 22 + 6 + 14 methods.

## Common
All `IRunCallback` only (no network). Data persisted under `FileManager` root (savepath/userid/appid). Async queries load/synthesize local state then complete via FrameResult.

## `EOSSDK_Achievements` (22)
Handle `EOS_HAchievements`. `EmuInit` @`0x180063940` / `EmuDeinit` @`0x180068450`.
- **Definitions:** `QueryDefinitions` @`0x180064770`; `GetAchievementDefinitionCount`, `CopyAchievementDefinitionByIndex/ByAchievementId`, plus **V2** variants (`CopyAchievementDefinitionV2ByIndex/ByAchievementId`).
- **Player state:** `QueryPlayerAchievements` @`0x180064ec0`; `GetPlayerAchievementCount`, `CopyPlayerAchievementByIndex/ByAchievementId` (+ helper `_CopyPlayerAchievement`); unlocked: `GetUnlockedAchievementCount`, `CopyUnlockedAchievementByIndex/ByAchievementId` (+ `_CopyUnlockedAchievement`).
- **Unlock:** `UnlockAchievements` @`0x180065900`; `AddNotifyAchievementsUnlocked`(+`V2`)/`Remove`. Callback-info `EOS_Achievements_OnUnlockAchievementsCompleteCallbackInfo`, notification `EOS_Achievements_OnAchievementsUnlockedCallbackInfo(V2)`.
- **Stat trigger link:** `CheckAchievementsStatTriggers` @`0x180064010` — evaluates stat thresholds to auto-unlock achievements (called after stat ingest). The Stats↔Achievements bridge.

## `EOSSDK_Stats` (6)
Handle `EOS_HStats`. `EmuDeinit` @`0x18017e250`.
- `IngestStat` @`0x18017d240` (record/increment; persists; triggers `CheckAchievementsStatTriggers`), `QueryStats` @`0x18017d930`, `GetStatsCount`, `CopyStatByIndex`/`ByName`. Callback-infos `EOS_Stats_*CallbackInfo`.

## `EOSSDK_Leaderboards` (14)
Handle `EOS_HLeaderboards`. `EmuInit` @`0x1800d4600` / `EmuDeinit` @`0x1800d52c0`.
- Definitions/records/user-scores: `QueryLeaderboardDefinitions` @`0x1800d47e0`, `QueryLeaderboardRanks` @`0x1800d4b20`, `QueryLeaderboardUserScores` @`0x1800d4eb0`; the `Get*Count`/`Copy*` accessors are tiny (109–157 B) — **largely stubbed** (LAN has no global leaderboard; likely returns local/empty).

## Reimpl notes
- Reimpl: achievement definitions from config/game data; player unlock state + stats persisted per user; `IngestStat` → threshold check → unlock + notify. Leaderboards can be local-only/empty. Follow-up: exact persistence format (JSON under savepath); V2 definition differences; stat aggregation ops.
