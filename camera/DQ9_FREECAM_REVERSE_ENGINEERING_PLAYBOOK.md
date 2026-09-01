# DQ9 Free-Camera / Presentation Reverse-Engineering Playbook
## 目的
この文書は、DQ9 battle presentation/free-camera の乱数消費を BattleEmulator へ再現するための調査・実装手順を固定する。特定のゲルニック戦、特定action、特定turnだけに合わせた局所的な `+N RNG`、actor ID直書き、turn番号分岐を作らない。camera subsystem は味方1～4人、敵1体以上を同じ `BattleActorRef{side,index}` で扱い、DQ9 managed actor ID は必ず `dq9::freecam::fast::Dq9ActorId()` で変換する。
## 絶対に守ること
- `camera::onFreeCameraMove()` は既に実機RNG列と照合されている。freecam発火条件を調べる過程で、この関数のcounter reset・RNG消費・ATTACK_ALLY特例の意味を勝手に変更しない。
- `RuleForAction()` の既知高速 `switch/if` を「手動mappingだから」という理由だけで捨てない。hot pathでは直接分岐が最速である。
- ただし、同一actionでもroute/membership/selector suppressionでfreecam不成立になるものを無条件Ruleへ落とさない。ZAKIが代表例。
- camera差を `(*position)+=N` で埋めない。action本体終了positionが一致した後のpresentation差は、actor/action/freecam entryを実機で帰属してから直す。
- 敵数・味方数をcamera.cppへハードコードしない。ゲルニック戦のroster/初期座標/monster IDはBattleEmulator側のencounter setupが一般camera APIへ供給する。
- BattleEmulator player slotから `0xC0 + index` をcamera.cppで生成しない。camera境界では `BattleActorRef` を受け、`Dq9ActorId()`を使用する。
- `actor==0 ? ally : enemy` のようにBattleEmulator固有slot意味をcamera側へ漏らさない。
- generated ROM metadataはcompile-timeで使う。hot loopでROM/巨大binを開かない。BattleEmulator ecosystem共通のcommon/god IDは維持し、既知actionのcommon-ID→DQ9-IDとpresentation metadataは`dq9_action_mapper.hpp`でcompile-time解決する。freecam mapperへ一般action mappingの責務を持ち込まない。
- `freecam_action_mapper.hpp`は**free-camera trigger pipelineへ入り得るaction専用**。presentation stateを再現したい、future actorのtargetを知りたい、内部action IDを保持したい、という理由で非freecam actionを登録してはいけない。`kFreeCameraActions`完成後にconstevalで全slotを走査し、common/god ID slot整合・一般DQ9 metadata整合・generated freecam gateを検証する。DQ9 503/912/929のような非freecam actionは一般metadata側にのみ置く。
- DQ9 action ID・target分類・`operation_type`等は`camera/dq9-action-target-classification.csv`が既存のデータマイニング台帳。`build_freecam_fast_generated.mjs`がtarget side/scope、repeat mode、operation type、resource cost、target-handler judgment等を1024-entry constexpr tableとして`freecam_fast_generated.hpp`へ焼く。mapperの第一template引数をseed sweepで再発見しない。CSVで既に解決済みのIDを再調査するのは車輪の再開発。
- freecam mapperのbuild gateはgenerated constexpr値を使うが、`kHasAnyMinedFreeCameraTriggerSource`をfreecam可否そのものと同一視しない。BACT/membershipはtrigger-source候補の材料であり、誤った中間実測を個別`static_assert`へ固定しない。最終的なmapper整合は`kFreeCameraActions`のconsteval全走査で検査する。巨大membership表をbindingごとにconsteval総走査しない。
- 画像が必要なら `C:\Users\owner\Documents\desmume_webassembly_harness\harness\screenshots` をisolationへ追加して画像ツールで読む。許可root外だったことを理由にUI確認を諦めない。
## 主要ソース
- `camera.cpp`, `camera.h`: BattleEmulatorとのadapterと既存RNG消費。
- `camera/freecam_actor.hpp`: BattleActorRefとDQ9 actor ID変換。人数に依存させない。
- `camera/dq9_action_mapper.hpp`: BattleEmulator ecosystem共通common/god ID→ROM DQ9 action IDと一般presentation metadataのcompile-time binding。
- `camera/freecam_action_mapper.hpp`: 上記一般metadataを参照し、freecam対象actionだけを`Bind<CommonAction>()`する専用mapper。
- `camera/freecam_fast_runtime.hpp`: ROM metadataをconsteval抽出し、route/membership/selector suppressionでTriggerDecisionを作るfast runtime。
- `camera/freecam_route.hpp`: presentation node graphとroute planner。
- `camera/freecam_setup.hpp`: goal assignment、occupancy、fallback goal、presentation state。
- `camera/freecam_fast_validation.cpp`, `camera/freecam_validation.cpp`: ROM由来ロジックのfixture/self-test。
- `C:\Users\owner\Documents\tunnelworkspace\BattleArrow\build_freecam_action_metadata.mjs`: ROM `actdata_a/b.gp2`からattack structure固定metadataを抽出。
- `C:\Users\owner\Documents\tunnelworkspace\nitro-fs.js`: NitroFS parser。
- `C:\Users\owner\Documents\tunnelworkspace\battle_harness\gp2.js`: GP2 parser/decompressor。
- `C:\Users\owner\Documents\tunnelworkspace\battle_harness\narc.js`: NARC parser。BACT/actor resource抽出が必要な場合に使う。
- BattleArrow側の `extract_freecam_memberships.mjs`, `build_freecam_membership_metadata.mjs`, `build_freecam_selector_projection.mjs`, `build_freecam_action_metadata.mjs` を改造・再生成して使う。新しい手書き表を増やさない。
## ROM固定metadata生成
### Attack structure
`data/prm/actdata_a.gp2::actdata_a.nat` と `actdata_b.gp2::actdata_b.nat` は0x34-byte record。
- `record+0x04 & 0xFFF`: DQ9 action ID。
- `(u32(record+0x18) >> 5) & 0x7F`: presentation type。0x0Cならfallback lookup actionは0x0158、それ以外は自身。
- `(u32(record+0x18) >> 12) & 0xF`: **attackFormationMode raw nibble**。target scopeやCSV分類から推測しない。
`build_freecam_action_metadata.mjs`は現在FCMA v3として、1024件のfallback lookup u16、1024件のformation mode u8、1024件のpresentation type u8を出力する。presentation typeはaction名・武器種・target scopeから推測せず、必ずROM recordから採掘する。現在の確認値:
- DQ9 0x001 通常攻撃: formationMode 0。
- DQ9 0x018 ザキ: formationMode 2。
- DQ9 0x019 ザラキ: formationMode 2。
- DQ9 0x045 しっぷうづき: formationMode 1。
- DQ9 0x046 けものづき: formationMode 1。
- DQ9 0x047 きゅうしょづき: formationMode 1。
- DQ9 0x048 一閃づき: formationMode 1。
生成例:
`node build_freecam_action_metadata.mjs "C:\Users\owner\Documents\tunnelworkspace\dq9_new2.nds"`
出力を `BattleEmulator/camera/freecam-action-metadata.bin` へ直接生成し、その後 `BattleEmulator/camera/build_freecam_fast_generated.mjs` を実行して `freecam_fast_generated.hpp` を再生成する。中間binを手で編集しない。

