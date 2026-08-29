### Free-camera runtime entry (2026-08-29)
- Runtime overlay function entry: `0x0216FDA4`.
- First camera RNG call: `0x0216FE3C: BL 0x02075488`, return/LR observed by C-table as `0x0216FE40`, `RandInt(100)`.
- Camera retry RNG when counter is 1..4: `0x0216FE64: BL 0x02075488`, return/LR `0x0216FE68`, `RandInt(5-counter)`.
- Runtime structure fields used by this function: `camera+0x25C` = retry counter; `camera+0x262/+0x263` are actor/target ids passed into the presentation route.
- These addresses match the counter/RNG structure represented by `camera::onFreeCameraMove()`. Do not replace the path with an unconditional per-turn `position++`; identify the triggering presentation/action and route it through `camera.cpp`.
- Seed `0x1A` runtime evidence: after turn-end RNG `#75 lr=0x0215962C`, the next RNG is `#76 lr=0x0216FE40`; freecam entry `0x0216FDA4` was reached with `LR=0x021DC624`, `R1=0xC0`, `R2=0`, `R3=0`, matching Iron C0 -> ally target. This is the Iron normal attack (`ATTACK_ENEMY`) free-camera consumption. `ATTACK_ENEMY` therefore maps to DQ9 runtime action ID `1` in `freecam_action_mapper.hpp` and must be sent through `camera::onFreeCameraMove()`.
- Four-actor Geruniku turn can record up to five actions (player + Iron C0 + Geruniku x2 + Iron C2). Camera action traversal must inspect all five records; the old 3-action traversal can silently drop the later Iron normal attack and miss its camera RNG.# DQ9 BattleEmulator 完全再現ハンドオフ
## 目的
- ゲルニック将軍戦を起点に、DQ9実ROMと乱数消費およびダメージを完全一致させる高速 `BattleEmulator` を実装する。
## 現在の重要事項
- `BattleEmulator.cpp` 全体を確認済み。
- `Main()` は `[主人公, 鉄甲C0, ゲルニックC1, 鉄甲C2]` の `Player[4]` を直接操作する4actor実行loopまで実装済み。Ironはjudgment 1としてturn-record生成時に選択、Gerunikuはjudgment 2として各行動実行時に選択し、2回行動も1回目実行後に再選択する。
- `BattleEmulator` は独立Git repo。現在branchは `gerunikku1_new_arugo`。作業開始時点でユーザー既存変更は `CMakeLists.txt` と `camera.h`。
- 古いcommit `244c395306eaca0b2b3dadbfb393aabc62bf1442` には、空switch化前の1対1実行loopが残っている。状態異常、`callAttackFun()`、HP反映、行動後処理の再利用元にする。
- 4actor初期値は `main.cpp` の `copiedPlayers[4]`: 主人公、鉄甲魔人C0、ゲルニックC1、鉄甲魔人C2。
- `Player` は4actor直結用に `magicResistanceLevel`, `confused`, `confusionTurns`, `guardedBy` を末尾へ追加済み。既存aggregate初期化の位置は変えていない。
- ゲルニック固有common IDとして `GERUNIKKU_MERAMI`, `GERUNIKKU_BAGIMA`, `EERIE_LIGHT`, `GERUNIKKU_MEDAPANI`, `GERUNIKKU_BAGIMA_STRONG` を追加済み。マホカンタは既存 `MAGIC_MIRROR` を使用する。
- `Genome`、A* hash、`ActionOptimizer`、`SimpleParameterOptimizer`、`SearchRequest`、`BruteForceRequest`、`mainLoop` の戦闘配列は `Player[4]` へ統一済み。ホットループ内の2体viewは作らない。
- `BattleResult` 自体は1000 record保持できるが、`main.cpp::dumpTable()` の表示バッファが `eAction[2]/eDamage[2]` のままだったため、4actor化後の3件目敵行動で範囲外書込みしてクラッシュしていた。CLion debuggerで1ターン `traceResult.position=5`, `isEnemy=[0,1,1,1,1]` を確認し、表示バッファと出力列を4件へ修正済み。修正後は debugger expression `dumpTable(traceResult, traceGene, -1).size() == 586` まで正常完了。
- `gerunikku` targetは4actor Main実装後もコンパイル・リンク成功済み。`--trace-turn <seed> [action]` の1ターン比較入口を `main.cpp` に追加済み。
## 根拠資料
- `C:\Users\owner\Documents\tunnelworkspace\dq9-skill-catalog\reports\selector-battleemulator-spec.json`
- `C:\Users\owner\Documents\tunnelworkspace\Restricted-behavior\analysis\CURRENT_HANDOFF.md`
- `C:\Users\owner\Documents\tunnelworkspace\Restricted-behavior\analysis2\CURRENT_HANDOFF.md`
- Ghidra program `dq9_new2.nds`
- DeSmuME persistent `Ctable_jp.js`
- `battle_harness\battle_damage_trace.js` は補助照合用。overlay由来の偽entryがあるため単独根拠にしない。
## AI実測
- 鉄甲魔人: judgment 1, scheme 1, actions `03A1,0001,0001,006D,002A,00AF`, weights `68,58,48,38,27,17`。
- ゲルニック: judgment 2, scheme 1, actions `0013,000A,009B,0037,0390,01CF`, weights `68,58,48,38,27,17`。
- judgment 1はturn-start selection、judgment 2はacting-time selection。ゲルニックの2回行動は2回ともacting-timeで選ぶ。
- `FUN_02160cfc` の `RandInt(2)` は全敵actorのturn-record生成中に1回消費する。Gerunikuのaction-count profile index 2は固定値1なので追加1行動、Ironのprofile index 0は固定値0。値自体は不要なので実装は `(*position)++; // max: 2, lr: 0x02160d64`。
- scheme1の6slot全滅時は再抽選せずaction ID 2へfallbackし、今回の1人partyでは通常攻撃と同じtarget構築 `0x02156874 -> 0x0216139c -> 0x021613b0` の3乱数を消費する。
- Iron通常攻撃/もろば斬りのturn-start target構築は `0x02156874`, `0x0216139c`, `0x021613b0`。かぶと割りhandler 72はrepeat=1では `02161360` を通らず `0x02156874` のみ。かばうは `0x021ee074`、スクルトgroup選択は `0x021ef980`。
## ダメージ/状態で確定済み
- `00AF`: incomingを1.5倍、追加RNGなし、最終damageの25%を反動。
- `006D`: generic physical後に防御低下 `RandInt(100)`。
- `006D`: attack record `+0x32 == -1` のため対象combat耐性を使用する。この主人公buildでは `combat+0x50 == 50` をlive RAMで確認済み。成功判定は `RandInt(100) < 50`, LR `0x021e3e7c`。成功時DEF stage -1、下限-2、既存表現で `BuffTurns=7`。
- `009B`: magic resistance stageを-1、下限-2。magic damage multiplierは `1 - 0.25*stage`。
- `009B` ぶきみなひかりはgeneric physical `FUN_0207564C` を通らない。共通先頭は `0x0216139C` -> `0x021613B0` -> `0x021EC6F8` -> `0x02158584` -> `0x02157F58` (`RandInt(100)`, threshold 50)。seed `0x1A` turn 3ではroll 87で失敗し、その後は `0x021ED7A8` 1消費だけ、magic resistance stageは変更しない。seed `0x1` ではroll 11で成功し、その後は `0x021E81A0(max=2)` -> `0x021E54FC(max=100)` -> `0x021ED7A8(max=100)` の3消費、成功時だけmagic resistance stageを1段低下（下限-2）。旧CPPのphysical damage近似と無条件stage低下は削除し、成功/失敗両経路を実測どおり実装済み。
- 上記修正後、seed `0x1A` / ぼうぎょ固定3ターンは実ROM turn-end `#227` / 主人公HP `261` に対しC++ `position=228` / HP `261`、敵HP `402,1854,402` で一致。positionはC++が「次に読む番号」なので実ROM #227消費済み <-> C++ position 228が正しい。turn 3の敵damage列も `バギマ12 / ぶきみなひかり0 / かばう0 / 通常攻撃4` で一致した。
- `000A`: `62 + rand[-10,+10)`。
- `0013`: `29 + rand[-15,+15)`。
- `01CF`: `44 + rand[-21,+21)`。
- `0037`: magic reflection状態。
- `0390` メダパニは無条件成功ではない。operation type 21 のdispatch entry `0x021FFD90` は `FUN_021DE0D4` を指し、成功時 `FUN_02088CC8` を呼ぶ。`FUN_02088CC8` は `0x02088CE0` で `combat+0x5E=3`、`0x02088CE4` で `combat+0x81=0`、`0x02088CEC-0x02088CF0` でstatus bit `0x20` を立てる。
- メダパニ成立時は、そのターンに選択済みの主人公の `ぼうぎょ` 状態も解除して通常倍率へ戻す。seed `0x1A` 実測では行動順が `ぼうぎょ -> バギマ -> メダパニ成功 -> 鉄甲魔人通常攻撃`。通常攻撃の物理raw RNGは実機/C++とも同一で、seed `0x1A` の70step seed上位32bitは `0x9423AC12`、実ROM `0x020756E4` の `floatRand(0,10.0625)` とC++ `FUN_0207564C()` はともにraw damage 5。それにもかかわらず実ROM最終damageも5であるため、メダパニ成立後は事前の `ぼうぎょ` 0.5倍が残っていない。CPPは `GERUNIKKU_MEDAPANI` 成功時に `players[defender].defence = 1.0` を即反映する。
- メダパニのaction record付随RNGは `0x0216139C` (`RandIntRange(3,4)`) -> `0x021613B0` (`RandIntRange(6,8)`)。CPPの旧 `(*position)+=2` はLRを失わないよう個別2消費へ分解済み。
- メダパニ成立判定は `FUN_021581F8`。この主人公buildではbase `25.0 * 0.75 + 0.5` を整数化して閾値19。実行時 `RandInt(100)` は LR `0x02157F58`、比較点は `0x02158258`。seed 2実測ではthreshold=19, roll=96で失敗したため、旧「無条件confused=true」は削除済み。
- メダパニの成立後RNGは成功/失敗で分岐する。seed `0x1A` 成功実測では `0x02157F58` の成功判定後に `0x021E81A0(max=2)` → `0x021E54FC(max=100)` が続き、その後action post `0x02159D40`。失敗実測では成功後2消費の代わりに `0x021ED7A8(max=100)` 1消費。CPPもこの分岐へ修正済み。
- 混乱時ランダム行動は `FUN_02160DFC`。最初に `RandInt(2)` LR `0x02160E14`、1人partyでは味方攻撃候補数<2のため値を捨てて4候補側へ強制し、`RandInt(4)` LR `0x02160F10` で `0x00DD / 0x0393 / 0x00DE / 0x0396` を均等選択する。`0x00DB` party attackは1人partyでは到達しない。
- `0x0393` はoperation type 24。dispatch table base `0x021FFCE8` のentry `0x021FFDA8` -> `FUN_021DE52C` -> call `0x021DE5F0` -> `FUN_02088B68`。`FUN_02088B68` は `0x02088BC0` で `combat+0x5C=3`、`0x02088BC4` で `combat+0x7F=0`、`0x02088BCC-0x02088BD0` でstatus bit `0x8` を立てる。同時に既存通常麻痺/混乱状態をclearする。
- status主counter更新は `FUN_0215B174`。`0x0215B198` で主counter base=`combat+0x5C`、`0x0215B19C` で回復counter base=`combat+0x7F`。offset table pointer `DAT_0215B3D0` -> `0x02183B4C` の先頭 `{0,1,2}`。`0x0215B1EC-0x0215B214` で該当主counterを1減らし、0到達時に回復counter=4を設定する。この関数は行動後 `FUN_021594BC` の `0x0215957C` から呼ばれる。
- status解除/行動差替え入口は `FUN_02159ABC`。`RandInt(100)` は call `0x02159B0C`（C-table LR `0x02159B10`）。status bit `0x8` branchは `0x02159B34` 以降、回復table compareは `0x02159B54`。status bit `0x20` branchは `0x02159BEC` 以降、table loadは `0x02159C10`。`0x0200BE88` のfloat比較はtable値 > roll/100のstrict比較で、`0.625/0.75/0.875/1.0`を整数RandInt(100)へ写す既存 `{62,75,87,100}` の比較ロジックと整合する。
- 被ダメージによる混乱解除は `FUN_02158A08`。action record `+0x10 & 0x800` があるdamage成立時に処理され、`FUN_02155B74` でstatus bit `0x20`（混乱）を確認する。player側は `FUN_02075B04(1)=0.5`、`DAT_02158B20=100.0` なので閾値50。`RandInt(100)` call後のLRは `0x02158AC4`、`0x02158AD0` で `roll < threshold` を比較し、成功時 `0x02158AF8` から `FUN_02088CF8` を呼んで混乱をclearする。seed `0x1A` 実戦ではメダパニ成功後の物理被弾でこの経路を踏んだ。
- `03A1`: ゲルニックを保護するactor-specific guard。
- generic spell/type-D damageの重要アドレス: `FUN_021E8458` のtype-D加算経路では `0x021E88D4` から `getFloatRandRange(0x02075514)` を呼び、`0x021E88F0` は `FloatAdd` のcallそのもの。したがってtype-D実結果をruntime hookで読む位置はcall後の `0x021E88F4`。`battle_damage_trace.js` も `0x021E88F0 -> 0x021E88F4`（別経路 `0x021E8680 -> 0x021E8684`）へ修正済み。seed `0x1A` の `0x0013` バギマでは実結果float bits `0x41DAAA20`（約27.33307）で、最終int 27はC++ `FUN_021e8458_typeD()` と一致した。
- type-D後の共通damage modifierは `FUN_021E7328`。generic damage本体 `FUN_021EBD9C` からのcall siteは `0x021ECF74`、返値の最終damage保存は `0x021ECF78`。`FUN_021E7328` は途中で整数truncateせずfloat倍率を積み、最後に `FloatToInt` する。属性倍率取得は `FUN_021582B8`。ユーザーが `Equipments::applyDamageReduction` 側の誤りを修正済みなので、今後はこの箇所を再度推測修正せず実機damageで再照合する。
## 現在の実装済み
- scheme1 fixed 6slot selector、limited used mask、lower→higher fallback、Geruniku acting-time selection。
- `GERUNIKKU_MEDAPANI` は成立時のみ `confused=true/confusionTurns=3` に修正済み。混乱の自分手番では `FUN_02159ABC` 相当の解除RNG後、残存時に `FUN_02160DFC` 相当の `0x00DD/0x0393/0x00DE/0x0396` 4択をcommon IDへ変換する処理を実装済み。`0x0393` は専用flagを増やさず既存paralysis状態へ変換する方針。
- generic physical damageで既に消費していた `0x02158AC4` の捨てRNGを、主人公が混乱中の場合のみ `RandInt(100) < 50` の実解除判定へ置換済み。追加の乱数消費は増やしていない。
- Iron/Gerunikuのcommon ID経路。メラミ、2種類のバギマ、ぶきみなひかり、マホカンタ、メダパニ、かばう、スクルト、かぶと割り、もろば斬りを `callAttackFun()` へ接続中/一部実装済み。
- もろば斬りは既存physical damageを通して1.5倍し、最終damage/4を反動として使用者HPへ反映。
- かぶと割りは既存physical damage後にLR `0x021e3e7c` のDEF低下判定を実装。
- かばうは「turn-record構築時の予約」と「行動実行後のactive guard」を分離している。予約だけで主人公攻撃を先回り肩代わりしない。
## 次の実装・照合順序
1. `--trace-turn` と `Ctable_jp.js` を同seedで比較し、最初にずれるRNG LRを特定してその場で修正する。
2. 新規 `callAttackFun()` caseに残る `(*position)+=N` を、C-tableで確認した個別LRコメント付き消費へ分解する。返値を使わないRNGは `getPercent()` 等を呼ばず `(*position)++` にする。
3. ぶきみなひかり/メダパニの成功判定と状態持続、混乱後の主人公行動決定を実ROMどおりにする。
4. priority action（特に `MERCURIAL_THRUST`）を4actor turn orderへ完全統合する。
5. `camera/freecam_fast_runtime.hpp` のコンパイル時metadataを `camera::Main()` へ接続し、ザキ→鉄甲魔人、一閃づき、けものづき、通常攻撃等のfreecam RNG/追尾を実ROMと照合する。
6. 複数ターンについてaction、damage、主要状態、turn-end RNG position、freecam状態が一致するまで繰り返す。
## 作業ルール
- 調査を貯めて最後にまとめて `BattleEmulator` を変更しない。確定した仕様から逐次実装する。
- 作業完了または実際の制限到達まで継続する。
- C-tableのLRアドレスは将来の照合キーなので、新しく追加/修正する乱数消費には可能な限り `max` と `lr: 0x........` をコメントで残す。
- 返値を捨てるだけの乱数はホットループで `lcg::getPercent()` 等を呼ばず、消費数が確定しているなら `(*position)++` / `(*position)+=N` とする。ただし複数消費は将来のLR照合のため可能なら1個ずつ記述する。
