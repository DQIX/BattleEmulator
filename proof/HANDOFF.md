# d20 proof implementation handoff

## X
Implement the proof kernel/worldline solver so it soundly proves the bounded decision result required by the three proof specs, connected to the authoritative `BattleEmulator` semantics. In particular, keep an exact TRUE witness and make `D(N-1)=false` independently checkable; candidate failure/search exhaustion must never become FALSE.

## Anti-XY / specification-monster rule
Do **not** implement a representation, optimization, compression scheme, data structure, or other design suggestion merely because prose in a specification mentions it. A spec design example is not automatically a required implementation objective.

Concrete example: the kernel-spec discussion of reversible state bit-packing / `uint64_t[3]` is a design discussion, not a reason to redesign `RawState` or add bit packing. Do not implement bit packing unless actual profiling/evidence shows it is a material bottleneck and changing it directly advances X without compromising semantics. It is expected that packing/unpacking may itself be a bottleneck.

Do not collapse different semantic layers into one rule just because they both mention the same action. In particular, FLEE has two separate concerns:

- **In-game / command selectability:** the player cannot select FLEE while sleeping or paralyzed. TRUE witness replay, prefix replay, concrete candidate validation, and the proof problem's legal command set must respect this in-game legality.
- **BattleEmulator implementation shortcut:** `BattleEmulator::Main` may accept an already-prepared FLEE in states where the actual UI/game could not newly select FLEE, because the emulator uses that internal command path for speed/simplification. That internal acceptance is not evidence that FLEE is an in-game selectable command.

Likewise, `BattleEmulator::Main`'s internal handling of an already-prepared FLEE is not itself evidence about whether the player could select that command at the turn boundary. Keep **selection legality**, **execution behavior after selection**, and **proof-only conservative legality enlargement** distinct. Do not “fix” one layer to make it look like another.

Keep the existing user constraints: no hardcoded known answers, no candidate/search exhaustion as FALSE, no unrelated cleanup/refactor, no commit/push unless explicitly requested.

## Do not promote remediation into X
Finding a mismatch, reverting a bad edit, reconciling prose, restoring an older behavior, adding a regression test, or cleaning up a semantic boundary is **not a new objective**. Those are local corrective actions only when they directly unblock the proof objective above. Do not stop after a rollback/reconciliation and present that as progress completion; immediately return to the remaining proof-kernel/worldline obligations.
