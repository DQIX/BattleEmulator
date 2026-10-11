# erugiosu1_new_arugo ごうけつ専用探索
## 最終対象
2026-10-11の追加指示に従い、新探索の対象を `erugiosu_1_gouketu`（`-Derugi1=1`）だけに限定した。`webassembly/build.sh` の対象branchから、はやて・かぶとのvariant定義を削除した。`SearchRequest` と `ActionOptimizer::RunAlgorithm` の新探索への接続も `erugi1` のみに限定している。
## 探索と局所最適化
探索本体は新規の `Erugiosu1Search.cpp` / `.h` に分離した。各深さで保持する状態数を制限するbeam探索を使い、幅を32、128、512、2048と段階的に広げる。全状態を保持するのは現在・次のfrontierだけで、過去の経路は親indexとactionで保持する。深さごとに行動列の全組み合わせを保存しない。
ごうけつの実測に基づき、ダメージ重視、ため重視、準備手数重視の順で探索する。最初の勝利だけでは終了せず、幅を広げて改善を試す。採用解があり、2回続けて幅を広げても改善がなければ次の評価へ進む。すべての評価で最良解と総deadlineを共有する。
終撃付近では、実worldと同じ次ターンの速度乱数2回をカーソルのコピーで読み、先行できる状態を探索順で優先する。敵2行が記録される前に撃破できる可能性を評価するためであり、予測ダメージによる勝利判定や枝刈りは行わない。実際のRNG cursor・乱数消費・速度値は変更しない。
この調整によって、25ターンprefixから追加11ターンの入力は、同じ装備変更2回で `BattleResult.position=108` から `106` へ改善した。はやて・かぶとの200ターン級の入力は最終調整の対象にしていない。
## 固定条件
- `-1` までのprefixを変更せず、元の初期状態とseedから再生して探索開始状態を作る。未終端や暗黙行動を表す0は入力不正として扱う。
- 新しいsuffixに通常攻撃を生成しない。さみだれづきはonだけで許可する。補助・回復ではon、off、gannanを扱う。
- MP、アイテム数、必殺の使用条件を実worldに合わせて確認する。行動不能時は装備を維持した行動不能markerだけを生成する。
- 各辺は実際の `BattleEmulator::Main` の1ターン実行で作る。完全に一致する将来状態だけを重複除去し、hash衝突だけでは状態を捨てない。
- 勝利候補は元のworldからfresh replayする。全行動列の一括再生と1ターンずつの再生を照合し、探索状態・RNG・行数とも一致し、敵HPが厳密に0の候補だけを採用する。
- 比較はfresh replayの `BattleResult.position` が最優先。同じ値なら、再生中に実際に発生した装備変更回数を優先する。RNG cursorはこのpositionとは別に保持する。
- 総budgetは最大1500ms。後処理の余裕を最大75ms確保し、探索・候補検証・解の改善で1つのdeadlineを使う。1500msまで待機する処理はない。
- `dumpTable` は採用したfresh replayと完全なaction列から生成する。未発見時は勝利扱いせず、固定prefixだけを表示する。
## 最終実測
Linux上のg++ 13.3、C++20、`-O3 -DMINGW_BUILD=1 -Derugi1=1`。実際の `SearchRequest` を呼び、時間は呼び出しの外側で計測した。内部のfresh replayとdump生成を含む。終了後、さらに実 `BattleEmulator` で採用列を再生し、敵HP・行数・装備変更・prefix一致・suffixの行動制約を確認した。時刻は代表実測であり、実行環境によって変動する。
| 入力 | prefix長 | 採用suffix長 | 敵HP | exact position | 実装備変更 | elapsed ms |
|---|---:|---:|---:|---:|---:|---:|
| P1 | 1 | 7 | 0 | 22 | 0 | 52.51 |
| P7 | 7 | 1 | 0 | 22 | 0 | 0.241 |
| P10 | 10 | 8 | 0 | 52 | 0 | 197.49 |
| P25 | 25 | 11 | 0 | 106 | 2 | 157.91 |
全入力のseedは `0x1a7d14c2`。prefix一致はすべて確認でき、探索suffixの通常攻撃数とoff/gannanでのさみだれづき数はいずれも0だった。
入力prefix（各列の直後に `-1` を置く）:
```text
P1  = 30
P7  = 30,62,62,33,62,62,34
P10 = 30,27,32,27,32,27,32,27,32,27
P25 = 37,37,37,37,37,47,37,37,49,37,37,48,37,37,37,37,37,37,26,26,26,26,26,26,26
```
P25で採用した追加11ターン（prefixを含まない）:
```text
50,30,131134,131134,62,33,62,50,50,50,34
```
この列のfresh replayは全36ターン、敵HP0、味方HP262、MP0、`BattleResult.position=106`、RNG cursor1766、装備変更2回だった。ここに記載したseedや行動列を探索器へ埋め込んではいない。
## 接続とビルド
`SearchRequest -> Erugiosu1Search::Run`、既存の `DEBUG3 -> SearchRequest`、`wasm_search_dump -> buildDumpOutput -> SearchRequest` が、いずれも最終のごうけつ用探索を使う。`ActionOptimizer::RunAlgorithm` とその非同期経路も新探索へ接続している。
既存DEBUG3は `position=22`、装備変更0回、約52.2msで勝利した。既存Wasm公開関数をネイティブで呼ぶ経路ではP25を使い、`position=106`、装備変更2回、約161.5msを確認した。
CMakeのSOURCESと `webassembly/build.sh` のSRC_FILESに新規cppを追加済み。Wasmの対象定義は以下だけである。
```bash
BRANCH_VARIANTS[erugiosu1_new_arugo]="
erugiosu_1_gouketu: -Derugi1=1
"
```
Emscriptenが利用できる環境では、プロジェクトルートで次を実行する。
```bash
bash webassembly/build.sh --branch erugiosu1_new_arugo
```
今回の環境には `emcc` / `em++` がないため、実Wasmバイナリの生成・実行とブラウザでの1500ms計測は未実施。確認済みなのはビルド定義の接続、bash構文、およびネイティブでの同じ公開関数経路である。
## 変更範囲
新規: `Erugiosu1Search.cpp`、`Erugiosu1Search.h`、本報告。
接続変更: `ActionOptimizer.cpp`、`main.cpp`、`CMakeLists.txt`、`webassembly/build.sh`。
長いaction列の保持対応: `BattleResult.h` の結果配列を350ターン分の最大行数に合わせ、`lcg.cpp` の既存乱数列キャッシュを必要時に拡張するよう変更した。乱数の数列・生成式・実戦闘での消費順序は維持している。
`BattleEmulator.cpp`、`Player.h`、`Equipment.h`、`camera.cpp` と初期Player値は変更していない。新しいテストスイートやsanitizerは追加・実行していない。
幅を制限する探索であり、大域的最適性や全入力での勝利を証明するものではない。勝利未発見の場合は失敗を返し、不可能性の証明とは扱わない。
