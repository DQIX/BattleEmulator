# reokonn_lv8_new_arugo 探索改善・最終報告
## 結論
独立新規ファイル `ReokonnSearch.cpp/.h` を実装し、24種類のdevelopment variantから **variant16: greedy-attack-preview** を採用した。固定後の独立holdout40ケースでは、250msで新方式37勝・baseline26勝、1000msで新方式37勝・baseline32勝。双方勝利ケースの `BattleResult.position` 悪化とbaselineのみの勝利は、いずれの予算でも0件だった。native SearchRequestとWebAssembly側buildDumpOutputへ接続済み。
予測重み、初期幅、末尾修復、生存枠の混合を追加比較してもdevelopmentの品質はほぼ横並びになったため、複雑な方式を増やし続けず提出する。全入力での最適性や、これ以上改善不可能という証明ではない。
## 独立holdoutの結果
| 指標 | 250ms | 1000ms |
|---|---:|---:|
| ケース数 | 40 | 40 |
| baseline勝利 | 26/40（65.0%） | 32/40（80.0%） |
| 新方式勝利 | 37/40（92.5%） | 37/40（92.5%） |
| 双方勝利・position改善 | 12 | 6 |
| 双方勝利・position同等 | 14 | 26 |
| 双方勝利・position悪化 | 0 | 0 |
| baselineのみ勝利 | 0 | 0 |
| 新方式のみ勝利 | 11 | 5 |
| 双方未発見 | 3 | 3 |
| 共通勝利ケースの平均position：baseline | 43.461538 | 43.625000 |
| 共通勝利ケースの平均position：新方式 | 42.269231 | 43.343750 |
| 共通勝利ケースの初回発見平均：baseline | 72.693591ms | 136.316659ms |
| 共通勝利ケースの初回発見平均：新方式 | 2.247220ms | 2.164356ms |
勝利集合が異なるため、各方式が勝てたケースだけの平均positionを直接比較していない。表のpositionと初回発見平均は双方勝利の共通集合で計算した。未発見を勝利として扱っていない。
生データは `evaluation/results/holdout.csv`、集計は `comparison.csv`、ケース別対応表は `paired-cases.csv`。全holdoutの既存dumpTable出力は `holdout-250-dumps/` と `holdout-1000-dumps/`。CSVにはseed、prefix長、正確position、行動長、発見時刻、実測時間、探索数、RNG位置、nowState、最終HP/MP/薬草数を保存した。
## 時間条件
Linux x86_64、Intel Xeon Platinum 8573Cのコンテナ、GCC 14.2.0、C++20、Release、各探索は単一スレッド。両方式に同じ入力、世界、合法action、時間予算、終了余裕を適用し、ケースごとに実行順を交互にした。予算の10%を返却余裕とし、探索締切は250ms指定で225ms、1000ms指定で900ms。新方式は締切後の候補を採用しない。
| 戻りまでの実測時間 | baseline平均 | 新方式平均 | baseline最大 | 新方式最大 |
|---|---:|---:|---:|---:|
| 250ms指定 | 217.975ms | 209.221ms | 264.899ms | 230.499ms |
| 1000ms指定 | 870.230ms | 833.866ms | 971.945ms | 906.230ms |
**250msのbaseline case28だけ264.899msを要した。** 隠したり除外したりせずbaselineの勝利として集計した。新方式は全holdout実行で予算内だった。これは実測であり、任意のOS負荷での厳密なhard deadline保証ではない。初回発見時刻は探索中の記録であり、その時点でAPIが途中結果を返すという意味ではない。
## baseline・development・holdout
元の `ActionOptimizer.cpp/.h` は無変更で保持した。比較用 `evaluation/BaselineTimed.cpp` は元の探索本体をコピーし、コスト、hash、heap、Genomeコピー、turn優先の勝利選択、horizonを維持した。世代数と改善後2000展開での早期終了を固定時間締切へ置き換え、元のA*にも時間を使い切る機会を与えた。新方式だけ探索量を増やした比較ではない。
睡眠・麻痺時のFLEEでpre-actionを不正回避する枝は、比較用baselineと新方式の双方で不採用とした。7種類の合法action一覧とMP・薬草の条件、世界側のFLEE実装自体は変更していない。
developmentは20ケース、prefix長0/1/3/6/9。最終固定版の1000ms比較は17対15勝、8改善・7同等・0悪化、新方式のみ2勝、双方未発見3件。独立holdoutは異なる生成saltの40ケースで、各prefix長を8ケースずつ含む。prefixは勝利解から切り出さず、ランダムな合法actionから生存・非終端の観測状態を作った。任意の全状態を均等に代表する評価ではない。
holdout開始前の探索ソースとbaselineのSHA-256は `evaluation/frozen-search.sha256`。その後に探索を再調整していない。既知行動列、seed別正解表、評価case認識は探索コードに含めていない。
## 採用探索器
元のPlayerから固定prefixを正規Mainで再生し、Player全項目、RNG位置、nowState、実ログ件数を現在rootへ引き継ぐ。その後のsuffixだけを探索する。
350個のactionを枝ごとにコピーせず、採用枝の親リンクと1actionを保持する。重複排除はhash一致だけで枝を捨てず、Player全意味項目、RNG位置、nowState、positionの一致を確認する。hashに読むpaddingの差は重複の見逃し方向にしか働かず、異なる意味のstateを統合しない。
敵残HP、味方HP、瀕死ペナルティ、MP、薬草から順位を付け、現在stateのコピーに正規Mainで次の通常攻撃を1回だけ実行し、敵HP差を予測加点する。予測stateは順位付け専用で、本物の探索stateへ書き戻さない。ビーム幅は64→256→1024→4096→16384→32768。時間とincumbentの実positionで制限し、枝を刈らずに探索完了した場合は繰り返さない。
勝利候補は元の初期Playerと固定prefix＋suffixから完全再生する。敵HP0に加え、最終Player全項目、RNG位置、nowState、BattleResult.positionが探索内の到達状態と一致する候補だけ採用する。優先順位は小さい正確position、同値なら短い行動長、さらに同値なら高い味方残HP。RNGカーソルを勝利速度として扱わない。
評価器は逐次再生と完全再生のイベント各項目も照合した。holdout160実行すべて `verified=1`、候補のreplay rejectは0。新規テストsuite、sanitizer、メモリ検査は作成・実行していない。
## 本番接続と実行確認
native SearchRequestとWebAssembly側buildDumpOutputは既定1000ms・variant16でRunを呼ぶ。完全再生済みのBattleResultを既存dumpTableへ直接渡す。従来の100ターン再生で終端後の不足actionを通常攻撃で補う経路は使わない。勝利未発見ならprefixと未発見概要を出し、勝利を捏造しない。
dumpTable本体・signature、世界、PastTurnsの表示方針、WebAssembly export一覧は維持。native接続箇所のprefix終端だけ、旧探索本体と同じ0/-1認識に合わせた。CMakeとWebAssemblyのソース一覧へ新cppを追加した。
native Releaseビルドと実SearchRequestの勝利・未発見出力を実行済み。記録は `evaluation/results/native-searchrequest.txt` と `native-searchrequest-no-win.txt`。**EmscriptenがないためWebAssemblyビルド・ブラウザ実行は未確認。** Windows/MinGWでの実行も未測定。
## 代表解
holdout case13、250ms、seed `0x1b2905`、固定prefix6手 `27,53,62,27,25,53`。baselineは28手・position55、新方式は25手・position49。最終状態は敵HP0、味方HP6、MP4、薬草2、RNG位置1006、nowState `0x19310`。完全再生一致。新方式の初回発見3.96843ms、最終更新37.1821ms、返却225.614ms。
全25手は `27,53,62,27,25,53,26,26,61,61,62,62,25,25,62,25,62,25,25,26,25,62,25,25,61`。これは実測出力でありコードに埋め込んでいない。dumpTable全文は `evaluation/results/representative.txt`、baseline側は `representative-baseline.txt`。既存tableのHP等は行動前情報を含むため、最終状態は直後のexact replay概要を正とする。
## ビルド・再実行
native本体は通常のReleaseビルドを使う。
```bash
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build -j2
```
評価器と実SearchRequestは次の通り。
```bash
bash evaluation/build.sh
evaluation/build/reokonn_benchmark dev 0 20 1000 baseline,16 development.csv development-dumps
evaluation/build/reokonn_benchmark holdout 0 40 250 baseline,16 holdout-250.csv holdout-250-dumps
evaluation/build/reokonn_benchmark holdout 0 40 1000 baseline,16 holdout-1000.csv holdout-1000-dumps
evaluation/build/reokonn_benchmark request dev 1
```
比較用variantは0〜23で指定する。API既定は16であり0ではない。予算はRunのbudgetMs、既定値はReokonnSearch::DefaultBudgetMs。Linux実行済みバイナリはbin/へ同梱した。
## 参考元と成果保持
元HEADは `0054de4be4a1016db2baf7b81fc2788c5768684a`。参考branchは `origin/bilyouma-gpt6-pro` の `a39276d8063f0a867127da52ce71a7545e011454` と `origin/yo2-gpt6-pro` の `4317f4863e79f1bdc861f4faea1f94613285211d`。親リンク、exact state比較、ビーム管理、末尾再探索等を参考にした。敵データ、別戦闘action、既知解は移植していない。添付optimization ZIPも探索技術の参考に限定した。
変更は新規探索2ファイルと接続用main.cpp、CMakeLists.txt、webassembly/build.sh。元の世界とbaselineは `evaluation/original/sha256.txt` で一致を確認済み。全24variant、比較用baseline、評価CLI、生データ、dump、入力Git履歴を最終ZIPへ保持し、巨大build cacheは除外した。途中成果はcheckpoint01〜03にも保存した。
Final ZIP: `/mnt/data/reokonn_lv8_optimized_final.zip`