## Roster row+4 compatibility / compiler-stack state
`overlay_d_25:021E1958`が作る12-byte roster rowは`+0 actorId / +1 presentationClass / +8 actor pointer`だけを書き、`+4`は初期化しない。`021E08BC`はこの`+4`についてraw 32-bit値ではなく`==0 / !=0`だけを利用する。

重要: これは最後まで単なるgarbageではない。setup入口値は以前のpresentation/action pathが残したcompiler-stack residueだが、setup中は`021E2904`がconflict actorのrowへ明示的に`+4 = 1`を書く。live overlayで`021E29AC MOV R0,#1`→`021E29B0 STR R0,[R1,#4]`を確認済み。したがって実装は「post-action initial residue state」と「setup中のmutable work state」を分けず、同じzero/nonzero compatibility stateとして逐次更新する。

seed8で12 physical slotを直接実測した既知post-action shape:
- presentation type 1（Bagima/Meramiで一致）: nonzero `{0,1,2,5,6,7,8,9}`、zero `{3,4,10,11}`。
- presentation type 17（Zaki後）: nonzero `{0,1,4,5,6,7,8,9}`、zero `{2,3,10,11}`。

これらをactor ID、monster ID、action順の意味論へ置換しない。未知presentation typeのshapeは推測せず、C++総当たりで候補seed/actionを発見し、DeSmuME実測で12-slot shapeを採取してからproductionへ追加する。

### 未知pathの総当たり手順
未知compatibility producerを1 actionずつ人手で探し続けない。BattleEmulator側へ探索専用C++ modeを持たせ、ROM固定metadataを全actionについて列挙し、seed rangeを高速走査する。

探索modeの役割は次の通り。
1. ROM mining済み`presentationType / formationMode / selector projection / membership`を使い、指定条件に一致するaction/seed候補を大量列挙する。
2. 同じpresentation typeだけでなく、異なるtype・selector・route branchを含む代表候補を返す。
3. 候補をDeSmuME harnessへ投入し、実ROMのaction order、presentation type、12-slot row+4 mask、freecam有無、RNG positionを実測する。
4. 同じpresentation typeで複数action/seedが同一shapeになることを確認してから、そのtypeをproduction compatibility tableへ昇格する。
5. type内でshapeが分岐するならpresentation type単独分類を捨て、ROM/実測で判明した追加固定metadataまたはbranch条件を採掘する。action IDやseed番号で分岐してはいけない。

総当たり結果そのものをproductionデータとして信用しない。C++探索は候補発見器であり、productionへ入れてよいデータ源はROM miningまたはDeSmuME実測だけである。

実装済み探索CLI:
`gerunikku --scan-action-seeds <startSeed> <count> [turns] [perAction] [heroAction] [heroTarget] [wantedPresentationType] [currentSeedPosition]`

2026-08-30の20,000-seed sweep（hero=Zaki、target=C0）では、既存BattleEmulator AIが実際に生成できる全主要enemy actionについて代表seedを自動収集できた。未mapped actionの最初の観測候補:
- common 187 Bagima(strong): seed `0x1`。
- common 185 Eerie Light: seed `0x2`。
- common 31 Magic Mirror: seed `0x2`。
- common 181 Helm Splitter: seed `0x2`。
- common 186 Medapani: seed `0x5`。
- common 173 Kabuff: seed `0x6`。
- common 21 inactive/skip系: seed `0x6`。
- common 182 Double-edged Slash: seed `0x15`。

この一覧はmappingではなく**実ROM測定候補**。各common actionのDQ9 action ID / presentation type / 12-slot maskをseed固定ROM probeで採取して初めてproductionへ昇格する。

