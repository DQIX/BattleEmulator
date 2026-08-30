# Free-camera live handoff
Updated: 2026-08-30 18:26 JST

## Working rule
- Confirmed ROM behavior is reflected into `BattleEmulator/camera` immediately; do not wait for the whole investigation to finish.
- Do not encode encounter-, seed-, monster-, actor-slot-, or action-order-specific fixes.
- ROM fixed data must be mined through existing scripts under `C:\Users\owner\Documents\tunnelworkspace\BattleArrow`; extend/reuse them when additional data is needed.
- DeSmuME harness is the execution oracle. Ghidra is a reference for stable/static code; live overlay disassembly is authoritative for swapped overlay addresses.

## Confirmed and already reflected
- DQ9 actor IDs are arithmetic (`Ally i -> i`, `Enemy i -> 0xC0+i`).
- `WHIPPING_BOY` maps to DQ9 action `109` and is present in `freecam_action_mapper.hpp`.
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

## Build/validator state
- 2026-08-30 18:44 JST: CLion CMake target `freecam_edge_validation` builds and exits 0 after the FCMA v3/runtime/setup-work-state changes: `PASS freecam-edge-validation` (`goalCases=23408`, `moving=19536`, `zakiSuppressed=3872`, `zakiTriggered=19536`).
- CLion's single-file/clangd inspection does not inherit the CMake `CMAKE_CXX_STANDARD 20` flags and falsely reports existing `std::span`/`std::bit_cast` symbols unresolved; this is not used as the build oracle. CMake target execution is the build oracle.
- The first `gerunikku`/`erugi1_gilyumei` build attempt exposed a production-wiring omission: `camera.cpp` called `ApplyKnownRosterField4PostActionCompatibility` but the helper had not actually landed in `freecam_fast_runtime.hpp`. The helper is now present; rebuild immediately follows this update.
- 2026-08-30 18:49 JST: CLion `gerunikku` Run configuration reached process launch (`fullOutputPath` issued), confirming the production `camera.cpp` translation unit now builds after the helper fix.
- `freecam_edge_validation` now contains explicit regression coverage for the observed 12-slot type-1/type-17 initial masks, unknown-type invalidation, and the exact `021E2904` conflict side effect (`row+4` compatibility slot becomes nonzero while the actor auxiliary node is invalidated).

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
1. Wire generic all-actor presentation scan into production `camera.cpp`, using current/future action data and existing movement-eligibility/target-resolution APIs.
2. Keep the unresolved `row+4` decision behind a dedicated compatibility-state predicate; do not hardcode seed8 observations.
3. Mine any fixed per-action data only with existing BattleArrow scripts, then generate/consume it in the fast runtime.
4. Run CLion validators and real-ROM seed8 comparison after each production change.

## Validation remaining
- seed8 false-positive final correction
- 10-turn run
- 一閃づき / けものづき / きゅうしょづき
- ザキ free-camera/non-free-camera cases
- screenshot save + image-MCP inspection
