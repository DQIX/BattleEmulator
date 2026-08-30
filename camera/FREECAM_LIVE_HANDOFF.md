# Free-camera live handoff
Updated: 2026-08-30 19:15 JST

## Working rule
- Confirmed ROM behavior is reflected into `BattleEmulator/camera` immediately; do not wait for the whole investigation to finish.
- Do not encode encounter-, seed-, monster-, actor-slot-, or action-order-specific fixes.
- ROM fixed data must be mined through existing scripts under `C:\Users\owner\Documents\tunnelworkspace\BattleArrow`; extend/reuse them when additional data is needed.
- `freecam_action_mapper.hpp` is freecam-only. Non-freecam/presentation-only actions must never be added there. `Bind<>` now has a compile-time guard derived from mined BACT + all actor-membership + fallback-membership data and rejects statically triggerless DQ9 actions.
- Existing DQ9 action IDs/target/operation data come from `camera/dq9-action-target-classification.csv`; `build_freecam_fast_generated.mjs` now bakes the required fixed columns into constexpr arrays. Do not rediscover those IDs with seed sweeps.
- The freecam-only mapper is validated by a consteval full-table walk after `kFreeCameraActions` is built. Do not add presentation-only actions to it. The old per-action assertions that DQ9 503/929 were mined triggerless were removed because that premise came from an unreliable intermediate trigger-source heuristic; 503/929 remain non-freecam actions and belong only to general action metadata.
- DeSmuME harness is the execution oracle. Ghidra is a reference for stable/static code; live overlay disassembly is authoritative for swapped overlay addresses.

## Confirmed and already reflected
- DQ9 actor IDs are arithmetic (`Ally i -> i`, `Enemy i -> 0xC0+i`).
- `WHIPPING_BOY` maps to DQ9 action `929` (`ゲルニックかばう`) in general action metadata and is intentionally absent from `freecam_action_mapper.hpp`. DQ9 action `109` is `HELM_SPLITTER` (`かぶと割り`).
- Bindings for Zaki and the three thrust skills are present in `freecam_action_mapper.hpp`.
- Fast runtime exposes generic equivalents of ROM target resolution and movement eligibility:
  - `ResolveActorPresentationTarget(...)`
  - `IsActorPresentationMovementEligible(...)`
- Commit `abe4e4b3` (2026-08-30 18:19 JST) contains those two APIs.
- Production runtime now owns an explicit `rosterField4Nonzero` compatibility state plus validity flag. This deliberately stores only the zero/nonzero information consumed by `021E08BC`; it does not attach invented semantics to the stale stack words.
- Existing `build_freecam_action_metadata.mjs` was extended instead of adding a one-off miner. FCMA v3 now carries the ROM-fixed attack-record `presentationType` alongside fallback lookup ID and attack formation mode; action bindings expose it at compile time.
- Measured correlation used for the compatibility model is by ROM `presentationType`, not by BattleEmulator/common action ID: type 1 is the normal attack/spell path observed as first-four mask `1110`; type 17 (Zaki) and type 4 observed as `1100`; type 0 action 503 observed as `0000`.
- Seed 8 full 12-slot probe now confirms the type-1 effect after both Bagima and Merami: nonzero slots `{0,1,2,5,6,7,8,9}`, zero slots `{3,4,10,11}`. This replaces the earlier 4-bit-only observation and is the shape to model for up-to-12-actor runtime compatibility.
- Seed 8 full 12-slot probe confirms the type-17 (Zaki) post-action shape consumed by action index 3: nonzero `{0,1,4,5,6,7,8,9}`, zero `{2,3,10,11}`.
- `row+4` is not frozen at setup entry. In seed 8 action-index-3 setup, C2's slot is zero in `build-roster-entry`, but earlier current-C0 goal processing changes that same physical slot to nonzero (`1`) before C2 reaches the `021E08BC` decision; C2 therefore takes fallback. Compatibility must be mutable during setup and updated by ROM-equivalent helper stack effects, not merely once after each action.
- Live `021E2904` identifies the exact setup-time write: when a conflict node is occupied by a roster actor other than the current/target actors, `021E29B0` executes `STR 1,[row,#4]` and clears that actor's auxiliary node. `InvalidatePresentationConflicts` now mirrors this by setting the matching compatibility slot nonzero; no actor-slot special case is used.

