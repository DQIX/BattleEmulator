# BattleEmulator debugging notes for ChatGPT

## Command ID interpretation: never compare IDs before mapping them

There are multiple action-ID namespaces in this repository. Treating them as the same namespace creates false mismatches.

- `battle_command_mcp.js` / ROM traces use DQ9 action IDs.
- `BattleEmulator::Main`, `Gene`, `BattleResult.actions[]`, and the `--trace-turn` CLI use BattleEmulator common/internal action IDs.
- `camera/dq9_action_mapper.hpp` is the confirmed mapping between BattleEmulator common/internal action IDs and DQ9 action IDs. Check this mapping before claiming an action mismatch.

Concrete examples currently confirmed in this repository:

| Meaning | DQ9 action ID | BattleEmulator common/internal ID |
|---|---:|---:|
| 一閃づき / THUNDER_THRUST | `72` / `0x0048` | `45` |
| ゲルニックかばう / WHIPPING_BOY | `929` / `0x03A1` | `180` |
| かぶと割り / HELM_SPLITTER | `109` / `0x006D` | `181` |
| メラミ / GERUNIKKU_MERAMI | `10` / `0x000A` | `183` |
| バギマ / GERUNIKKU_BAGIMA | `19` / `0x0013` | `184` |

In particular, **DQ9 `0x03A1` is not a damaging attack and is not かぶと割り. It is `WHIPPING_BOY` / かばう, common/internal action `180`.** DQ9 `109` is the separate `HELM_SPLITTER` / かぶと割り action, common/internal action `181`.

## `--trace-turn` argument semantics from `main.cpp`

For a `gerunikku` build, `main.cpp` parses:

```text
--trace-turn <initialSeed> <BattleEmulator common/internal action ID> <heroTargetOverride> <currentSeedPosition>
```

The implementation does the following:

```cpp
const uint64_t traceSeed = std::stoull(argv[2], nullptr, 0);
const int traceAction = argc >= 4 ? std::stoi(argv[3], nullptr, 0) : BattleEmulator::DEFENCE;
const int traceTarget = argc >= 5 ? std::stoi(argv[4], nullptr, 0) : -1;
const int currentSeedPosition = argc >= 6 ? std::stoi(argv[5], nullptr, 0) : 0;
...
makeDebugGene(traceGene, 1, traceAction);
...
int tracePosition = currentSeedPosition + 1;
lcg::init(traceSeed);
BattleEmulator::Main(&tracePosition, 1, traceGene, tracePlayers, &traceResult,
                     traceSeed, nullptr, nullptr, -1, &traceState, traceTarget, true);
```

Therefore:

1. The action argument is **not** the DQ9 action ID. It is the BattleEmulator common/internal ID placed directly into `traceGene`.
2. The target argument is passed as `heroTargetOverride` to `BattleEmulator::Main`.
3. The CLI receives `currentSeedPosition`, then `main.cpp` starts the battle emulator at `currentSeedPosition + 1`.
4. `TRACE record[i] action=...` prints `BattleResult.actions[i]`, so it is also a BattleEmulator common/internal action ID, not a DQ9 action ID.

For the current Geruniku ROM harness comparison, selecting `skill:72` (一閃づき) against `enemy:0` corresponds to the finite C++ comparison command:

```text
--trace-turn <22-bit-seed> 45 1 1
```

The `45` is deliberate: DQ9 action `72` maps to BattleEmulator `THUNDER_THRUST = 45`.

Do not replace `45` with `72` merely because the ROM command or `battle_command_mcp.js` reports `skill:72`.

## Do not interpret `battle_damage_trace` helper values as actual HP damage without checking action semantics

`battle_damage_trace.js` observes internal ROM damage-related routines. A value named `final-damage` in that trace is not automatically equivalent to final HP loss for every action.

The important counterexample is DQ9 `0x03A1` / BattleEmulator `WHIPPING_BOY` (`180`). The C++ implementation intentionally executes the generic damage helper path for RNG/behavior fidelity, then applies guard state and discards the computed damage:

```cpp
case BattleEmulator::WHIPPING_BOY:
    (*position) += 5;
    baseDamage = FUN_0207564c(position, players[attacker].defaultATK, players[defender].def);
    if (baseDamage == 0) {
        baseDamage = lcg::getPercent(position, 2);
    }
    if (baseDamage != 0) {
        (*position)++;
    }
    players[defender].guardedBy = attacker;
    baseDamage = 0;
    resetCombo(NowState);
    break;
```