昇格済みの実例:
- common 187 `GERUNIKKU_BAGIMA_STRONG`: C++ sweep seed `0x1`を実ROMへ投入するとaction recordはDQ9 `463`。そのaction後の次setup入口12-slotはtype1既知shape `{0,1,2,5,6,7,8,9}=nonzero`と完全一致した。さらに`build_freecam_action_metadata.mjs ROM 463`でROM固定値`presentationType=1 / attackFormationMode=2 / fallbackLookupActionId=463`を確認。よって`Bind<463,...>`へproduction昇格済み。
- `build_freecam_action_metadata.mjs`は任意action IDを追加引数で受け、既存ROM parsing経路からそのIDのmetadataを出力できる。未知mapping確認のために別extractor/tableを作らない。
- seed `0x2`ではC++候補列の先頭2 enemy actionが実ROMと一致したため、common185 Eerie Light→DQ9 `155`、common31 Magic Mirror→DQ9 `55`をaction recordで確定。ROM miningで155=`presentationType 22 / formation1`、55=`presentationType 31 / formation2`。mapperへ昇格済み。ただしtype22/type31のpost-action 12-slot residueは未実測なのでcompatibility producer tableにはまだ入れない。
- common21は`INACTIVE_ENEMY`。200k sweepの`bestRecord=0 / bestSeed=0x1b2`をfresh ROMへ投入すると先頭enemy actionはDQ9 `503 (0x01F7)`。以前眠り/麻痺skipで観測した内部503と一致し、ROM miningも`presentationType=0 / formationMode=0 / fallback=503`。`INACTIVE_ENEMY -> Bind<503>`へproduction昇格済み。type0のfull 12-slot post-action residueはまだ未確定なのでproducer tableには追加しない。
- common186 `GERUNIKKU_MEDAPANI`: `bestRecord=0 / bestSeed=0x55`で先頭enemy action=DQ9 `912 (0x390)`をfresh ROMで確認。ROM miningは`presentationType=21 / formationMode=2 / fallback=912`。`GERUNIKKU_MEDAPANI -> Bind<912>`へ昇格済み。type21 residueは未実測。
- common21は`INACTIVE_ENEMY`。200k sweepの`bestRecord=0 / bestSeed=0x1b2`を実ROMへ投入すると先頭enemy actionはDQ9 `503 (0x01F7)`。これは以前眠り/麻痺skipで観測した内部503と一致し、ROM miningも`presentationType=0 / formationMode=0 / fallback=503`。`INACTIVE_ENEMY -> Bind<503>`へproduction昇格済み。type0のfull 12-slot post-action residueはまだ未確定なのでproducer tableには追加しない。
- 同じseedの3番目以降はC++とROM action列が分岐した。したがってseed候補の品質は「対象actionが何record目に現れるか」を重視する。`--scan-action-seeds`は各common actionについて最小`bestRecord`と`bestSeed`を集計し、最も短いprefixで実ROM検証できる候補を返す。
## managed actor ID
DQ9 managed battle actor ID:
- ally index i → 0x00+i。
- enemy index i → 0xC0+i。
必ず `Dq9ActorId(BattleActorRef{side,index})` で生成する。算術自体をcall siteに再実装しない。
ゲルニック戦で実機 `FUN_02154E60(controller, actorId)` の戻り値をprobeした対応:
- C0 = てっこうまじんA。
- C1 = ゲルニック。
- C2 = てっこうまじんB。
画面上の並びは **左→右: てっこうまじんA(C0), ゲルニック(C1), てっこうまじんB(C2)**。
これは実機fixtureの説明であり、camera production logicへ順序表として埋め込まない。
## Presentation object layout
実機probeとGhidraで確認済み:
- actor+0x13C → presentation object。
- presentation+0x10 = worldX。
- +0x14 = worldY。
- +0x18 = worldZ。
- +0x20 = presentationFlags。
- +0x24.. = route node bytes。
- +0x34 = routeCount。
- +0x4C = cached target ID。
- +0x4D = auxiliary target ID。
- +0x56 = movementEnabled。
`FUN_0204A264(actor)` は `actor+0x13C` がnullなら0、存在すればpresentation+0x34のrouteCountを返す。
`FUN_0204A230(actor,nodes,count)` はnodesをpresentation+0x24へcopyし、countを+0x34へ書く。
初期nodeは固定番号を手書きせず、worldX/worldZを `NearestPresentationNodeFast()`へ通す。
## ゲルニック戦fixture
`zaki.dst`初期presentation実測:
- Hero: world=(10641,12868,18432), node59, flags=0x2。
- C0/てっこうまじんA: world=(-5320,0,-9216), node30。
- C1/ゲルニック: world=(10641,0,-18432), node23, flags=0x80。
- C2/てっこうまじんB: world=(26604,0,-9216), node33。
monster ID:
- てっこうまじんA/B: 0x0C1 (Bad Karmour)。
- ゲルニック: 0x13A (Hootingham-Gore)。
これらの値はBattleEmulator側fixture/encounter setupから一般camera roster APIへ渡す。camera.cppのglobal固定表にしない。
## ZAKI freecam再現手順
### State
- 初ターンpattern: `C:\Users\owner\Documents\tunnelworkspace\battle_harness\zaki.dst`。
- 初ターン以外pattern: `C:\Users\owner\Documents\tunnelworkspace\battle_harness\zaki3.dst`。
### 操作
この2stateを「そこから数ターン進めるstate」と誤解しない。stateロード後、その**1ターンだけ**再現する。
方法A: stateロード→Aを約100ms間隔で連打→1ターン完了を待つ。
方法B: stateロード→スクショまたは`battle_command_mcp.seeUi`で現在カーソルを確認→その状態と同じ選択だけbattle MCPで確定→1ターンだけ通す。
`zaki.dst/zaki3.dst`ではザキ対象はてっこうまじんB(C2)だった。UI indexはstateごとに同じとは限らないため、indexを固定せず毎回確認する。
### Freecam entryの帰属
1ターン中にfreecam entry `0x0216FDA4` は複数回呼ばれる。最終positionや`p1=0`だけで「ザキfreecam」と決めない。
`0216FDA4`引数 p1/p2/p3/LR と、action/actor時系列をpersistent probeで並べる。
重要: `camera+0x262/+0x263`は0216FDA4関数内でparam2/param3を書き込む。**entry hook時点で+0x262を読むと前回値**なので、現在呼び出しのactor判定に使わない。
確認済みZAKI freecam call:
`p1=0, p2=0xC2, p3=0, LR=0x021DC5D8`。
### Route証拠
`zaki.dst`:
- Hero route count=2, nodes=[59,50]。
- 直後にZAKI freecam `p1=0,p2=C2`。
`zaki3.dst`:
- Hero route count=2, nodes=[66,67]。
- 直後にZAKI freecam `p1=0,p2=C2`。
ZAKIのaction selector projectionは0x00050001。`ComputeSelectorSuppression()`により、actor route count==0ならfreecam抑止、route count>0なら候補になる。したがって `case ZAKI: always free_camera` は不正確。
## Presentation routeの継続状態
ZAKI route `[59,50]` の終点50を次turn startとして単純採用してはいけない。`zaki.dst`の1ターン終了時Hero実座標はnode68だった。
実機routeログではZAKI freecam後、Heroに `route-write count=0, sourceFirst=68` が現れる。action routeの後にfallback/return position処理がある。
実装は既存 `AssignActorFallbackPresentationGoal()` とroute plannerを通し、action route endpoint→fallback goal→fallback route endpointの順にpresentation stateをcommitする。`turn==1 ? node68`などの専用分岐を作らない。

