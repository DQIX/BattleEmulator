# zilyadama_new_arugo 探索実装
## 対象と成果
対象は `webassembly/build.sh` が定義する `zilyadama_v6_tamahane` と `zilyadama_v6_hagane` の2 variant。既存の `ActionOptimizer::RunAlgorithm` を、総時間予算を共有する反復ビーム探索へ置き換えた。元の状態からfixed prefixと探索suffixをfresh replayし、敵HPが0で、探索状態と一致する候補だけを採用する。勝利候補の比較値は `BattleResult.position` であり、RNG cursorやターン数ではない。
## 探索と接続
探索開始から1490msを締切とし、独立replayと呼び出し側の最終replayのために余裕を残す。狭い探索幅で初期解を得た後、同じ締切内で探索幅と資源評価を変えて改善する。状態の重複除去には実際のPlayer各フィールド・NowState・RNG cursorを用い、hash一致だけでは状態を同一視しない。似たRNG位置の候補がbeamを埋め尽くさないように候補を分散する。
通常攻撃、ドラゴン斬り、防御、逃げる、特やくそう、ホイミ、ヒャド、バギ、ヒャダルコ、アクロバットスターを実際の使用条件に応じて候補にする。会心やアクロバットスターの効果は実際のBattleEmulator遷移で評価し、固定の戦術や行動列は埋め込んでいない。
MP条件はBattleEmulatorの消費に合わせた。敵が先にマホトラを使うなどして、選択した呪文の実行時にMPが不足する経路も、遷移ログの負のMPを検出して除外する。眠り・麻痺状態からのFLEEによる既知の不正skipを探索に使わない。world側の処理は変更していない。
fixed prefixは350要素の入力配列の `-1` までを一般に処理し、その内容・順序を変更しない。suffixのみを探索する。候補採用時には元のPlayer・seed・初期cursor・初期NowStateから全列を再実行し、敵HP、結果position、RNG cursor、NowState、Player全フィールドの一致を確認する。
最終接続は次のとおり。新しい専用wrapperや接続切替flagは追加していない。
```text
DEBUG3 -> 既存SearchRequest -> ActionOptimizer::RunAlgorithm
SearchRequest -> ActionOptimizer::RunAlgorithm
wasm_search_dump -> 既存buildDumpOutput -> ActionOptimizer::RunAlgorithm
```
SearchRequestとWasm用入口でも独立replayとprefix保持を確認し、そのreplay結果からdumpを生成する。成功時のGenome.fitnessは検証済みBattleResult.position、Genome.positionは従来どおりRNG cursor。未撃破はInitialized=falseとして成功扱いにしない。
## 最終コードのネイティブ実測
GCC 14.2.0、CMake 3.31.6、既存CMakeターゲットのReleaseビルド、DEBUG3からSearchRequestを実行した結果。時間には呼び出し側のfresh replayを含む。全行で `enemy_hp=0`、`prefix_preserved=1`、`replay_verified=1` を確認した。時間依存探索のため、他の環境や再実行で行動列・結果が同一になる保証や、数学的な最適性の主張はしない。
| variant | seed | fixed prefix | exact position | ターン数 | 時間(ms) |
|---|---|---|---:|---:|---:|
| tamahane | 0x81a66014 | 25 | 36 | 15 | 1491.85 |
| hagane | 0x81a66014 | 25 | 41 | 17 | 1491.69 |
| tamahane | 0x13579bdf | 25 | 34 | 14 | 1403.99 |
| hagane | 0x13579bdf | 25 | 41 | 17 | 1491.62 |
| tamahane | 0x81a6609d | 25,50,25 | 36 | 15 | 1233.29 |
| hagane | 0x81a6609d | 25,50,25 | 49 | 20 | 1491.83 |
行動25は通常攻撃、50は特やくそう。haganeのseed `0x13579bdf` では、実際に8ターン目のアクロバットスターを含む撃破列を得た。完全な行動列と戦闘dumpは `verification/` に同梱した。
添付旧探索を変更せず、同じ入力を既定のSearchRequest呼び出しで測定した場合、seed `0x81a66014` のpositionはtamahane=36、hagane=41で、新探索も同値だった。seed `0x13579bdf` はtamahane=34で同値、haganeは旧44・18ターンに対し新41・17ターンへ改善した。旧探索の実測時間はこれらで約305～340msであり、旧探索を1500msに再調整した比較ではない。
旧探索のseed `0x81a6609d`、prefix `25,50,25` のhaganeはposition48を返したが、MPが-5になる呪文経路を含んでいたため、有効な比較解から除外した。新探索のposition49はこの負のMP経路を使わない。別の試行中に見つけた負のMPを含む旧解も、改善実績として数えていない。
## ビルドとWasmの確認範囲
対象2 variantに対応する既存CMakeターゲットはコンパイル・リンク成功。Wasm用の既存exportを `MINGW_BUILD=1` でネイティブ共有ライブラリとしてビルドし、`wasm_prepare_input` と `wasm_search_dump` を実行した。tamahaneはposition36・1495.56ms、haganeはposition41・1491.18msで撃破、prefix保持、fresh replay一致を確認した。これはWasm用入口のネイティブ実行であり、Wasm runtimeでの実測ではない。
実際の `webassembly/build.sh --branch zilyadama_new_arugo` も実行したが、環境にEmscripten SDKがなく `emcc: command not found` で停止した。実Wasmバイナリのコンパイル・ブラウザ実行・ブラウザ上の1500ms達成は未確認。ビルドスクリプトのvariant定義・ビルド内容は変更せず、bashでの実行を妨げていたCRLFだけをLFへ変更した。
### ネイティブでの再実行
BattleEmulatorディレクトリから実行する。DEBUG3は引数なしなら元のseedと1手prefixを使い、引数を与えた場合だけ単一の入力を差し替える。新しいテストスイートは追加していない。
```bash
cmake -S . -B build-search -DCMAKE_BUILD_TYPE=Release -DCMAKE_CXX_FLAGS=-DDEBUG3=1
cmake --build build-search --target zbe_lv16_sp22_tamahagane_atk123_def86_v2 zbe_lv16_sp22_hagane_atk106_def93_v2 -j4
./build-search/zbe_lv16_sp22_tamahagane_atk123_def86_v2
./build-search/zbe_lv16_sp22_hagane_atk106_def93_v2
./build-search/zbe_lv16_sp22_hagane_atk106_def93_v2 0x13579bdf 25
./build-search/zbe_lv16_sp22_hagane_atk106_def93_v2 0x81a6609d 25 50 25
```
### Emscripten導入済み環境での既存ビルドコマンド
```bash
bash webassembly/build.sh --branch zilyadama_new_arugo --output public
```
## 差分と同梱内容
実装差分は `ActionOptimizer.cpp`、`ActionOptimizer.h`、`main.cpp`。`webassembly/build.sh` は改行コードのみ変更した。BattleEmulator、Player、BasePlayersの能力値、LCG、敵AI、ダメージ・会心・状態異常処理は変更していない。
`verification/` は実行済みのビルドログ・benchmark dump・Wasmビルド試行ログであり、実行が必要な追加ツールやテスト基盤ではない。ZIPにはソース一式とこの報告を含め、`.git`、IDE個人設定、古いビルドキャッシュ、試行用の旧探索コード、検証用バイナリは含めない。