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
- generated ROM metadataはcompile-timeで使う。hot loopでROM/巨大binを開かない。既知actionのcommon-ID→DQ9-IDは`freecam_action_mapper.hpp`のcompile-time bindingを使い、線形探索tableを作らない。
- 画像が必要なら `C:\Users\owner\Documents\desmume_webassembly_harness\harness\screenshots` をisolationへ追加して画像ツールで読む。許可root外だったことを理由にUI確認を諦めない。
## 主要ソース
- `camera.cpp`, `camera.h`: BattleEmulatorとのadapterと既存RNG消費。
- `camera/freecam_actor.hpp`: BattleActorRefとDQ9 actor ID変換。人数に依存させない。
- `camera/freecam_action_mapper.hpp`: BattleEmulator common action ID→ROM DQ9 action IDのcompile-time binding。
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
`build_freecam_action_metadata.mjs`はFCMA v2として、1024件のfallback lookup u16と1024件のformation mode u8を出力する。現在の確認値:
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
## Fast runtime接続順
一般的なturn処理:
1. battle開始時だけ`ResetBattle()`。
2. roster全actorを`SetPresentationActor()`。actor IDは`Dq9ActorId()`。world座標からnodeを求める。
3. membership profileが分かるactorは`SetPlayerMembershipProfile` / `SetMonsterMembershipProfile` / `SetSpecialActorMembershipProfile`を設定。
4. turn開始時、action順の`BattleActorRef`を`BeginTurn()`へ渡す。
5. `BeginPresentationGoalSetup()`。
6. 各actionについてmapper bindingからROM `attackFormationMode`を取り、`AssignActorPresentationGoal(actor,target,actionActorIds,mode)`。
7. actionごとに`PlanCurrentActionRoutes(actionIndex)`。
8. `binding->decide(ActionRuntimeInput)`。ここでmembership→action BACT→fallback membership、routeCount selector suppression、param5 exceptionを決める。
9. `decision.callFreeCamera`の時だけ既存`camera::onFreeCameraMove()`を呼ぶ。`param5`はdecisionの値を使う。
10. action progressをcommit。
11. action routeとfallback routeをpresentation stateへcommitして次action/次turnへ引き継ぐ。
全actionを一度にruntimeへ置換せず、既存手動Ruleで検証済みのactionは回帰試験しながら接続する。ZAKIはroute suppressionが必須なのでruntime判定を優先する。
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
