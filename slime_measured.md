# Slime.dst — measured implementation inputs

Source of RNG truth: `../battle_harness/Ctable_jp.js`, managed lane
`slime-rom-20260929-a`, 2026-09-29. Turn/damage probes describe actions but
do not replace C-table checkpoint comparisons.

## Initial actors

| Raw ID | Actor | HP/MP | ATK/DEF/AGI | Battle monster ID |
|---|---|---|---|---|
| 0 | あ, level 1 | 20/6 | 16/14/8 | — |
| 1 | イザヤール, level 20, tactics 3 | 70/84 | 47/51/0 | — |
| 192 | スライムA | 8/2 | 10/7/7 | 0x122 |
| 193 | ズッキーニャ | 10/2 | 12/9/10 | 0x124 |
| 194 | スライムB | 8/2 | 10/7/7 | 0x122 |

Actor pointers: table `020f33e0 + rawId*4`; combat pointer at actor+138.
The old party-monitor HP addresses were for a different party order. The
analysis lane explicitly uses `020f38e0`, `020f41ac`; the default harness
configuration remains unchanged for other states.

Enemy scheme 3 is sequential: slime `[1,1,225,225,225,225]`, cruelcumber
`[1,1,1,225,225,225]`. These are DQ9 action IDs, not common IDs.
Each live actor consumes speed RNG, including the zero-speed guest.
Party flee suppresses both allies' actions and the guest AI calculation.

## Raw checkpoint observations

- Fresh 22-bit seed `0x18e0e1`, defend, defend, flee:
  - Turn 1: consumed 138; HP `[18,70,8,0,8]`; live seed `c650adfe32eb7873`.
  - Turn 2: consumed 251; HP `[18,70,0,0,8]`; live seed `3eaeb9712cc7fb52`.
  - Turn 3: remaining slime flees; last consumed checkpoint 268.
- Fresh 22-bit seed `0x11e04c`, flee:
  - Turn 1: consumed 68; HP `[19,68,8,10,8]`; live seed `c198f47df6349860`.
  - Attacks in order: 193->1, 194->1, 192->0, each damage 1.
- Controlled HP calibration, **not an unchanged-DST verification**:
  reload Slime.dst, write hero HP12, seed `0x18e0e1`, defend.
  Guest chooses Hoimi at hero HP10, heals 33, MP84->82, final hero HP20.
  Last consumed checkpoint 138. The same final position as the normal
  run does not imply the same intermediate RNG or camera events.

## Guest AI observations

The available actions are exactly attack (DQ9 1) and Hoimi (DQ9 30).
Tactics 3 dispatches `overlay_d_24:021f8bc4`. Normal mode consumes 44 RNG:
one float at LR021f87f4, 21 RandInt(100) at LR021f884c, 21 inclusive
ranges at LR021f88ac (including fixed/zero ranges), and a float(0,0.9)
at LR021f88d0. Selection and scoring consume no further RNG in the
observed attack and healing paths.

Normal attack selection prefers the highest HP enemy among targets that
the estimated attack can kill; exact decision branch: `021f954c`.
The HP threshold for Hoimi is captured at context+124 by the runtime
probe; do not invent a probabilistic heal chance.

## Presentation inputs

Initial world positions, in raw actor order:
`[(0,12868,18432),(21283,12868,18432),(-5320,0,-9216),
(10641,0,-18432),(26604,0,-9216)]`; start nodes `[58,60,30,23,33]`.
Initial flags `[2,2,0,0,0]`, movement enabled for all five, aux/target255.
Battle world positions:
`[(3072,204,10240),(-3072,204,10240),(-6963,204,-10240),
(0,204,-10240),(6963,204,-10240)]`.
Radii `[4096,4096,3276,8192,3276]`.
Hero equipment-derived model pair `(2,1)` is read from ROM, not inferred
from item names. Guest has no ordinary equipment records.

Initial five-row stack compatibility was observed as nonzero mask11001.
Subsequent stack writers differ from the old four-actor Gerunikku state;
retain the full camera runtime and compare each boundary, not only the
final RNG position.