## Confirmed ROM behavior still being wired into production
- `overlay_d_25:021E08BC` scans presentation actors, not only the current action actor.
- Per actor it first applies `021E2850` movement eligibility against the current action target.
- For eligible actors:
  - current action actor uses current action target;
  - a future actor with roster `row+4 != 0` uses fallback presentation goal (`021E2664` path);
  - a future actor with roster `row+4 == 0` resolves that actor's future action target via `021E2818`, then assigns a presentation goal.
- Current `camera.cpp` historically assigned only the current actor, so this structural gap must be fixed generically.

## Production wiring now present
- `camera.cpp` now performs the confirmed `021E08BC` current/future suffix scan for mapped actions: suffix actors are de-duplicated before eligibility, current actor uses current target, future nonzero `row+4` uses fallback, future zero resolves that future action's primary target through `ResolveActorPresentationTarget`.
- Live `021E0D34..021E0D5C` confirmed that future actor goal assignment still uses the **current action record**; production therefore shares the current action's ROM `attackFormationMode`/current-action actor list rather than using the future action's formation mode.
- Known post-action initial-residue producers are currently implemented only for presentation types 1 and 17. Unknown types invalidate compatibility instead of being guessed. This is sufficient to exercise the seed-8 Bagima/Merami/Zaki sequence while additional type paths are mined.
- `main.cpp --scan-action-seeds` is the generic candidate finder for the unresolved producer paths. It sweeps a seed range, records representative seeds for every enemy common action that actually occurs, and prints confirmed ROM metadata for mapped actions. With a presentation-type filter it also enumerates every DQ9 action ID in the ROM carrying that type before running the seed sweep. Unmapped common actions remain explicitly `dq9=unmapped`; they are candidates for DeSmuME measurement rather than guessed mappings.

## Build/validator state
- 2026-08-30 18:44 JST: CLion CMake target `freecam_edge_validation` builds and exits 0 after the FCMA v3/runtime/setup-work-state changes: `PASS freecam-edge-validation` (`goalCases=23408`, `moving=19536`, `zakiSuppressed=3872`, `zakiTriggered=19536`).
- CLion's single-file/clangd inspection does not inherit the CMake `CMAKE_CXX_STANDARD 20` flags and falsely reports existing `std::span`/`std::bit_cast` symbols unresolved; this is not used as the build oracle. CMake target execution is the build oracle.
- The first `gerunikku`/`erugi1_gilyumei` build attempt exposed a production-wiring omission: `camera.cpp` called `ApplyKnownRosterField4PostActionCompatibility` but the helper had not actually landed in `freecam_fast_runtime.hpp`. The helper is now present; rebuild immediately follows this update.
- 2026-08-30 18:49 JST: CLion `gerunikku` Run configuration reached process launch (`fullOutputPath` issued), confirming the production `camera.cpp` translation unit now builds after the helper fix.
- `freecam_edge_validation` now contains explicit regression coverage for the observed 12-slot type-1/type-17 initial masks, unknown-type invalidation, and the exact `021E2904` conflict side effect (`row+4` compatibility slot becomes nonzero while the actor auxiliary node is invalidated).
- 2026-08-30 19:14 JST: the expanded `ValidateRosterField4Compatibility` regression was actually rebuilt/run in CLion and exits 0: `PASS freecam-edge-validation`.

## C++ brute-force discovery mode
- `main.cpp` now provides `--scan-action-seeds`; it reuses `BattleEmulator::Main`, the existing enemy AI, and generated ROM metadata. No duplicate action table was added.
- Metadata accessors (`HasBact`, `SelectorProjection`, `FallbackLookupActionId`, `AttackFormationMode`, `PresentationType`) are now `constexpr` rather than `consteval`, preserving compile-time use while allowing the discovery mode to enumerate all 1024 ROM actions at runtime.
- First real sweep: seeds `1..20000`, one turn, hero Zaki to C0, current seed position 1. Representative unmapped candidates found automatically:
  - Bagima(strong) common187: seed 0x1
  - Eerie Light common185: seed 0x2
  - Magic Mirror common31: seed 0x2
  - Helm Splitter common181: seed 0x2
  - Medapani common186: seed 0x5
  - Kabuff common173 and common21 inactive/skip path: seed 0x6
  - Double-edged Slash common182: seed 0x15