## `021DC1D4` later-action `param5=1` の正確な3条件
`overlay_d_25:021DC1D4` はfree-cameraを呼ぶかと第5引数 `param5` を選ぶselector。mode1 call siteは `0x021DC5D4` (`0216FDA4` entry LR=`0x021DC5D8`)、mode0 call siteは `0x021DC620` (entry LR=`0x021DC624`)。

later actionでmode1へ上げる条件は次の3系統で、`current target == current actor` ではない。

1. turn participantのpresentation route countが1人でも `>4`。
   `controller+0x57C8 != 0` のlater-action branchでparticipantを列挙し、`FUN_0204A264(actor)` の戻り値（presentation object `+0x34` route count）が5以上ならmode1側へ戻る。
2. **現在actionのtarget ID == 直前action recordのactor[0] ID**。ただし、この比較にはtarget presentation `+0x1E != 0xFF` のgateがある。
   - current target IDの取得経路: `FUN_0216268C(controller)` (`0x0216268C`) でcurrent 0x28-byte action recordを得る。record `+0x14` のtarget referenceからtarget actor IDを得て、`getDynamicAllocatedMemIndexStartAddr()` -> `FUN_0200FCC4(heap,targetId)` でtarget battle actor objectを引く。
   - `FUN_0204A20C(targetActor)` (`0x0204A20C`) がselectorで使うraw target presentation値を返す。実装は `0x0204A20C: LDR r0,[r0,#0x13C]` でactor object `+0x13C` のpresentation object pointerを読み、nullなら `0xFF`、非nullなら `0x0204A214: LDRBNE r0,[r0,#0x1E]` でpresentation object `+0x1E` を返す。
   - C++の `PresentationActorState::auxiliaryNode` はこのpresentation object `+0x1E`を保持するfieldなので、`targetAuxiliaryNode = presentationActors[FindPresentationActorIndex(currentTargetId)].auxiliaryNode` と算出する。**presentation配列のindex、party index、enemy indexを代用してはいけない**。target actorがruntime presentation rosterに存在しない場合だけROMのnull経路に合わせて`0xFF`。
   - `021DC1D4`では `FUN_0204A20C(targetActor) != 0xFF` のときだけ、以下のprevious-action actor一致判定へ進む。したがって別ボスで再演する場合も、固定値を持たず「current action record -> target ID -> actor object -> actor+0x13C presentation pointer -> presentation+0x1E」のpathから毎回算出する。
   - `0x021DC394`: `r0=controller`。
   - `0x021DC398`: `BL 0x021626CC`。
   - `FUN_021626CC` は `controller+0x57C8==0` なら0。1以上なら `[controller+0x218] + 0x821C + (index-1)*0x28`、つまり直前action recordを返す。
   - `0x021DC3A8`: `BL 0x02161814(previousRecord, 0)` で直前recordのactor[0]を解決。
   - `0x021DC3B4`: `LDRH r0,[actorRecord,#0x20]` でそのactor IDを読む。
   - `0x021DC3B8`: `CMP r0,r11`。`r11` は現在actionのprimary target ID。
   - `0x021DC3BC..0x021DC3C0`: equalならlocal force-mode1 flagを1にする。
   したがってC++で `current target == current acting actor` と実装するのは誤り。fast runtimeでは `state.previousAction.actorId` と現在targetを比較する。
3. 現在actorとtargetのworld geometryが重なる。
   - `0x021DC3D0..0x021DC3E0`: current actor object `+0x44` のVector3をstackへコピー。
   - `0x021DC3E4..0x021DC3F4`: target actor object `+0x44` のVector3をstackへコピー。
   - `0x021DC3F8`: `BL 0x020C4AFC` でdistanceを求める。
   - `0x021DC400`: current actorに `FUN_02037228`。
   - `0x021DC40C`: target actorに `FUN_02037228`。
   - `FUN_02037228` (`0x02037228`) は単純に actor object `+0x64` の値を返す。
   - `0x021DC414`: radiusを加算。
   - `0x021DC41C`: `CMP distance, (radiusA+radiusB) ASR #1`。
   - `0x021DC420..0x021DC424`: `distance < (radiusA+radiusB)/2` ならforce-mode1 flagを1にする。

## internal action `944 / 0x03B0` の生成規則
`0x03B0`を特定の技IDへ手書きで結び付けてはいけない。ROMではaction record内のactor snapshotがpresentation child slot 1を持つと、後処理でcleanup用のinternal self-action `0x03B0` が追加される。

再演に必要な構造とアドレスは次の通り。

