# DQ9 BattleEmulator 完全再現ハンドオフ
## 目的
- ゲルニック将軍戦を起点に、DQ9実ROMと乱数消費およびダメージを完全一致させる高速 `BattleEmulator` を実装する。
## 現在の重要事項
- `BattleEmulator.cpp` 全2202行を確認済み。
- 現在の `Main()` は4actor速度sortまでは実装されているがactor switch本体が空で、実戦ループは未完成。
- `BattleEmulator` は独立Git repo。現在branchは `gerunikku1_new_arugo`。作業開始時点でユーザー既存変更は `CMakeLists.txt` と `camera.h`。
- 古いcommit `244c395306eaca0b2b3dadbfb393aabc62bf1442` には、空switch化前の1対1実行loopが残っている。状態異常、`callAttackFun()`、HP反映、行動後処理の再利用元にする。
- 4actor初期値は `main.cpp` の `copiedPlayers[4]`: 主人公、鉄甲魔人C0、ゲルニックC1、鉄甲魔人C2。
- `Player` は4actor直結用に `magicResistanceLevel`, `confused`, `confusionTurns`, `guardedBy` を末尾へ追加済み。既存aggregate初期化の位置は変えていない。
- ゲルニック固有common IDとして `GERUNIKKU_MERAMI`, `GERUNIKKU_BAGIMA`, `EERIE_LIGHT`, `GERUNIKKU_MEDAPANI`, `GERUNIKKU_BAGIMA_STRONG` を追加済み。マホカンタは既存 `MAGIC_MIRROR` を使用する。
- 既存optimizer/request codeは2体しかコピーしておらず、現状 `Main()` の `players[2]/[3]` は範囲外。ここは実装対象。
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
## ダメージ/状態で確定済み
- `00AF`: incomingを1.5倍、追加RNGなし、最終damageの25%を反動。
- `006D`: generic physical後に防御低下 `RandInt(100)`。
- `009B`: magic resistance stageを-1、下限-2。magic damage multiplierは `1 - 0.25*stage`。
- `000A`: `62 + rand[-10,+10)`。
- `0013`: `29 + rand[-15,+15)`。
- `01CF`: `44 + rand[-21,+21)`。
- `0037`: magic reflection状態。
- `0390`: confusion状態。
- `03A1`: ゲルニックを保護するactor-specific guard。
## 次の実装順序
1. `Genome`/optimizer/requestを4actor状態保持へ修正し、A* hashへ鉄甲魔人状態を追加する。
2. `Player[4]` を戦闘状態の唯一の実体に統一し、`callAttackFun()` を4体配列へ直接適用する。ホットループ内の2体コピーは禁止。
3. 空actor switchへ旧loopの主人公処理を移植する。
4. 鉄甲魔人turn-start AIをslot単位で保持し、limited-slot、MP6スクルト、かばう重複不可を実装する。
5. ゲルニックacting-time AIを実装し、1回目実行後に2回目を再選択する。
6. メラミ/2種類のバギマ、ぶきみ、マホカンタ、メダパニ、かばう、かぶと、もろばのcase/side effectを即時実装する。
7. CLion `gerunikku` targetでビルドし、同seed C-tableと最初の不一致まで比較する。
## 作業ルール
- 調査を貯めて最後にまとめて `BattleEmulator` を変更しない。確定した仕様から逐次実装する。
- 作業完了または実際の制限到達まで継続する。