So a ROM damage trace may show a nonzero intermediate value such as `21` during `0x03A1`, while the real action is still かばう and causes no HP damage. Do **not** report this as "ROM damage 21 vs C++ damage 0" unless an actual HP delta or final action semantics independently prove damage occurred.

This is not unique to `WHIPPING_BOY`. Confirmed examples from bounded ROM-vs-C++ runs include:

- DQ9 `0x009B` / `EERIE_LIGHT` (BattleEmulator `185`): ROM damage helper can report `1`, while final emulator action damage is `0`.
- DQ9 `0x0037` / Geruniku マホカンタ (BattleEmulator `194`): ROM damage helper can report `1`, while final emulator action damage is `0`.
- DQ9 `0x002A` / スクルト (BattleEmulator `173`): ROM damage helper can report nonzero values such as `20` or `16`, while final emulator action damage is `0`.

Therefore the general rule is: for non-damaging state/status/buff/guard actions, `battle_damage_trace` can expose a generic internal defense/damage helper result that is not the semantic HP delta.

For state/status/guard actions, compare all of the following before declaring a mismatch:

- mapped action identity,
- RNG checkpoints/position,
- actual HP delta,
- relevant state change such as `guardedBy`,
- final `BattleResult` semantics.

## Seed-position comparison

The standard ROM harness setup for this comparison is `Ctable_jp.setSeedFromInitial(initialSeed, position=1)`. The matching `--trace-turn` argument is `currentSeedPosition=1`, because `main.cpp` then starts `tracePosition` at `2` via `currentSeedPosition + 1`.

Do not declare an off-by-one bug only because Ctable and C++ print different position conventions. Check individual RNG checkpoint numbers first. In the confirmed `0x300f74` comparison, ROM checkpoints `#15 ... #84` matched the C++ boundary/consume sequence, while the terminal display was Ctable position `84` versus C++ `position=85`; that is consistent with last-consumed-index versus next-position representation.

## Confirmed reference comparison: seed `0x300f74`

ROM command: 一閃づき (`skill:72`, DQ9 `0x0048`) to `enemy:0`.

C++ finite trace command:

```text
--trace-turn 0x300f74 45 1 1
```

The five core action identities map as follows:

```text
ROM DQ9 0x0048 -> C++ 45  THUNDER_THRUST
ROM DQ9 0x0013 -> C++ 184 GERUNIKKU_BAGIMA
ROM DQ9 0x000A -> C++ 183 GERUNIKKU_MERAMI
ROM DQ9 0x0001 -> C++ 1   normal attack
ROM DQ9 0x03A1 -> C++ 180 WHIPPING_BOY / かばう
```

For this seed the core RNG boundary sequence matched. The first real damage values also matched (`14`, `33`, `5`). The ROM `0x03A1` internal helper value must not be treated as HP damage; it is the guard action described above.

## Procedure before claiming a ROM/C++ mismatch

1. Read the current `main.cpp` CLI branch being invoked; do not rely on remembered argument order.
2. Identify whether each observed action ID is a DQ9 ID or a BattleEmulator common/internal ID.
3. Map through `camera/dq9_action_mapper.hpp` and verify the corresponding constant in `BattleEmulator.h`.
4. For `--trace-turn`, pass the common/internal action ID, not the DQ9 ID.
5. Compare action identity and RNG checkpoints before comparing damage numbers.
6. For guard/status/state actions, verify actual HP/state effects instead of treating an internal damage-helper trace value as final damage.
7. Only after those checks may an observed difference be called a behavioral mismatch.

## Multi-turn debugging is the default goal

When the user asks to continue ROM-vs-C++ battle debugging, the intended unit of comparison is **multiple consecutive turns in the same battle state**, not many isolated one-turn trials. A one-turn trace is useful only to localize an already-observed multi-turn divergence or to verify one specific mechanic.

Use the finite multi-turn CLI paths already present in `main.cpp`:

```text
--trace-battle <initialSeed> <turnCount> <BattleEmulator common/internal action ID> <heroTargetOverride> <currentSeedPosition>
```

`--trace-battle` repeats the same hero action for `turnCount` turns. `main.cpp` fills the gene with that common/internal action and starts at `currentSeedPosition + 1`.

For a different hero command on each turn, use:

```text
--trace-main-sequence <initialSeed> <currentSeedPosition> <action:target> <action:target> ...
```