- The same sweep also re-confirmed mapped actions from production metadata: Attack common1→DQ9 1/type1, Whipping Boy common180→DQ9109/type1, Merami common183→DQ910/type1, Bagima common184→DQ919/type1.
- These brute-force results are candidate seeds only. Unknown common→DQ9 mapping and compatibility shapes remain forbidden from production until DeSmuME measurement or ROM mining confirms them.
- First candidate promoted: seed 0x1 common187 Bagima(strong) was measured in the ROM as DQ9 action 463. Its next-setup 12-slot residue is exactly the known type-1 mask, and ROM `actdata` mining independently returns type1 / formation2 / fallback463. `GERUNIKKU_BAGIMA_STRONG` is now bound to DQ9 463 in `freecam_action_mapper.hpp`.
- Seed 0x2 live ROM records confirmed the first two C++ enemy candidates before the model diverges later in the turn: common185 Eerie Light -> DQ9 155 (ROM type22/formation1), common31 Magic Mirror -> DQ9 55 (ROM type31/formation2). Both bindings are now production. Their residue producer types remain unknown and are deliberately not added to `ApplyKnownRosterField4PostActionCompatibility` yet.
- common21 `INACTIVE_ENEMY`: record-0 seed 0x1b2 -> fresh live ROM DQ9 503; ROM mining type0/formation0/fallback503. This mapping belongs to general action metadata only; 503 is non-freecam. Type0 full residue is still unknown.
- common186 `GERUNIKKU_MEDAPANI`: record-0 seed 0x55 -> fresh live ROM DQ9 912; ROM mining type21/formation2/fallback912. This mapping belongs to general action metadata only; do not register it in the freecam mapper. Type21 residue remains unknown.
- 200k sweep gave common21 `INACTIVE_ENEMY` a record-0 candidate at seed 0x1b2. Fresh ROM measurement confirmed the first enemy action is DQ9 503; ROM mining confirms type0/formation0/fallback503. The general action metadata maps `INACTIVE_ENEMY -> 503`; the freecam mapper does not. Full type0 12-slot residue is still intentionally unimplemented.
- The brute-force mode now records `bestRecord/bestSeed` per common action. Use the smallest record-index candidate for ROM verification, because later records can diverge after an earlier RNG/model mismatch even when the candidate action itself exists elsewhere.

## Compiler-stack compatibility fact
- Roster `row+4` is not metadata. `021E1958` writes only row `+0/+1/+8`; `+4` is stale stack content.
- The four physical `row+4` slots overlap stack locals used by movement/presentation code. `FUN_02049D84` itself overwrites them as part of its 12-byte-stride point array, and later caller/callee stack reuse overwrites them again before the next setup.
- Therefore no semantic shortcut such as `C2`, `last actor`, `future actor`, or `last turn action` may replace this state.
- Seed 8 observed next-Zaki mask after normal action: nonzero/nonzero/nonzero/zero.
- Seed 9 demonstrated a different path: action 503 leaves zero/zero/zero/zero, disproving slot/order heuristics.
- Only zero/nonzero is consumed by `021E08BC`; raw 32-bit residue values are incidental.

## Latest caller evidence
- `021DC70C` returns through saved PC `0x021DBAB4`; live action-time word at `0x021DBAB0` is `BL 021DC70C`.
- Live caller continuation contains action/post-presentation calls through the `021DBAxx..021DBDxx` range; this caller/callee stack reuse produces the compatibility mask consumed by the next setup.

## Immediate implementation task
1. Add a C++ brute-force discovery mode that enumerates seed/action candidates using only already-mined ROM metadata; use it as a candidate finder for unknown compatibility producer paths.
2. For every unknown presentation type/path, execute representative candidates in DeSmuME and record the full 12-slot row+4 shape. Only ROM mining or emulator measurements may become production data.
3. If a presentation type is not internally uniform, mine the additional ROM-fixed branch input instead of adding action-ID/seed special cases.
4. Port the confirmed `021E0D84..021E0F30` previous-action loop and keep validating seed8 after each production change.
5. `lv99.dst` is available only as an observation fixture if selector coverage requires skills not present in the current state. Do not implement its entire skill inventory; battle-MCP multi-player/equipment support is only justified when required to reach a specific unresolved branch.

## Validation remaining
- seed8 false-positive final correction
- 10-turn run
- 一閃づき / けものづき / きゅうしょづき
- ザキ free-camera/non-free-camera cases
- screenshot save + image-MCP inspection
