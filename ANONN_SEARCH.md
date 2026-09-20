# anonn_new_arugo 探索実装
## 対象と接続
対象は `webassembly/build.sh` の `anonn_v6`（`erusionn_lv21=1`、`MULTITHREADING=1`）。探索本体は `AnonnSearch.cpp` / `AnonnSearch.h` に分離した。
`SearchRequest -> ActionOptimizer::RunAlgorithm -> AnonnSearch::Run` を使用する。既存 `DEBUG3 -> SearchRequest` を維持し、WebAssembly側も `wasm_search_dump -> buildDumpOutput -> ActionOptimizer::RunAlgorithm -> AnonnSearch::Run` で同じ本番探索を呼ぶ。
両入口の標準総予算は1500ms。prefix処理、候補検証、探索用メモリの解放を計測に含め、最終表示用replayの余裕を残す。最初の解が得られない場合だけ代替探索へ進み、予算を足し増ししない。
## 採用方式
本番はvariant 0。幅512のビーム探索で勝利候補を得て、記録イベント数を上限にした反復深化の深さ優先探索で短縮と装備変更削減を試す。初期ビームで未勝利の場合は、残りの共通予算内でweighted best-firstと別ビームへ切り替える。
深さ優先探索のダメージ上限は、その呼び出しの実ステータスから計算する。4回会心、MP制約なし、バフ期限なし、無料の装備選択という有利な仮定でも届かない枝を除くための上限であり、実ダメージや勝利判定には使わない。
全状態遷移は実際の `BattleEmulator::Main` で実行する。状態のfingerprintが同じだけでは統合せず、Playerの全フィールド、NowState、乱数位置も照合する。seed別回答、prefix別回答、既知勝利列のlookupはない。
比較順はfresh replay後の `enemy HP == 0`、`BattleResult.position`、実際の `ACTION_EQUIPMENT_CHANGED` 件数。乱数cursorは別に保持する。勝利候補を初期worldからprefix込みで再生し、探索側の最終状態、乱数位置、記録数、変更数が一致しなければ採用しない。
## actionと持越しの休み
ためる、すてみ、さみだれづきを主軸に、通常攻撃、スカラ、ベホイミ、ホイミ、特やくそう、まほうのせいすい、防御、合法な逃げるを候補にする。MP消費は実処理に合わせ、スカラ3、ベホイミ4、ホイミ2、さみだれづき4。さみだれづきは武器装備時だけ生成する。
ターン開始時に `isStunned` / `sleeping` / `paralysis` が残る場合、装備を維持して通常の状態処理を通す。FLEEを生成しない。fixed prefixがこの制約に違反する場合も、prefixを書き換えて探索せず、未勝利として返す。前ターン中に休みが解消して次ターンへ持ち越されなければ、次ターンの装備変更を候補にできる。
`BattleEmulator.cpp` の変更は、持越し状態で渡されたFLEEを通常攻撃placeholderへ戻し、既存の状態処理を実行させる部分だけ。新しい包括的validatorは追加せず、既存の装備変更チェックとその他の検証は残した。
`BattleResult` の記録容量のみ700へ拡張した。349コマンドと終端を扱う際、1ターン最大2記録を保持するためであり、positionの意味、ダメージ、敵AI、乱数消費の計算は変更していない。
## 実測結果
Linux / GCC / Release（`-O3 -DNDEBUG`）、既存DEBUG3入力 `seed=0x0c421b38`、fixed prefix `[BUFF]`。時間はこの環境の一回の測定値で、ブラウザーのWasm速度ではない。
| 方式 | exact position | 実装備変更 | 探索時間ms |
|---|---:|---:|---:|
| 旧探索 | 17 | 2 | 25.098 |
| 本番0：beam 512 + 記録数上限DFS | 17 | 0 | 33.508 |
| 1：best-first、重み1.2 | 17 | 0 | 777.611 |
| 2：best-first、重み1.8、HP重視 | 17 | 0 | 997.302 |
| 3：beam 512 | 17 | 0 | 16.049 |
| 4：beam 4096、HP重視 | 17 | 0 | 104.040 |
| 5：beam 16384、攻撃寄り | 17 | 0 | 411.365 |
| 6：best-first、重み1.0、攻撃寄り | 17 | 0 | 741.953 |
| 7：beam 32768 | 17 | 0 | 846.860 |
| 8：beam 512 + 記録数上限DFS、fallbackなし | 17 | 0 | 33.126 |
| 9：beam 65536、攻撃寄り | 17 | 0 | 1462.660 |
全て撃破。新探索の再生不一致による却下は0件。旧探索のpositionと変更数は、その出力列を別途fresh replayして確認した。旧探索の時間は旧SearchRequestの既存計測値で、比較用の別replay時間は含めない。
本番0では、同じprefixで `0x0c421b39` がposition 15・変更2回・63.661ms、`0x0c421b3a` が15・0回・31.269ms、`0x123456` が13・2回・25.559ms。5手のprefix `[30,25,30,33,62]` を固定した場合は17・2回・11.774msで、出力先頭5手も変更していない。
既存DEBUG3入力の本番出力は `30,25,62,62,50,33,62,62,34`。これは探索結果の記録であり、探索器に埋め込んだ行動列ではない。9ターン目のさみだれづき1404ダメージで撃破する。dumpTableのHP欄は既存のターン開始時表示であり、勝利判定はfresh replay完了後のPlayer.hpを使う。
複数の評価方式、幅512〜65536、best-first、記録数上限DFSを比較して改善が止まったため、追加の一般QAやテスト基盤整備へは進んでいない。数学的な大域最適性は主張しない。
## ビルドと実行
既存 `debug.h` のDEBUG3設定を維持している。ZIPを展開したBattleEmulatorディレクトリで実行する。
```sh
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build --target anonn_lv21_v2 -j 4
./build/anonn_lv21_v2
./build/anonn_lv21_v2 --variant 8 --budget-ms 1500 --seed 0x0c421b38 --prefix 30
./build/anonn_lv21_v2 --prefix 30,25,30,33,62
```
`--variant` は0〜9。`--prefix` はコンマ区切りの正のaction値で、終端は自動設定する。指定を省略した場合は元のDEBUG3入力を使う。Windowsでは生成された実行ファイルのパスを使う。
WebAssemblyのビルド定義には新規探索ファイルと8MiBのスタック指定を接続した。Emscriptenが利用可能な環境では既存スクリプトを使う。
```sh
bash webassembly/build.sh --branch anonn_new_arugo
```
## 検証できた範囲と制限
CMakeの実targetをビルドして、DEBUG3からSearchRequestの本番探索を実行した。既存の `wasm_prepare_input -> wasm_search_dump` C APIも、`__EMSCRIPTEN__` の浮動小数点計算分岐と既存MINGW用export経路を有効にしたネイティブ共有ライブラリで実行した。入力 `b`、seed `0x0c421b38` でenemy HP 0・position 17・変更0回を確認した。
実Wasmのコンパイルとブラウザー実行は未確認。環境に `emcc` がなく、取得先のDNS解決にも失敗したため、実ビルド試行は `emcc: command not found` で停止した。ネイティブC APIの確認を実Wasmビルド成功とは扱っていない。
`search-benchmark/` に最終版の主要実行ログを同梱した。新規unit test、regression test、CI test、sanitizer、検査用frameworkは追加していない。