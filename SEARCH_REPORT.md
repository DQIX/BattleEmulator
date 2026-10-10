# バルボロス探索 実装・比較結果
## 対象
添付 ZIP の `baruborosu_new_arugo` に実装した。対象は `-DGOUKETU=1`、WebAssembly variant は `baruborosu_gouketu`。指示書の対象名は、ユーザーの訂正に従ってバルボロスとして扱った。
## 採用した探索
`BaruborosuSearch.cpp` / `.h` に、幅を制限したビーム探索と反復的な幅拡大を実装した。幅は 64 → 256 → 1024 → 4096。既定 variant 3 は、ダメージ優先、ため・補助状態の多様性維持、準備行動を含む将来火力の見積もり、という 3 種類の順序付けを同じ時間枠内で実行する。改善を出した順序付けを次の幅で先に試す。
各深さで保持する状態数を幅で制限し、同一状態を統合する。両 Player の全フィールド、NowState、乱数カーソル、深さを比較し、ハッシュ衝突だけで状態を破棄しない。祖先には親リンクと行動だけを保持する。長い行動列を全ノードに複製せず、10 ターン以上でも各深さの展開量を制限する。
推定値は探索順序の決定に使用する。枝刈りの勝利条件・目的値は実エミュレータから取得する。攻撃・回復・補助・アイテムと 3 種の装備を、MP、所持数、必殺、状態異常、槍専用技の条件に合わせて列挙する。状態異常中の装備・行動変更や、FLEE による同一ターンの行動不能処理の迂回は新規候補に採用しない。
## 勝利・評価・prefix
各枝は `BattleEmulator::Main` で 1 ターン進める。勝利候補は、初期 Player、初期 NowState、初期乱数カーソルから固定 prefix を含む全行動列を別途再生する。敵 HP = 0、最終 Player、NowState、乱数カーソル、記録行数、装備変更記録が枝探索の結果と一致した候補だけを採用する。
採用結果の比較は `BattleResult.position`、次に味方行動に記録された `ACTION_EQUIPMENT_CHANGED` の回数、の辞書順。ターン数や残り HP は勝利結果の優劣判定に使わない。例えば position 33・装備変更 0 回の初期案より、position 31・装備変更 4 回の最終案を優先する。
`aActions[350]` の先頭から `-1` までを変更せず再生し、その終端状態からだけ探索する。短い prefix への切り詰め、prefix の再探索、seed 別の解答表はない。Main は `-1` だけでは停止しないため、prefix 再生と最終再生には実際の行動列長を渡す。prefix だけで戦闘が終了する場合は、その結果と実際に実行されたターン数を返す。配列内に `-1` がない入力は `input_valid=0` とする。
## 時間制御
1 回の `SearchRequest` が持つ探索予算を最大 1500 ms とし、全幅・全順序付けで共用する。1500 ms 設定時は 1400 ms を探索の打切り時点とし、最終再生・結果返却・メモリ解放の余裕を確保する。計測される `elapsed_ms` には探索オブジェクトの解放まで含む。
期限到達、幅制限による切捨てを伴わない候補の探索完了、または勝利取得後に幅を広げても 2 ラウンド連続で目的値が改善しなかった場合に終了する。待機や最低実行時間は設けない。有限幅の探索なので、大域的最適解の保証は付けない。
## 接続した経路
通常の `GOUKETU` の `ActionOptimizer::RunAlgorithm` と `SearchRequest` を新探索につないだ。既存 DEBUG3 入口も同じ `SearchRequest` を通る。WebAssembly の `wasm_search_dump` からも同じ処理を呼び、既定 variant 3・予算 1500 ms を使用する。CMake と `webassembly/build.sh` のコンパイル対象に新規ソースを追加した。
勝利時は最終再生の表と全行動列を表示する。表の最終 HP / MP は最終状態と一致させる。予算内に検証済み勝利が得られなかった場合は、その旨を返す。
native DEBUG3 には比較用の `--search-variant`、`--search-ms`、`--search-seed`、`--search-prefix` を追加した。variant -1 は旧探索、0～2 は個別の順序付け、3 は採用した組合せ。seed または prefix を指定すると 1 回の探索を実行する。
## 比較結果
実測はこの環境の native GCC、`-std=c++20 -O3 -DNDEBUG -DGOUKETU=1`。既存 DEBUG3 → SearchRequest → 実エミュレータを使用した。各値は 1 回の計測値で、CPU の実行状況によって時間や期限内の展開数は変動する。全行の勝利は敵 HP = 0、`replay_rejected=0`。
同一 seed `0x350e5772`、固定 prefix `30`、上限 1500 ms の順序付け比較：
| variant | 内容 | ターン | position | 装備変更 | elapsed_ms |
|---|---|---:|---:|---:|---:|
| 0 | ダメージ優先 | 12 | 34 | 2 | 368.124 |
| 1 | ため・補助状態の多様性 | 11 | 31 | 4 | 1401.95 |
| 2 | 準備行動込みの火力見積もり | 11 | 31 | 4 | 1402.19 |
| 3 | 共通期限内で 3 種を実行 | 11 | 31 | 4 | 1407.24 |
対応ログは `search_benchmarks/refined_v0.txt` ～ `refined_v3.txt`。多様性の枠を装備別に 60 分割していた初期案から、ため・攻撃補助・ミラーによる 20 分割へ変更し、position を 33 から 31 に改善した。勝利後に行動 ID を固定して装備だけを再探索する案も比較したが、目的値の追加改善がなく、最終コードには採用しなかった。
採用した variant 3 の最終計測：
| seed | prefix 長 | ターン | position | 装備変更 | 味方 HP / MP | elapsed_ms |
|---|---:|---:|---:|---:|---|---:|
| 0x350e5772 | 1 | 11 | 31 | 4 | 34 / 149 | 1481.01 |
| 0x350e5773 | 1 | 11 | 31 | 2 | 165 / 125 | 1146.60 |
| 0x350e5778 | 1 | 10 | 28 | 4 | 50 / 149 | 1022.95 |
| 0x13579bdf | 1 | 11 | 31 | 2 | 16 / 153 | 1403.04 |
| 0x350e5772 | 4 | 11 | 31 | 4 | 34 / 149 | 1401.25 |
| 0x350e5772 | 10 | 11 | 31 | 4 | 34 / 149 | 0.220831 |
prefix 長 1 は `30`。長さ 4 と 10 は、下記の勝利行動列の先頭 4 個・10 個をそのまま指定した。入力 prefix が保たれていることを出力の全行動列で確認した。長さ 1 の例では suffix を 10 ターン探索している。長さ 10 の例は 1 状態・46 候補の展開で完了し、1500 ms を待たず終了した。最終の表示処理の再ビルド後にも同じ長さ 10 の入力を実行し、同じ勝利・目的値を 0.248957 ms で得た。
対応ログは `search_benchmarks/final_*.txt`。旧探索も比較用に実行したが、既存の 7 seed 連続実行は外側の 15 秒上限で打切りになった。この値を旧探索 1 回の所要時間としては扱っていない。
## 既定 seed の勝利行動列
seed `0x350e5772`、固定 prefix `30`：
```text
30, 131103, 34, 62, 62, 131134, 131105, 62, 34, 25, 34, -1
```
最終実再生は 11 ターン、position 31、装備変更記録 4 回、味方 HP 34 / MP 149、敵 HP 0。
## 再現コマンド
以下はリポジトリのルートで実行する native のビルドと既存 DEBUG3 入口による計測。実際の検証は同じフラグでオブジェクトを分割コンパイルしてリンクした。
```bash
g++ -std=c++20 -O3 -DNDEBUG -DGOUKETU=1 \
  main.cpp lcg.cpp BattleEmulator.cpp camera.cpp debug.cpp \
  ActionOptimizer.cpp BaruborosuSearch.cpp EnhancedCostCalculator.cpp \
  EnhancedHashCalculator.cpp EnhancedHeapQueue.cpp -o baruborosu_search
./baruborosu_search --search-seed 0x350e5772 --search-variant 3 --search-ms 1500
./baruborosu_search --search-seed 0x350e5772 --search-variant 3 --search-ms 1500 \
  --search-prefix '30,131103,34,62,62,131134,131105,62,34,25'
```
WebAssembly の対象コマンド：
```bash
bash webassembly/build.sh --branch baruborosu_new_arugo
```
この環境では `emcc: command not found` で終了したため、Wasm のコンパイルとブラウザ上の性能は未確認。ソースの接続とビルド対象の更新、build.sh の構文確認は完了している。CMake もこの環境にはないため、native 実行確認には上記 GCC を用いた。
## 変更範囲
追加：`BaruborosuSearch.cpp`、`BaruborosuSearch.h`、本書、`search_benchmarks/` の実行ログ。変更：`ActionOptimizer.cpp`、`ActionOptimizer.h`、`main.cpp`、`CMakeLists.txt`、`webassembly/build.sh`。
`BattleEmulator.cpp`、`BattleEmulator.h`、`Player.h`、`BattleResult.h`、`Equipment.h`、`lcg.cpp`、`lcg.h`、`camera.cpp`、`camera.h` は添付 ZIP とバイト単位で一致することを確認した。敵 AI、ダメージ、初期能力、装備値、乱数処理を変更していない。
