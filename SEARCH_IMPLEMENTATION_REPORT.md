# zilyadama_new_arugo 新探索実装結果
## 採用結果
本番探索を新規実装の `ZilyadamaSearch` に置き換えた。採用方式は `critical-preview`（開発用variant番号1）、最大beam幅16384、探索予算1500ms。旧 `ActionOptimizer::RunAlgorithm` は明示的な比較用CLIだけに残し、`SearchRequest` とWASMの本番出力経路からは撤去した。
最終目的値はfresh replay後の `BattleResult.position`。敵HPが正確に0になった候補だけを採用する。RNG cursor、turn数、削ったHPを最終目的値に置き換えていない。
## 対象と実Player値
| 対象 | compile definition | 味方HP | 攻撃 | 防御 | MP | 敵HP |
|---|---|---:|---:|---:|---:|---:|
| zilyadama_v6_tamahane | lv16_sp22_tamahagane_atk123_def86=1 | 93 | 119 | 89 | 33 | 796 |
| zilyadama_v6_hagane | lv16_sp22_hagane_atk106=1 | 93 | 102 | 89 | 33 | 796 |
両方とも `MULTITHREADING=1` を定義した。実際の探索は共有LCGキャッシュを競合させない単一スレッド実装で、既存の `numThreads` 引数と本番APIの形は維持する。マクロ名の123/106をプレイヤー状態へ上書きしていない。
## 独立評価
方式と最大幅を選定した後に、新たに6seedを生成した。各seedに対して指定2variantと、prefixなし・`25`・`25,26,25` の3条件を実際の `--search` → `SearchRequest` 経路で実行した。`25=ATTACK_ALLY`、`26=HEAL`。合計36条件。
評価seedは `0x6c601664`, `0x261960d0`, `0xeaf69c10`, `0x0df54faf`, `0x85d335c0`, `0x1dbe8922`。これらを探索コードの分岐・答え・lookupへ使用していない。
| 項目 | 新探索 | 旧探索 |
|---|---:|---:|
| exact replayで敵HP=0 | 36/36 | 36/36 |
| 1500ms以内 | 36/36 | 33/36 |
| 最小時間 | 346.573ms | 313.532ms |
| 平均時間 | 630.262ms | 633.492ms |
| 最大時間 | 1084.852ms | 3346.336ms |
新探索のpositionは、旧探索に対して18条件で改善、18条件で同値、悪化0条件。最大改善幅は8。新探索のreplay不一致は0、deadline到達は0、固定prefixの出力不一致は0だった。
旧探索の比較は元の `maxGenerations=20000` 呼び出しを使用した。旧実装は内部でgeneration枠を拡大するため、1500msを超えて返った3条件も上表に含め、時間超過を区別している。新探索が全条件で旧探索より速いという意味ではない。
| variant / prefix | 件数 | 旧position平均 | 新position平均 | 改善 / 同値 / 悪化 | 新時間平均 |
|---|---:|---:|---:|---|---:|
| tamahane / なし | 6 | 36.000 | 35.500 | 3 / 3 / 0 | 616.906ms |
| tamahane / ATTACK | 6 | 36.833 | 36.167 | 3 / 3 / 0 | 560.129ms |
| tamahane / ATTACK,HEAL,ATTACK | 6 | 41.500 | 40.833 | 2 / 4 / 0 | 493.097ms |
| hagane / なし | 6 | 44.500 | 42.167 | 3 / 3 / 0 | 746.331ms |
| hagane / ATTACK | 6 | 44.833 | 43.000 | 5 / 1 / 0 | 743.311ms |
| hagane / ATTACK,HEAL,ATTACK | 6 | 49.667 | 49.167 | 2 / 4 / 0 | 621.802ms |
### 代表例
| seed | variant | 固定prefix | 旧position → 新position | 新時間 |
|---|---|---|---|---:|
| 0x261960d0 | hagane | なし | 49 → 41 | 960.138ms |
| 0x6c601664 | hagane | ATTACK | 40 → 37 | 1084.852ms |
| 0x85d335c0 | tamahane | ATTACK,HEAL,ATTACK | 44 → 41 | 445.050ms |
| 0x261960d0 | hagane | ATTACK,HEAL,ATTACK | 53 → 51 | 697.634ms |
これらは実測した入力についての改善であり、全入力での数学的最適性の証明ではない。
## 新アルゴリズム
各turnで旧探索由来の合法行動をすべて生成し、正規エミュレータを1turn実行する。状態は両Playerの全フィールド、nowState、RNG cursor、正確なイベント数を保持する。行動列全体を各nodeへコピーせず、親リンクで保持する。
重複除去はハッシュだけで決めず、衝突後に全PlayerフィールドとnowState・RNG cursor・イベント数を比較する。異なる状態を同一ハッシュだけで捨てない。
主順位は敵HPの減少。各候補の次turnの通常攻撃を同じエミュレータで試行し、実際に得られる追加ダメージを順位へ反映する。先読みは順位付けに使うだけで、元の候補のHP・RNG・statusを変更しない。先読み中に撃破を見つけた場合も、元のinitial worldから全行動列をfresh replayして採用を判定する。
会心率、ダメージ、敵AIを変更していない。会心重視という助言は先読み方式の仮説として比較した。アクロバットスターは合法候補のまま保持し、最終評価の採用列にも実際に含まれている。特定の戦術を入力仕様にしていない。
beam幅は64→256→1024→4096→16384の固定順序。同値時は決定的な順序で比較する。壁時計のdeadlineは別に持ち、返却処理等のため75msを予約する。deadlineに達した場合も、既にexact replayで確認した勝利解だけを返す。
## 合法行動とprefix
旧 `ActionOptimizer::RunAlgorithm` の `possibleActions` を直接確認して、以下を継承した。
| 行動 | 条件 |
|---|---|
| ATTACK_ALLY, DRAGON_SLASH, DEFENCE, FLEE_ALLY | 常時 |
| SPECIAL_MEDICINE | SpecialMedicineCount >= 1 |
| HEAL | MP >= 2 |
| CRACK_ALLY, WOOSH_ALLY | MP >= 3 |
| CRACKLE | MP >= 8 |
| ACROBATIC_STAR | specialCharge && specialChargeTurn != 0 |
FLEE自体は残し、既知pre-action skipを利用しないよう、睡眠・麻痺中のFLEE枝だけを探索側で除外する。今回の36条件ではこの除外は0回だった。FLEEを含めBattleEmulator本体は変更していない。
prefixは元のinitial worldからそのまま正規replayし、終了状態からsuffixだけを探索する。prefixの削除、短縮、別行動への置換、過去のRNGの再探索はしない。各最良候補の全行動列を再度fresh replayし、探索状態との全フィールド一致と敵HP=0を確認する。
このbranchの `BattleEmulator::Main` は `Gene` を絶対turnで索引する。そのため1turn遷移でも350要素のscratchの該当turnへ行動を置く。他branchの「suffix先頭を渡す」呼び方は流用していない。
## DEBUG3・CLI・WASM接続
既存DEBUG3の `seed=0x81a66014`, `prefix=ATTACK_ALLY` は変更していない。DEBUG3も本番 `SearchRequest` を直接呼ぶ。失敗時の終了コードも成功扱いにしない。
両variantについて、DEBUG3の引数なし実行・DEBUG3 buildの `--search 0x81a66014 25`・通常Release buildの同CLIを実行した。時間統計行と空行だけを除外し、turn table・行動列・SearchResultが完全一致した。
| variant | 敵最終HP | position | turn数 | 比較用出力SHA-256 |
|---|---:|---:|---:|---|
| tamahane | 0 | 36 | 15 | 080854bdf3e0c42cb1506dfab30c2d147c9af36a3d26464234efceb78c1a4f62 |
| hagane | 0 | 41 | 17 | 8aeb3db33dc718cbb3828aef5ef58f76b3fbaf5bff30ab8d6432debfb23b1eed |
WASMの既存 `wasm_search_dump` → `buildDumpOutput` も、nativeと同じ `productionSearchOutput` へ接続した。別の旧探索呼び出し、fallback、export追加はない。`webassembly/build.sh` のsource一覧に新cppを追加し、複数のBattleResult作業領域に対応してstackを1MiBへ明示した。
実行検証環境はLinux / GCC 14.2.0 / CMake Release、指定2variantのみ。DEBUG3もRelease最適化にDEBUG3 defineを加えてビルドした。MinGW/MSVCでのビルドと、実EmscriptenでのWASMビルド・実行は未検証。この環境にemccはなく、WASMについて確認済みなのは経路のコード接続とビルドscriptの構文までである。
## 出力と実行方法
既存dumpTableの見た目と戦闘ログの値は維持した。行内ehpは既存のログ由来の値なので、真の最終HPと `BattleResult.position` は追加した `SearchResult` 行で明示する。未勝利stateを成功tableとして返さない。
```sh
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build --target zilyadama_v6_tamahane zilyadama_v6_hagane -j 4
./build/zilyadama_v6_tamahane --search 0x81a66014 25
./build/zilyadama_v6_hagane --search 0x81a66014 -
./build/zilyadama_v6_hagane --search 0x261960d0 25,26,25
```
`--search` は常に本番の採用方式だけを使う。Windowsでは生成先と `.exe` に合わせてパスを読み替える。
開発比較専用CLIは `--search-benchmark SEED PREFIX_CSV|- [VARIANT [MS [WIDTH]]]` と `--legacy-benchmark SEED PREFIX_CSV|- [GENERATIONS]`。seedを固定した答えは実装していない。旧探索は後者と、従来から存在する旧探索専用の最適化コードにのみ残る。
## 比較した方式と停止理由
先読みなし、通常攻撃先読み、HP・資源重視、アクロバット優遇、二種類の順位の混合、強い先読み、2turn単位のbeam、2turn単位＋先読みを比較した。開発用6seedは `0xced11b65`, `0x07e239cf`, `0xabdda4df`, `0x1b67f7d6`, `0x58a63ce7`, `0xd964f0a9`。
通常攻撃先読みは先読みなしに比べてpositionを縮める条件があり、最大幅4096→16384でも追加改善があった。一方、アクロバット優遇と2turn方式には安定した追加改善がなく、32768への拡張は試した後半3seedの両variant・prefixありなしで追加改善なし、deadline到達を増やした。採用はvariant1・16384に固定し、他方式は明示的な開発比較からのみ選べる状態で残した。
## 変更ファイル
`ZilyadamaSearch.h` と `ZilyadamaSearch.cpp` を新規追加。既存ファイルの変更は `main.cpp`、`CMakeLists.txt`、`webassembly/build.sh` の3つ。
`BattleEmulator.cpp/.h`, `BattleResult.h`, `Player.h`, `lcg.cpp/.h`, `camera.cpp/.h`, `ActionOptimizer.cpp/.h` は添付ZIPからバイト単位で不変。既存の旧heuristicとhashも変更していない。新しいテストソース、テストスイート、fixture、CI testは作成していない。サニタイザ、メモリ整合性検査は実行していない。
実測の完全なdumpTable・行動列・統計は `search_results/` に同梱する。`evaluation_summary.txt` が独立36条件の集計、`eval_*_new.txt` と `eval_*_old.txt` が対応する実測ログである。