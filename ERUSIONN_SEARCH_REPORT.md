# エルシオン探索 実装・実測結果

## 採用結果
対象: `erusionn_new_arugo` / `webassembly/build.sh` の `erusionn_v6`。
コンパイル定義: `erusionn_lv21=1`, `MULTITHREADING=1`。
最終既定: **variant 12 (`large-transposition-dfs`)**、総budget 1500ms。

初期の小幅ビームで勝利候補を確保し、残り時間で楽観的ダメージ上限による分枝限定探索を行う。
262,144スロットの状態表はハッシュ一致だけでは統合せず、Playerの全フィールド・NowState・RNG cursor・記録数の一致を確認する。
同じ将来状態なら実装備変更回数が少ない枝を優先する。表は1リクエストの中だけで使用し、seed/prefix別の回答を保持しない。

ダメージ上限は攻撃が全命中・全会心・テンション成功・攻撃上昇が失効しない楽観条件から求める。
上限値は枝刈りだけに使い、実ダメージやPlayerには書き込まない。実際の全遷移はBattleEmulator::Mainで計算する。

## 最終版の実測
同じ添付BasePlayers、固定prefix `[BUFF]`、Linux x86-64 / GCC Release / -O3。
すべてfresh full replayで敵HP=0、探索状態との一致を確認。実行環境によって所要時間は変動する。

| seed | 敵HP | 総ターン | exact BattleResult.position | exact装備変更回数 | 探索ms |
|---|---:|---:|---:|---:|---:|
| `0xc5a8c4e` | 0 | 11 | 31 | 0 | 850.803 |
| `0xc5a8c4d` | 0 | 10 | 28 | 2 | 69.690 |
| `0xc5a8c4f` | 0 | 10 | 28 | 2 | 120.544 |
| `0x12345` | 0 | 10 | 28 | 4 | 171.836 |

DEBUG3の既存入力 `seed=0xc5a8c4e`, `[BUFF]` を変更していない。
添付版の旧探索は12ターン（最終先制撃破、記録数34）、装備変更4回だった。
最終版は11ターン・記録数31・装備変更0回。最終採用列は実行ログに収録しているが、探索ソースに埋め込んでいない。

9手のprefix `[30,30,62,62,50,62,62,33,34]` からも、先頭9手を一切変更せず11ターン・記録数31・装備変更0回で継続撃破。
これは新規テストケースではなく既存SearchRequestの直接実行。実装は`-1`までのprefixを任意長として扱う。
既存400件の記録配列・LCG配列の範囲を超える入力は探索側で終了する。

## 比較した探索variant
0: ビーム・ポートフォリオ / 1: 均衡評価 / 2: 火力評価 / 3: 生存評価 /
4: 戦術別枠なし / 5: best-first / 6: 装備固定 / 7: 装備固定・変更併用 /
8: ダメージ上限DFS / 9: ビーム後DFS / 10: 実遷移前の上限枝刈り /
11: 65,536スロット状態表 / 12: 262,144スロット状態表 / 13: 16,384スロット状態表。

基準入力ではビーム各方式が記録数31・変更0回に到達。best-firstは今回の比較では勝利を得られなかった。
単独DFSは記録数31・変更2回、ビーム併用は記録数31・変更0回。
上限の事前判定と状態表で同品質に到達する所要時間を削減した。
状態表サイズ比較では基準入力の同結果が約1,390ms（16,384）、1,091ms（65,536）、893ms（262,144）だった。
追加比較seedでも装備変更探索が装備固定より小さいpositionを得たため、variant 12を採用した。
数学的な大域最適性は主張しない。

## 行動とprefix
探索候補はためる、すてみ、さみだれづき、特やくそう、ベホイミ、スカラ、防御、逃げる、および撃破ターン限定の通常攻撃。
通常攻撃は実遷移後に敵HP=0になった場合だけ採用し、生存敵への通常攻撃は探索を継続しない。
さみだれづきは要求装備が武器あり、かつMP>=4の場合だけ生成する。同じターンに武器を装備して使用することは可能。
ベホイミはMP>=4、スカラはMP>=3。これらは実エミュレータの消費値に合わせた。
ターン開始時に眠り/麻痺を持ち越している場合、探索側でFLEEと装備変更を生成しない。
このボス用にBattleEmulatorへFLEE修正や追加validatorを入れていない。

prefixは削除・置換・並べ替えず、実エミュレータでそのまま再現してからsuffixを探索する。
勝利候補を元のinitial worldからprefix+suffix全体でfresh replayし、敵HP、全Player状態、NowState、RNG cursor、記録数、装備変更回数の一致を確認する。
最終比較はそのfresh replayの `(BattleResult.position, ACTION_EQUIPMENT_CHANGEDの件数)`。
`Genome.position` は従来どおりRNG cursorの意味を保持している。

## 接続と変更範囲
- 新規探索本体: `ErusionnSearch.cpp`, `ErusionnSearch.h`。
- `SearchRequest -> ErusionnSearch::Run`。
- `DEBUG3 -> 既存SearchRequest -> ErusionnSearch::Run`。
- `wasm_search_dump -> buildDumpOutput -> ErusionnSearch::Run`。
- 旧 `ActionOptimizer::RunAlgorithm/RunAlgorithmAsync` の呼出しも同じ探索へ接続。
- CMake共通ソースとWebAssemblyビルドのSRC_FILESに新規ソースを追加。

既存ファイルの変更は `ActionOptimizer.cpp`, `main.cpp`, `CMakeLists.txt`, `webassembly/build.sh` のみ。
`BattleEmulator.cpp/.h`, `Player.h`, `BattleResult.h`, `lcg.cpp/.h`, `camera.cpp/.h`, `Equipment.h`, `InputBuilder.cpp/.h` は添付ZIPとバイト一致。
世界の値、乱数消費、既存検証を変更していない。

## 実行確認の範囲
CMake Releaseの`ebe_lv21_v2`ビルドと、最終既定でのDEBUG3実行が成功。
既存の`wasm_prepare_input("b")`と`wasm_search_dump`のC++エントリをnative共有ライブラリとして実行し、敵HP0・position31・変更0回の出力を確認。
これは**WebAssemblyバイナリの実行ではない**。環境に`emcc`がなく、導入先の名前解決もできなかったため、実WebAssemblyビルド・ブラウザ実行は未確認。
ビルド定義への接続はZIPに含まれる。

## 呼び出し
通常のRelease実行ファイルから同じSearchRequestへ直接渡せる。

```text
ebe_lv21_v2.exe --search 0xc5a8c4e 30
ebe_lv21_v2.exe --search 0xc5a8c4e 30 --variant=12 --budget-ms=1500
```

seedの後の数値はすべて固定prefixのaction（装備bitを含む）。`-1`はCLIが末尾に付加する。
引数なしDEBUG3は従来の入力から同じ既定探索を実行する。
WebAssemblyは従来どおり、リポジトリルートで次のビルドを使う。

```sh
bash webassembly/build.sh --branch erusionn_new_arugo
```

実測ログは`search_results/`。新しいunit test、test suite、sanitizerは追加・実行していない。