- action recordのactor snapshot列はlinked list。`FUN_021617E8(actionRecord, actorSnapshot)` (`0x021617E8`) は `actionRecord+0x10` から空linkを探し、次snapshot pointer `actorSnapshot+0x30` を辿って末尾へ追加し、`actionRecord+0x08` のactor snapshot countを1増やす。
- snapshotをindexで読むのは `FUN_02161814(actionRecord,index)` (`0x02161814`)。`actionRecord+0x10` を先頭としてsnapshot `+0x30` をindex回辿る。したがって `+0x27` はbattle actor objectのfieldではなく、この **0x34-byte action actor snapshot** のfield。
- snapshot poolは `FUN_02160098(battle)` (`0x02160098`) が算出する。countは `battle+0x8E00`、strideは`0x34`、先頭は`battle+0x20`、上限は`0x48`。よって `snapshot = battle + 0x20 + (battle[0x8E00] * 0x34)`。この式から算出し、fixtureの絶対RAMアドレスを固定値にしない。
- action record poolは `FUN_02160158(battle)` (`0x02160158`) が算出する。action countは`battle+0x8E24`、strideは`0x28`、先頭は`battle+0x821C`、上限は`0x48`。よって `record = battle + 0x821C + (*(u32*)(battle+0x8E24) * 0x28)`。
- presentation childをsnapshotへ追加する汎用関数は `FUN_02161604(snapshot, child, slot)` (`0x02161604`)。`0x02161604`で `snapshot + slot*4` のlinked-list headを選び、child `+0x20` をnext linkとして末尾へappendする。その後 `0x02161620`でcount base `snapshot+0x26` を作り、`0x02161624..0x0216162C` で `snapshot[0x26+slot]++`。
- `FUN_02159C68` (`0x02159C68`) は `0x02159C98: MOV r2,#1` -> `0x02159C9C: BL 0x02161604` なので、ここを通るpresentation/status childは **slot=1**。したがってproducer側で増えるcount byteは `snapshot+0x27`。
- `FUN_0215DA50` (`0x0215DA50`) がsource actionの各snapshotを `FUN_02161814` で列挙し、`0x0215DABC: LDRB r0,[r9,#0x27]` -> `0x0215DAC0: CMP r0,#0` でslot1 countを検査する。0なら何も追加しない。
- 非0なら新action record/snapshotをpoolから確保し、`0x0215DB50: MOV r0,#0x3B0` -> `0x0215DB54: STRH r0,[r8]` でinternal action IDを生成する。元snapshotは0x34 bytesコピーされた後、`0x0215DB6C`でoriginal `+0x04=0`、`0x0215DB70`でoriginal `+0x27=0`、`0x0215DB74`でoriginal `+0x1C`をreset、`0x0215DB78`でoriginal `+0x1E=0`。新snapshotは `FUN_021617E8` で0x03B0 recordへattachされる。

seed `0x04176` のdynamic write-watchでも、`snapshot+0x27=1`の真正なproducer PCは `0x0216162C`、LR=`0x02159CA0`（`FUN_02159C68`からslot=1で呼んだ戻り先）だった。`0x02001970`で見えるvalue=1は0x03B0生成時の0x34-byte `memcpy`による複製でproducerではなく、`0x0215DB70`はclear。後続AIはこのPC/LRと上記pool式から再演し、特定ボスのsnapshot絶対アドレスを保存しないこと。

### seed `0x42F3C` で見つかった実装バグ
`battle.dst`, initial position 0, Hero normal attack→enemy:0 の1turnでROMとC++のbattle coreは一致したが、camera RNGだけずれた。

ROM index3は action `1`, actor `0xC2`, target `0x00`。`0x021DC41C` のlive値は `distance=24141`, `radiusA+radiusB=11468`, threshold=`5734` なのでoverlapはfalse。route `>4` でもない。一方 `021626CC -> 02161814(previous,0)` は直前index2のactor `0x00` を返し、現在targetも `0x00` なので条件2だけがtrue。ROMは `LR=0x021DC5D8`, `param5=1` でfree-cameraへ入る。

修正前C++は `.currentActorId = runtimeActorId` として `target == current actor` を比較していたため、このindex3を `param5=0` と誤判定していた。fast/reference runtimeの入力からこの曖昧なfieldを削除し、`turnActionIndex>0 && previousAction exists && currentTarget==previousAction.actorId` を直接判定する。

## presentation child 451 -> internal action 944 の算出・再演手順
この経路はゲルニック専用ではない。ROMのpresentation child slotとsynthetic action-record生成器をそのまま再現する。

### 固定action metadataはROMから生成する
`C:\Users\owner\Documents\tunnelworkspace\BattleArrow\build_freecam_action_metadata.mjs` はROMの `data/prm/actdata_a.gp2` / `data/prm/actdata_b.gp2` を読み、`freecam-action-metadata.bin` を生成する。451/944もこの通常経路から取得すること。2026-09-01のJP ROMでの再生成結果:
- DQ9 action 451 (`0x01C3`): `fallbackLookupActionId=451`, `presentationType=2`, `attackFormationMode=3`。
- DQ9 action 944 (`0x03B0`): `fallbackLookupActionId=944`, `presentationType=0`, `attackFormationMode=0`。
その後 `BattleEmulator/camera/build_freecam_fast_generated.mjs` を実行し、`freecam_fast_generated.hpp` 等へconstexpr化する。C++本体へpresentationType/formation値を直書きしない。

451/944というaction ID自体はboss tableではなくoverlay engine codeの即値なので、アドレスを根拠にする:
- `0x0215A4F8`: `FUN_02159D0C` のMagic-Mirror recovery成功経路が `0x01C3` をロードし、`FUN_02159C68`へ渡す。
- `0x0215DB54`: `FUN_0215DA50` が新しい0x28-byte action recordへ `0x03B0` をstoreする。

### child slot 1 count の生成
`FUN_02159C68` はpresentation child recordを作り、`FUN_02161604(actorSnapshot, childRecord, 1)` を呼ぶ。`FUN_02161604 @ 0x02161604` のcount更新は:
- `0x02161620`: count base = `actorSnapshot + 0x26`。
- `0x02161624`: `actorSnapshot[0x26 + slot]`を読む。
- `0x02161628`: +1。
- `0x0216162C`: 同じbyteへstore。
slot=1なので、944生成器が読む `actorSnapshot+0x27` は **presentation child slot-1 count**。live write-watchでもproducerは `PC=0x0216162C`, `LR=0x02159CA0`, value=1。`0x02001970`で見える1は後述944生成時のsnapshot memcpy、`0x0215DB70`はsource count clearなのでproducerと誤認しない。