Each `action` is a BattleEmulator common/internal action ID. `target` is packed with it using `BattleEmulator::PackHeroAction`. Prefer these bounded multi-turn paths over unbounded search/exploration algorithms for direct ROM/C++ comparison.

The ROM side must likewise remain in one continuous battle for the compared turns: set the initial seed once, then issue each turn's command through `battle_command_mcp.js` without reloading the State between compared turns. Compare per-turn action order, mapped action identity, RNG positions/checkpoints, real HP/state changes, and final battle state.

Do not turn "multi-turn debugging" into "run one turn on many seeds". Seed diversity is secondary; consecutive-state fidelity across turns is the primary test.

Before using turn N+1 as battle-core evidence, verify that the full end-of-turn RNG phase after turn N is still aligned. Remember the representation rule: when C++ reports final next `position = P`, an aligned `Ctable_jp.readSeed()` normally reports last-consumed position `P-1`. If core logic matched but an out-of-scope camera/presentation path consumed an extra RNG before the next turn, stop that seed at the last aligned core turn. Do not call the next turn's changed speed order/enemy actions a battle-core mismatch; choose another seed whose presentation path leaves the next-turn phase aligned.

## Mandatory mismatch repro file

At the **first confirmed ROM/C++ divergence**, stop broad testing and immediately create a **new UTF-8 text/Markdown file** under this `BattleEmulator` directory. Do not overwrite an older mismatch file. The new file must record at minimum:

- initial seed exactly as used,
- every C++ CLI argument needed to reproduce the run, preferably the complete `--trace-battle` or `--trace-main-sequence` argument string,
- ROM command sequence up to and including the divergent turn, using DQ9 command/option IDs and targets,
- first divergent turn/action index,
- ROM observed value and C++ observed value,
- RNG position/checkpoint immediately before and after the divergence when available.

A provisional observation is not enough to create a false mismatch: first apply the ID mapping and internal-damage-helper rules in this document. Once the difference is confirmed as semantic, save the repro file before deeper diagnosis so the seed and command arguments cannot be lost.

## Tool boundary

Do not assume Ghidra/decompiler access. In this environment ChatGPT may have CLion/source-file access and DeSmuME ROM-harness probes, but no Ghidra authority unless a tool explicitly provides it. Diagnose with the available C++ source/runtime and ROM harness evidence; do not fabricate disassembly/decompiler findings.

When using `desmume_harness__restart_analyze`, inspect its returned `paused`/`running` state before sending commands through `battle_command_mcp`. If the emulator is paused, resume it first; otherwise `confirmOption` can fail with `fight action menu did not open` even though the seed and command mapping are correct. Treat that as a harness execution-state issue, not a ROM/C++ behavioral mismatch.

重要な前提資料
C:\Users\owner\Documents\tunnelworkspace\BattleArrow\dq9-skill-catalog\reports\battle-structure-interim-report.md
C:\Users\owner\Documents\tunnelworkspace\BattleArrow\analysis


271. [13A] - Hootingham-Gore (1)
     Type: Bird
     Level: 45    # of turns: 2
     HP:    1854    MP:     255
     Exp:  18500  Gold:    4050
     Atk: 125     Def: 238  Agl: 148
     Evade:  4%  Block:  0%
     Fire: 100                               Ice: 100
     Wind: 050                               Lightning: 125
     Earth: 125                              Dark: 050
     Light: 150                              Dazzle: 025
     Sleep: 000                              Death: 000
     Drain: 100                              Confuse: 000
     Fizzle: 000                             Stun: 025
     Poison: 000                             Paralyze: 000
     Blunt: 050                              Sap: 025
     Decelerate: 025                         Magic Resist: 075
     Charm: 005        

186. [118] - bad karmour
     Type: Material
     Level: 37    # of turns: 1
     HP:     402    MP:      10
     Exp:   2280  Gold:     132
     Atk: 161     Def: 256  Agl:  98
     Evade:  0%  Block:  4%
     Fire: 050                               Ice: 100
     Wind: 050                               Lightning: 050
     Earth: 150                              Dark: 100
     Light: 150                              Dazzle: 075
     Sleep: 000                              Death: 050
     Drain: 100                              Confuse: 000
     Fizzle: 050                             Stun: 000
     Poison: 100                             Paralyze: 000
     Blunt: 050                              Sap: 100
     Decelerate: 050                         Magic Resist: 100
     Charm: 005                              
