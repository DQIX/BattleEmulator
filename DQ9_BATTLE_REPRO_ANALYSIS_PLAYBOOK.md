# DQ9 バトル完全再現解析指示書
## 目的
- `BattleEmulator` で実ROMと同じ乱数位置・行動選択・ダメージ・状態遷移を再現する。
- バトル全体を闇雲にデコンパイルせず、乱数消費、行動選択、ダメージ値、次ターンの乱数列やダメージへ影響する状態だけを解析する。
- 総当たり用途なので、実装はROM実行や巨大lookupではなく、必要な式・分岐・小さい固定表へ落とす。
## 作業規則
- 調査結果を最後まで貯めて一括実装しない。1つの仕様が確定したら、その場で最小差分を `BattleEmulator` へ入れ、ビルド可能性を確認してから次へ進む。
- 「乱数だけ合う」「ダメージだけ合う」は未完了。各行動について乱数消費数と順序、最終ダメージ、次行動へ残る状態を同時に一致させる。
- `BattleEmulator.cpp` の既存ダメージ経路を優先して再利用する。既存式を別実装で複製しない。
- 技固有selectorは `C:\Users\owner\Documents\tunnelworkspace\dq9-skill-catalog\reports\selector-battleemulator-spec.json` を最初に確認する。既にデコンパイル済みなら再発見作業をしない。
- AI選択は `Restricted-behavior\analysis\CURRENT_HANDOFF.md` の `movementPattern` / `isCanActionTaken` 再構成を基準にする。6slot weighted roll、選択slotから下方向→上方向のfallback、limited-slot、judgmentによる選択時期を維持する。
- 実機照合は `Ctable_jp.js` を主な乱数順序の基準にする。補助probeは対応付け確認に使ってよいが、overlay再配置等で偽entryが混ざり得るprobe単独を仕様根拠にしない。
- C-tableに表示されるLRアドレスは「この乱数消費が何か」を後から再照合するための識別子として扱う。新しく実装する乱数消費には、判明している `max` と `lr: 0x........` をコードコメントへ残す。
- RNG返値を使わず消費だけ必要な箇所で `lcg::getPercent()` / `floatRand()` 等を呼ばない。消費数と分岐が確定している場合は `(*position)++` を使う。複数消費も、LRが別なら `(*position)+=N` へ潰さず1個ずつLRコメントを付ける。
- 逆に、AI weighted roll、target group index、成功率、damage幅など返値そのものが後続分岐/値へ必要なRNGは、対応するLCG helperを使って実ROMと同じ演算を行う。「高速化」のために結果を近似しない。
## 解析の順序
1. 対象action IDを確定する。common IDとDQ9内部IDを混同しない。
2. `selector-battleemulator-spec.json` でselector固有演算、追加RNG、side effectを確認する。
3. attack recordからoperation type、cost、target handler、packed damage parametersを確認する。
4. generic damage pathをGhidraで追い、既存 `BattleEmulator.cpp` のどのhelper/caseへ対応するか決める。
5. action実行前後のC-tableを取り、RNG呼び出しの順序と最大値を列挙する。
6. HP/MP/防御段階/魔法耐性/状態異常/反射/かばう等、次のAI可否またはダメージへ影響するside effectを追う。
7. 確定した部分を即実装する。
8. CLionの対象targetでビルドし、同seedの実機と `action + damage + RNG position` を比較する。
9. 不一致が出た最初のRNG呼び出しまたは最初のダメージ演算へ戻り、その局所だけを解析する。
10. 実行時C++側の値/クラッシュ/制御順が静的読解だけで断定できない場合は `battle_harness\SKILL.md` のCLion debugger手順を使う。native backendでlogpoint eventが取れない場合は停止breakpoint + stack/frame/evaluateを使い、修正前に具体的な値を1つ以上取得する。
## ゲルニック戦のactor構成
- `players[0]`: 主人公。
- `players[1]`: 鉄甲魔人 C0。
- `players[2]`: ゲルニック将軍 C1。
- `players[3]`: 鉄甲魔人 C2。
- 鉄甲魔人はjudgment 1なのでターン開始時にactionを選ぶ。
- ゲルニックはjudgment 2なので本人の手番到達時にactionを選ぶ。2回行動は1回目実行後に2回目を再選択する。
- ゲルニック2行動をターン冒頭で先取りしてはいけない。そこまでに主人公/鉄甲魔人の行動が消費したRNGを飛ばすためである。
## 既に確定しているゲルニック戦固有事項
- 鉄甲魔人AI raw actions: `03A1,0001,0001,006D,002A,00AF`、weights `68,58,48,38,27,17`。
- ゲルニックAI raw actions: `0013,000A,009B,0037,0390,01CF`、weights `68,58,48,38,27,17`。
- `0x00AF もろば斬り`: selector 36。generic incoming damageを `trunc(1.5 * incoming)`、selector追加RNGなし。最終ダメージの25%切り捨てを使用者への反動として保持する後段がある。
- `0x006D かぶと割り`: generic physical damage後、防御低下判定で `RandInt(100)` を消費する。
- `0x009B ぶきみなひかり`: 魔法耐性段階を1段低下、下限 `-2`。魔法ダメージ倍率は `1.0 - 0.25 * magicResistanceStage` なので `-1=1.25倍`, `-2=1.5倍`。
- `0x000A メラミ`: enemy packed damageは `trunc(62 + rand[-10,+10))`。
- `0x0013 バギマ`: enemy packed damageは `trunc(29 + rand[-15,+15))`。
- `0x01CF バギマ`: enemy packed damageは `trunc(44 + rand[-21,+21))`。
- `0x0037 マホカンタ`: self reflection stateを付与する。
- `0x0390 メダパニ`: 対象へ混乱状態を付与する。状態は後続の行動可能性/RNGに影響するので省略不可。
- `0x03A1 ゲルニックかばう`: 鉄甲魔人がゲルニックを保護するactor-specific action。主人公からゲルニックへの攻撃target変更へ影響するので省略不可。
## 実装上の再利用方針
- 戦闘状態は常に `Player players[4]` を唯一の実体として扱う。ホットループ内で2体viewを作ってコピー/同期してはいけない。
- `callAttackFun()` と関連helperは同じ4体配列を直接受け、`attacker` / `defender` indexで対象actorを参照する。旧1対1実装の `players[1]` hard-codeは、意味が「現在の敵/対象」である箇所から逐次index参照へ置換する。
- 鉄甲魔人2体のHP/MP/防御段階/死亡状態は別々に保持する。同speciesでもencounter groupが別であり、limited-slot stateも別である。
- A*の状態hashにも鉄甲魔人2体の戦闘状態を含める。同じ主人公/ゲルニックHPでも鉄甲魔人状態が異なる枝を同一状態として潰してはいけない。
- debug/整形用コードも4actor化による1ターン最大敵行動数を前提にする。`BattleResult` のrecord容量だけを増やして表示側の固定バッファを旧2行動のまま残さない。
## Free cameraの解析と実装
- freecamも最終RNG位置に影響するため「見た目だけ」として省略しない。ただしカメラシステム全体をruntimeで再現しない。
- `camera/freecam_fast_runtime.hpp` と生成済みのコンパイル時metadataを使い、任意actionに必要なmembership/routeだけをcompile-timeで焼き込む。ホットループでROMファイルを読む、巨大runtime lookupを走査する、既知技表を線形探索する実装は禁止。
- 今回の重点は通常攻撃、ザキ（対象: 鉄甲魔人）、一閃づき、けものづき等、実戦でfreecam候補になるaction。common IDとDQ9 action IDのbindingはcompile-time固定にする。
- C-tableでfreecam由来の乱数LRを区別し、action本体のdamage/target RNGと混ぜない。cameraの消費がある技/ない技を同seedで比較して追尾する。
## 完了条件
- 同一seedで、少なくとも複数ターンについて実ROMとactor順、敵AI action、主人公action、各damage、各action後HP/MP/主要状態、ターン終了時RNG positionが一致する。
- 既知actionだけをハードコードして未解析actionを黙って通常攻撃扱いしない。未解析経路へ到達した場合は照合失敗として検出できる状態にする。
- ハンドオフには、実装済み、実機照合済み、未照合、未解析を明確に分けて残す。