Magic Mirror recoveryについては、`FUN_02159D0C` の `combat+0x84` branchだけが今回観測したchild 451を生成した。`FUN_0215AFF4` はcombat `+0x14 & 0x200`を確認し、成功時 `FUN_02089210` がbit 0x200、`+0x61`、`+0x84`をclearする。BattleEmulatorの対応は `hasMagicMirror`, `MagicMirrorTurn`, `MagicMirrorRecoveryTurn`。したがってruntimeでは「しっぷうづき後」などaction名で944を決めず、**そのreal action snapshotへslot1 childが実際に追加されたか**を保持する。

### 944 record poolと生成条件
`FUN_0215DA50` はsource 0x28-byte action recordのactor snapshotsを走査し、各snapshotについて `snapshot+0x27 != 0` をboolean gateとして1件だけ944 self-actionを作る。count値ぶん944を複数作るわけではない。生成後source snapshotの `+0x27` は `0x0215DB70` で0へ戻る。

別boss/別Stateでpool addressを再計算する場合:
- actor snapshot allocator `FUN_02160098`: `battle + 0x20 + battle[0x8E00] * 0x34`, 上限0x48件。
- action record allocator `FUN_02160158`: `battle + 0x821C + battle[0x8E24] * 0x28`, 上限0x48件。
- action record -> actor snapshot resolve: `FUN_02161814`。
- snapshotをaction recordへattach: `FUN_021617E8`。
絶対RAMアドレスをfixtureからコピーしない。

`FUN_0215DA50` の944生成では、new snapshotを確保してsource snapshotを0x34 bytesコピーし、source側では `+0x04=0`, `+0x27=0`, `+0x1C=0xFFFF`, `+0x1E=0` をclearする。new action recordは944、actor/targetはselfになる。実presentation処理後にはactorのtransient goal/aux/targetが無効化されることをlive route probeでも確認している。

### C++ runtimeでreal action indexとpresentation record indexを混同しない
BattleEmulatorの `actions[]` はbattle実処理のreal action列で、route setupのreal indexとして使う。一方ROM `controller+0x57C8` は944を含む0x28-byte presentation action-record列を数える。944を`actions[]`へ物理挿入してreal indexをずらしてはいけない。

`freecam_fast_runtime.hpp`では:
- `previousActionIndex`: real BattleEmulator actionの連続性確認専用。
- `presentationActionRecordIndex`: synthetic 944も含むROM presentation record index。
- `previousAction`: `FUN_021626CC(controller)`が返す直前presentation record相当。944をappendしたら `{dq9ActionId=944, actor=self, target=self}`へ更新する。
これにより次のreal actionの `021DC394..021DC3C0` previous-actor条件、first/later `param5`判定がROMと同じ履歴を見る。

### seed 0x04176 Turn6での確認fixture
固定列: Mirror Shield -> 一閃づき(Geruniku) -> ためる -> ザキ(Tekkomajin A, 成功) -> さみだれづき -> しっぷうづき(Geruniku)。BattleEmulator internal IDsは最後が `MERCURIAL_THRUST=44`。**DQ9 action 69と内部69(けものづき)を混同しない**。

Turn6ではHeroのMagic-Mirror recovery成功によりslot1 child 451が1件付き、その直後synthetic 944が生成される。944自身のfixed metadata/freecam判定もROM-mined constexpr経路を通し、このfixtureでは`callFreeCamera=false`。
期待camera RNGは:
`#488 0216F0E4 -> #489 0216FE40 -> #490 0216FE40 -> #491 0216FE68 -> #492 0216FFF8`。
ROM consumed=492に対しC++ next position=493、HPは `Hero=148, A=0, Geruniku=1458, B=364`。synthetic debug eventは `dq9=944, synthetic=1, slot1Count=1, slot1Child=451, call=0`、直後のGeruniku action10は`param5=0`。final positionだけでなくこのLR順・damage/HP・synthetic eventを同時に確認する。

## SKY_ATTACK 0ダメージ -> MERA_ZOMA の歴史的camera補正
BattleEmulator内部 `SKY_ATTACK` はDQ9 action `540 (0x021C)`、`MERA_ZOMA` はDQ9 action `11 (0x000B)`。

現在C++の `TiggerSkyAttack` は、物理攻撃処理の `kaihi` / `tate` 分岐ではなく通常damage計算側で `baseDamage==0` になった `SKY_ATTACK` のときだけ立つ。つまり「みかわし/盾ガードで0」ではなく、攻防計算・倍率適用後も0だったケースだけを区別している。turn末尾でこの値を `camera::Main(..., bakuti)` へ渡し、現行互換コードはそのSKY_ATTACKを見た後のMERA_ZOMAで `onFreeCameraMove(..., param5=1)` を強制する。

このrare pathを `021E03AC` と同一視しない。`021E03AC` は現在/直前ともaction ID 1の連続通常攻撃専用reset判定で、MERA_ZOMAでは成立しないことがdecompileで確定している。

静的に再現可能な位置情報は既にある。presentation nodeのworld X/Zは `overlay_d_00:02170F40..02170F54` のfloat定数から `PresentationNodeWorldPosition()` がconstexpr算出し、route commit時にactor stateの `worldX/worldZ` へ反映する。selector overlapが必要ならROM側の比較は上記 `0x021DC3F8..0x021DC424`。不足しているのはactor object `+0x64` radiusのROM静的供給元と、**0ダメージSKY_ATTACKのpresentation completionが通常hit/missとどの地点で分岐するか**の確定だけ。

