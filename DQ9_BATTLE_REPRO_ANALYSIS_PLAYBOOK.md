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
- 実機とC++の差を見つけたら、後続ターンまでまとめて直そうとしない。最後に一致した地点と最初に不一致になった地点を固定し、その間にある最初のRNG、最初のstate write、最初のdamage差のどれか1つへ調査範囲を縮める。
- action本体のRNGと、action終了後に走るpresentation/freecam RNGを混同しない。action本体が一致していてturn-end positionだけずれる場合は、まずcamera/presentation経路を疑う。逆にdamageや状態が先にずれている場合はcameraを触らずaction本体を直す。
- 実ROM側で観測できた値を、推測した意味へすぐ昇格させない。actor ID、状態field、camera counter等は意味が未確定ならraw値として記録し、複数の独立した観測で意味が確定してからコード上の名前・仕様へ反映する。
- 実測で否定された仮説を残したまま別補正を重ねない。仮説由来の差分は最小単位で置き換え、現在の実測だけで説明できる状態を維持する。
## 実機ハーネス運用
- ハーネス再起動後は既存laneが消えていることがある。最初にlane一覧を確認し、無ければ既知のState/Saveから新しいlaneを1本だけ作る。解析中に同目的のlaneを無闇に増やさない。
- State読込直後を再現起点として使う場合はbaselineを保存し、各試行は `baseline restore -> pause -> seed注入 -> trace clear -> command入力 -> resume` の順でfresh状態から始める。前試行の戦闘状態やtraceを次の試行へ持ち越さない。
- persistent scriptは通常の登録経路を優先する。`Ctable_jp.js`、battle command、damage trace、turn audit等は登録後に公開MCP名まで確認し、scriptが「起動したように見える」だけで解析を始めない。
- `battle.dst` を使う比較試行を始める前に、少なくとも `Ctable_jp.js`、`battle_damage_trace.js`、`battle_speed_probe.js`、`battle_turn_audit.js`、`battle_command_mcp.js` をrerunし、consoleをfreshにする。その後いったんresumeしてframeが実際に増えることを確認し、再度 `battle.dst` をloadしてからseed試行を開始する。Stateをloadしただけ、scriptが以前から残っているだけ、UIが見えているだけではclean runとみなさない。
- `Ctable_jp.js` は乱数列の正本、battle damage traceはaction境界とdamage内訳の補助、turn auditはHP/MP等の状態確認、battle command MCPは入力自動化として役割を分ける。補助probeに偽entryが混ざっても、それをC-tableの正しい乱数列より優先しない。
- command入力後にハーネスが元のpaused状態へ戻しただけなら、同じ入力を二重送信しない。UI状態を確認して、既にqueue済みならresumeだけでターンを流す。
- `battle_command_mcp.js` の `confirmOption` は、敵targetが必要なのにtarget指定が無い場合をエラーにしない。正常にenemy-target画面へ遷移し、`status:"ok"`, `targetRequired:true`, `targets:[...]` を返す。そこで返ったIDを `confirmEnemyTarget` へ渡す。command MCPは入力確定をまれに取りこぼすため、重要な比較では返値だけでなく `seeUi`、damage trace先頭action、target actorも確認する。
- 定型操作をmicro macroへまとめられる場合も、macroは複数の通常MCP呼び出しを束ねるだけと考える。各stepの意味・待機・失敗地点を追える粒度を保ち、戦闘ロジックの観測そのものをmacro内部へ隠さない。
## RNG位置の比較規則
- `setSeedFromInitial(initialSeed, position=0)` を使った試行では、実機C-tableのcandidate positionは「既に消費した個数」として読む。C++側の `position` が「次に読むindex」を表す場合、同一地点でも表示上1差になる。まず表現規約を揃えてからoff-by-oneと判定する。
- 自動照合用initial seedは原則14bit以下、すなわち `0x0000..0x3FFF` を使う。大きいinitial seedでは現在seedから逆算した初期seed候補が複数残り `initial seed: unresolved (N candidates)` になり得るため、そのrunのcandidate positionを回帰基準に使わない。47bit以下でも一意性は保証されない。14bit seedで `readSeed` が1 candidateを返したrunを基準にする。
- `Ctable_jp.setSeedFromInitial(initialSeed, position=1)` を使う標準試行では、注入直後のseedはposition 1に対応し、次に消費される乱数がconsole上 `#2` になる。C++の `--trace-turn` / `--trace-battle` は引数 `currentSeedPosition` から内部で `tracePosition = currentSeedPosition + 1` とするため、同じ試行を比較するときは最後のposition引数にも `1` を渡す。ここを `0` にするとC++が最初から1 RNG手前を読み、速度乱数から全列がずれる。
- `--trace-turn` の形式は `--trace-turn <initialSeed> <BattleEmulator内部action ID> <C++ actor target index> <currentSeedPosition>`。第2引数はDQ9 action IDではない。一閃づきは実ROM/DQ9では `0x0048` だが、C++内部IDは `45`。targetもbattle command MCPの `enemy:N` と同じ数値ではなく、ゲルニック戦では `enemy:0=てっこうまじんA -> players[1]`, `enemy:1=ゲルニック将軍 -> players[2]`, `enemy:2=てっこうまじんB -> players[3]`。したがって一閃づきで鉄甲魔人Aを狙い、seed注入位置1から比較する例は `--trace-turn 0x084c 45 1 1`。`target=0` は主人公側indexでありenemy:0の意味ではない。
- 各ターンは可能なら「action本体終了」と「presentation/freecam終了」の2地点でpositionを取る。action本体終了地点が一致し、その後だけ差が増えるならcamera/presentation問題として切り離せる。
- C-tableのLR列は順序が重要である。同じ最終positionでも途中LR順が違えば未再現とみなす。特に成功/失敗branchでは、失敗時に省略されるRNGと成功時だけ追加されるRNGを別々のseedで確認する。
- 乱数差が `+N` だからといって固定で `position += N` を足さない。実ROMログから、そのN個がどのLR、どのactor、どのaction、どのbranchに属するかを確認してから対応するロジックへ置く。
- `readSeed` は演出途中でも呼べるため、取得時刻によってcamera RNGがまだ終わっていないことがある。turn-end比較はbattle UIが次のtop状態へ戻ったこと、または目的の終端イベントを確認してから採る。
## ROM/C++共通action境界checkpoint
- `Ctable_jp.js` は人間が実ROM検証で使っている次の6境界を出す。`0x021ebd9c = start FUN_021ebd9c_ct`, `0x0215f950 = end FUN_021ebd9c_ct`, `0x021594bc = start FUN_021594bc`, `0x0215f980 = end FUN_021594bc`, `0x02158dfc = start FUN_02158dfc`, `0x0215f924 = end FUN_02158dfc`。最終positionだけではなく、この境界ごとに比較する。
- C++側も `--trace-turn` / `--trace-battle` のときだけ同名checkpointを `TRACE boundary <label> position=<N>` として出す。通常の総当たり・optimizer経路では出力しない。`position=N` は「次に読むRNG index」。ROM側のboundary marker直後の最初のC-table乱数が `#N` なら同じ地点である。
- `FUN_02158dfc` はactor pre-action、`FUN_021ebd9c_ct` はaction本体、`FUN_021594bc` はaction後処理の大まかな境界として使える。例えばC++/ROMが `start 02158dfc` までは同じで `end 02158dfc` からずれれば、敵AI実行時選択、魅了判定、pre-action RNG等へ範囲を絞れる。`start 021ebd9c` までは同じで `end 021ebd9c` がずれればaction本体を調べる。`end 021ebd9c` までは同じで `end 021594bc` がずれれば状態持続・post-action処理を調べる。
- 2026-08-30にseed `0x084c`, `setSeedFromInitial(..., position=1)`, 主人公一閃づき `DQ9 0x0048 / C++ 45`, target `てっこうまじんA = enemy:0 / players[1]` でclean runを実施し、戦闘本体の5 actionについてROM/C++境界が完全一致した。順に `13 -> 14 -> 25 -> 26`, `26 -> 30 -> 38 -> 39`, `39 -> 43 -> 52 -> 53`, `53 -> 55 -> 63 -> 64`, `64 -> 66 -> 74 -> 75` で、各4値は `start FUN_02158dfc -> end FUN_02158dfc/start FUN_021ebd9c_ct -> end FUN_021ebd9c_ct/start FUN_021594bc -> end FUN_021594bc` のposition。最終 `readSeed` もinitial seed候補1件でposition `78`、C++ `TRACE position=78` と一致した。
- 上記 `0x084c` の一致は最終positionが偶然一致しただけではない。主人公一閃づき本体からゲルニック2行動、鉄甲魔人2行動まで、pre-action/action/post-actionの境界がすべて一致している。後続エージェントは同様のcheckpoint比較を先に行い、いきなりdamage helperやAIを再解析しない。
- 2026-08-30のseed `0x1ba1` でも戦闘本体のcheckpointは一致した。ROMでは最後の `end FUN_021594bc` 後からcamera/presentation RNGが追加され、turn-end candidate positionがC++本体位置とずれるため、今回のバトル本体デバッグでは `end FUN_021594bc` までの一致をまず判定基準にする。camera側を調査対象にしていない作業で、ここから先の差を理由にbattle actionロジックを変更しない。
- 誤診例: `--trace-turn 0x084c 45 0 0` は二重に誤っている。`currentSeedPosition=0` により速度RNGから1つずれ、`target=0` により `enemy:0` ではなくC++ target override無効/別targetになる。この誤条件ではC++一閃づきが317等になり、ROMの0damageと食い違って見えた。正しい条件 `--trace-turn 0x084c 45 1 1` では一閃づき0damage、戦闘本体checkpoint、最終positionまで一致した。不一致を修正する前にCLI引数の意味を必ず検証する。
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
11. C++と実機でaction列が同じなのにdamageだけ違う場合は、damage式そのものだけでなく、その直前に参照するATK/DEF/buff/defence/stateが既にずれていないか確認する。後段のdamage値から原因を逆算して状態差を見落とさない。
12. C++と実機でaction本体終了positionが同じなのにturn-endだけずれる場合は、C-tableのfreecam enterとcamera系LRをactorごとに列挙し、欠けているpresentation経路を特定する。
## CLionデバッグ運用
- run configurationを実行しただけで必ず再ビルドされたとは限らない。出力に埋め込まれたBuild date/time等が変わっていない場合、修正が効かなかったと判断せず、まず古いbinaryを起動していないか切り分ける。
- 静的コード上は正しく見えるのに実行結果が違う場合、O0 Debug targetで最初の不一致地点へbreakpointを置き、`position`、action ID、attacker/defender、HP/MP、ATK/DEF、buff段階、状態flag、中間damageをframe/evaluateで直接読む。
- breakpoint条件は「対象actionかつ既知のposition以降」のように狭くし、同じ共通damage関数を多数のactionが通る場合でも目的の1回だけ止める。
- debuggerで得たC++側の値と、同seed・同actionのC-table/damage trace実測を並べて比較する。最終damageが違う場合でも、raw damage、参照ATK/DEF、状態倍率、後段補正のどこから差が始まったかを順に分解する。
- debuggerで観測した値が仮説を否定したら、その仮説を維持しない。「実機の閾値が違うはず」等を続けず、C++側が別RNG位置を読んだ、先行state writeがあった、古いbinaryだった等の候補へ切り替える。
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
- compile-time bindingが存在することと、実戦runtimeからその判定が実際に呼ばれていることは別である。`freecam_action_mapper.hpp` にbinding/assertがあるだけで完了扱いにせず、`BattleEmulator` 本体から生成済みmetadataの判定へ到達する経路まで確認する。
- camera差を手書きの「このactionなら+N消費」で埋めない。同じactionでもactor membership、fallback membership、BACT、selector suppression、route、行動順でfreecam可否や消費内容が変わり得るため、生成済みmetadataが持つ判定を利用する。
- freecam enterが複数actorで発生するターンは、actorごとに入口と続くLR列を区切って記録する。最終差分だけを見て、複数actorの消費を1つのactionへ誤帰属しない。
- action本体終了後から次top UIまでに現れる `0x0216FE40`、`0x0216FE68`、`0x0216FFF8`、`0x0216F0E4` 等のcamera/presentation系LRは、本体damage traceとは別系列として監査する。
## 完了条件
- 同一seedで、少なくとも複数ターンについて実ROMとactor順、敵AI action、主人公action、各damage、各action後HP/MP/主要状態、ターン終了時RNG positionが一致する。
- 既知actionだけをハードコードして未解析actionを黙って通常攻撃扱いしない。未解析経路へ到達した場合は照合失敗として検出できる状態にする。
- ハンドオフには、実装済み、実機照合済み、未照合、未解析を明確に分けて残す。
- 「Nターン通った」は最終positionだけではなく、途中各ターンの最初の不一致が無いことを確認して初めて成立する。長い連続試験で最後だけ比較せず、序盤からcheckpointを置く。
- 変更後は、以前一致していたseed/ターンを再試験して回帰が無いことを確認する。新しい技やcamera対応を追加して既存の一致区間を壊した場合は完了扱いにしない。