したがって、該当Stateを直接再現できるまでは `moture/TiggerSkyAttack` 互換分岐を消して推測のroute補正へ置き換えない。Stateが得られたら、SKY_ATTACK終了直前/次MERA_ZOMAの `021E0F48` route write、actor `+0x44` Vector3、`+0x64` radius、`021DC394..021DC424` の3例外を同時captureし、通常runtimeだけで同じ `param5=1` が出た時点で互換分岐を削除する。

## Player profileの `2 / 6` をマジックナンバーにしないための算出根拠
主人公のfree-camera actor membershipは `mp0206` を直接指定しない。`body item ID + primary weapon item ID -> ROM visual model code -> mp%02d%02d -> actor membership profile` の順で解決する。

Ghidra上の根拠は `FUN_02073E2C` (`0x02073E2C`)。この関数は `getBattleActorPlayerData` (`0x02054FE4`) で得たplayerDataについて、次の2つのpresentation recordを読む。
- `0x02073E6C`: `playerData + 0x294` を第2model（武器側）record slotとして保持。
- `0x02073E70` / `0x02073E98`: `playerData + 0x194` を第1model（body側）record slotとして保持。
- `0x02073E9C` -> `0x02073EA4`: 第1record pointerを辿り `[record+4]` を読む。
- `0x02073EAC` (`LSL #12`) -> `0x02073EB4` (`LSR #24`): `([record+4] >> 12) & 0xff` を第1model codeにする。
- `0x02073EC0` -> `0x02073EC4`: 第2record pointerを辿り `[record+4]` を読む。
- `0x02073EC8` (`LSL #12`) -> `0x02073ECC` (`LSR #24`): 同じくbits 12..19を第2model codeにする。
- `0x02073ED0` は `0x02073EE4` にあるpointerを読み、その先 `0x020F0DEC` のformat stringは `"mp%02d%02d"`。したがって第1=2、第2=6なら `mp0206` になる。

現在の `battle.dst` 主人公については、`FirstPartyMemberPlayerData = 0x020F52B0`。`getBattleActorEquipmentState` (`0x020541E4`) はbattle actorの `+0x144` playerData pointerへ `+0x488` した装備stateを返す。現在の実RAMでは:
- equipment state base = `0x020F52B0 + 0x488 = 0x020F5738`。
- `0x020F5738 + 0x00` のu16 = `0x3382`。現在scenarioでbody item IDとして渡している値。
- `0x020F5738 + 0x0E` (`equipmentState[7]`) のu16 = `0x5021`。現在scenarioでprimary weapon item IDとして渡している値。
- `playerData+0x194 = 0x020F5444` は `0x020F5908` を指し、`0x020F590C = 0x180027F3`。`(0x180027F3 >> 12) & 0xff = 2`。
- `playerData+0x294 = 0x020F5544` は `0x020F5A08` を指し、`0x020F5A0C = 0x18006000`。`(0x18006000 >> 12) & 0xff = 6`。
このRAMアドレスは現在fixtureの具体例であり、一般実装では固定RAM pointerを使わずitem IDからROM静的表を引く。

静的生成では `data/prm/itemdata_bod3.cn` と `data/prm/itemdata_wea3.cn` を展開する。両方とも、展開後 `u16[0]=count`、primary table開始 `0x0C`、stride `0x20`。primary recordの `+0x18` がstable item ID、`+0x00` がvisual indexで、`visualBase = 0x0C + count * 0x20`、`visual = visualBase + visualIndex * 0x20`、最終的なmodel codeは `u32(visual+0x04)` のbits 12..19である。

現在fixtureを静的表から再計算すると:
- body `0x3382`: `count=183`, `visualBase=0x16EC`, primary index `67` (`0x086C`), visual index `20`, visual offset `0x196C`, `u32(+4)=0x180027F3` -> model `2`。
- weapon `0x5021`: `count=268`, `visualBase=0x218C`, primary index `173` (`0x15AC`), visual index `43`, visual offset `0x26EC`, `u32(+4)=0x18006000` -> model `6`。

`build_freecam_membership_metadata.mjs` はこの関係をv3のsorted item->model tableへ生成し、C++は `ResolveBodyModelCode(itemId)` / `ResolveWeaponModelCode(itemId)` のconstexpr binary searchから `ResolvePlayerProfileFromEquipment(bodyItemId, primaryWeaponItemId)` へ進む。`2`、`6`、`mp0206`、generated profile indexをBattleEmulator本体へ直接書かないこと。装備が変わればitem IDだけscenario stateで変え、同じ算出経路を通す。

## ミラーシールド137とマホカンタ55を同じpresentation actionにしない
DQ9 action IDは別物。
- `137 (0x0089)` = ミラーシールド。実ROM skill menuでも `skill:137` として取得される。
- `55 (0x0037)` = マホカンタ。ゲルニックAI側のspell action。

BattleEmulatorでは状態効果が同じため、長く内部 `MAGIC_MIRROR=31` を主人公とゲルニック双方で共用していた。このためcamera mapperまで `31 -> DQ9 55` に固定され、主人公ミラーシールドのpresentation identityが誤っていた。

修正後は主人公側 `MAGIC_MIRROR=31 -> DQ9 137`、ゲルニック側は別common ID `GERUNIKKU_MAGIC_MIRROR -> DQ9 55` とする。状態効果処理（MP、`hasMagicMirror`, `MagicMirrorTurn`）は同じswitch bodyを共用してよいが、DQ9 action identityは共用しない。

位置については、X/Zそのものは既に静的算出可能です。02170F40..02170F54の定数→PresentationNodeWorldPosition()→route commitでworldX/worldZまで入っています。overlap判定もROMでは、

## Fast runtime接続順
一般的なturn処理:
1. battle開始時だけ`ResetBattle()`。
2. roster全actorを`SetPresentationActor()`。actor IDは`Dq9ActorId()`。world座標からnodeを求める。
3. membership profileが分かるactorはplayerなら`SetPlayerMembershipProfileFromEquipment`、monster/specialなら`SetMonsterMembershipProfile` / `SetSpecialActorMembershipProfile`を設定。
4. turn開始時、action順の`BattleActorRef`を`BeginTurn()`へ渡す。
5. `BeginPresentationGoalSetup()`。
6. 各actionについてmapper bindingからROM `attackFormationMode`を取り、`AssignActorPresentationGoal(actor,target,actionActorIds,mode)`。
7. actionごとに`PlanCurrentActionRoutes(actionIndex)`。
8. `binding->decide(ActionRuntimeInput)`。ここでmembership→action BACT→fallback membership、routeCount selector suppression、param5 exceptionを決める。
9. `decision.callFreeCamera`の時だけ既存`camera::onFreeCameraMove()`を呼ぶ。`param5`はdecisionの値を使う。
10. action progressをcommit。
11. action routeとfallback routeをpresentation stateへcommitして次action/次turnへ引き継ぐ。
全actionを一度にruntimeへ置換せず、既存手動Ruleで検証済みのactionは回帰試験しながら接続する。ZAKIはroute suppressionが必須なのでruntime判定を優先する。

`021E08BC`のcurrent/future suffix loopについてlive overlayで確認済みの規則:
- current action indexからturn末尾まで走査する。
- actorはsuffix内の最初のactionで一度だけ処理し、movement eligibilityより前にvisited扱いになる。
- current actorはcurrent targetを使う。
- future actorでrow+4 nonzeroなら`021E2664` fallback。
- future actorでrow+4 zeroならそのfuture action recordから`021E2818`でtargetを解決する。
- future actorのgoal計算`021E1FD8`へ渡すattack/action contextはfuture actionではなく**current action record**。したがってformation modeもcurrent actionのROM値を共有する。

## Selector/UI拡張のXY問題を避ける
技固有selectorを実ROMで観測するためにセーブデータやbattle MCPを拡張してよいが、UI操作支援をBattleEmulator本体実装へ混ぜない。
- `lv99.dst`は観測fixtureであり、そこで覚えている技を網羅的にBattleEmulatorへ実装しない。
- battle MCPの1人→最大4人対応は、必要な観測を成立させる最小の一般化なら行ってよい。
- `さくせん > そうびがえ > ... > 武器選択`も、対象技を実測するために必要になった場合だけharness側へ一般操作として追加する。
- 「技が要求する武器種を返すAPI」を先に作らない。ROM selector解明に本当に必要と判明してから、既存ROM dataから採掘する。
- セレクタ調査の目的はcamera/RNG branchの解決であり、技カタログや装備システムの再実装ではない。
## 追尾camera
`free_camera_with_tracking_fallback`は以前のAIが実機検証した既存挙動を保護する。freecam不成立時に追尾が発生する可能性があるからといって、全actionをtrackingへ変更しない。逆に「freecamがなかったから常にtracking」とも決めない。actionごとに実機で検証する。
候補0のactionでは重いcamera処理を通さない設計を維持する。ROM bin/compile-time metadataでBACT/membership候補0をcheapに判定し、既知switch/ifをhot pathで使ってよい。
## 実機probeの原則
- DeSmuMEの意図しないpauseは調査中断理由にしない。resumeまたはstate再読込で続ける。
- persistent scriptを使う。Ctable_jp、battle_damage_trace、battle_turn_audit、battle_command_mcpはbattle本体調査で維持する。
- stateがA連打前提なら、battle MCPで勝手に複数turnへ進めず、指定どおりA連打またはUI確認後の1turn再現を行う。
- async化されたmacro/toolは同じ操作を再送しない。async IDを回収する。
- freecamがrareなら1回不発で「実装不要」としない。人間観測済みの発生条件をstate化してbranchを直接再現する。
## Action本体との切り分け
camera実装前にaction本体終了positionをC-tableで取る。damage/状態/action本体positionがずれている場合はcameraを触らない。action本体一致・turn-endだけずれる時にpresentation/freecamを追う。
Block/Evadeなどdamage0でもselector/action固有RNGが発生する場合があるため、damage0を理由にpresentation以前の処理を短絡しない。一閃づきではBlock/Evadeでfinal damage=0でもselector45成功時のfloatと成功側追加RNGが実機で発生することを確認済み。
## Validation checklist
変更後は最低限:
- generated metadataをROMから再生成できる。
- `freecam_fast_validation.cpp` / `freecam_validation.cpp`の既存fixtureを壊さない。
- ZAKI `zaki.dst` route [59,50]→freecamを再現。
- ZAKI `zaki3.dst` route [66,67]→freecamを再現。
- ZAKI routeCount=0時はselector suppressionでfreecamを呼ばない。
- `onFreeCameraMove()`の既存RNG消費を変更していない。
- 通常攻撃、しっぷうづき、けものづき、きゅうしょづき、一閃づきの既知seedを回帰。
- freecam/trackingを含むturn-end positionを複数seedで実機比較。
- 最後に10ターン連続でaction、damage、状態、RNG positionを実機と比較。
## よくある誤り
- ZAKI/ZARAKIを混同する。ザキは単体、ザラキは範囲/group。
- ザラキに対象矢印がないことをマホカンタのせいにする。
- `p1=0`だけを見てfreecamを主人公へ帰属する。
- 0216FDA4 entry時のcamera+0x262を現在actorだと思う。
- `route=[59,50]`だから次turn node50、と決める。
- target scopeからattackFormationModeを推測する。raw nibbleをROMから抽出する。
- ゲルニック戦のC0/C1/C2や4actor固定をcamera.cppへ書く。
- generated runtimeを接続するために既存`onFreeCameraMove()`やtracking resetの意味まで変更する。
- 「全部mapperへ追加」「全部runtime lookup」に走る。mapperはDQ9 IDが確認済みのcommon actionだけcompile-time bindingし、hot pathはcheapに保つ。
