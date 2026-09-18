# ガナンのおおしゃく対応・実測報告
## 対象と採用結果
対象は添付`BattleEmulator(20260918-032011).zip`の`erugiosu_new_arugo`、通常の`GOUKETU=1`経路。ガナンを含む合法な武器選択を既存探索に接続した。採用値は`ErugiosuSearch::DefaultVariant = 0`、`DefaultBudgetMs = 1500`。勝利を前提に、正確な`BattleResult.position`、固定prefix後の実際の装備変更回数の順で比較する。RNG cursorやターン数を第一目的にはしていない。
## 実装
`BattleEmulator.h`へ既存の武器攻撃力定数324／179／249を移し、エミュレータと探索で共有した。数値は変更していない。`ErugiosuSearch.cpp`は通常装備、素手、ガナンの入力表現・装備維持・切替・固定prefix・完全再生を扱う。ガナンは既存の`ACTION_GANANN`、表示は既存の`ganann`を利用する。素手とガナンの同時指定、および出力専用の装備変更markerを含む探索入力は拒否する。
MULTITHRUSTは、そのターンの合法な装備変更を適用した後の`defaultATK == GouketuEquippedATK`かつMP4以上の場合だけ生成する。ガナンや素手のままでは生成しない。ガナンから通常装備へ戻して同ターンにMULTITHRUSTを使う選択は、エミュレータの装備変更順に従い許可する。麻痺・眠り中は装備を維持し、攻撃へ正規化する枝だけを生成する。素手・ガナンの装備固定探索の評価でも、使えないMULTITHRUSTを将来火力として見積もらない。
`BattleEmulator::Main`にはFLEEが状態異常処理を無条件に飛ばす経路が残っていた。コマンド決定時に麻痺・眠りならATTACKへ正規化し、さらに敵先行で行動直前に状態異常を受けた場合もATTACKへ正規化して通常の状態異常処理へ入れる。敵専用技は探索候補に加えていない。
武器変更、派生能力値、ダメージ、状態異常、乱数消費は正規の`Main`の遷移を使う。武器変更のためにRNG cursorを人工的に進めたり、世界の能力値・ダメージ式・敵AIを調整したりしていない。
## 探索配分の調整
装備固定探索、通常装備＋素手を中心にする探索、ガナンを含む3装備探索を、同じ1500msの期限・同じ最良候補の下で組み合わせる。最初は幅48、以後は96から最大4096まで拡張し、従来の複数評価関数・末尾再探索を利用する。1回の探索passは最大220ms、終了処理用に20msを残す。
単純にすべての装備対応passを3装備へ広げると、`0x248218`でposition33／変更4回になり、2装備比較版のposition33／変更2回に劣った。採用版は後続passで2装備と3装備を交互に使い、この入力をposition33／変更2回へ戻した。固定した技列の武器だけを再探索する案も試したが改善がなく、採用していない。variant2／6／8等の比較結果も保存している。新しい固定技列やseed別の既知解は探索コードに入れていない。
## 最終実測
Linux GCC 14.2.0、CMake 3.31.6、Releaseで単一プロセスを逐次実行した。各入力は両方式とも1500ms。比較版はvariant10で、同じ合法性修正を適用し、ガナンへの新しい切替候補を生成しない。既にガナンである固定prefixの装備維持は許可するため、その入力を「ガナンを一切使わない比較」とは扱わない。
| seed | 固定prefix | 比較版position／変更回数 | 採用版position／変更回数 |
|---|---|---:|---:|
| 0x4a1ff18 | 31 | 34／2 | 34／2 |
| 0x248218 | なし | 33／2 | 33／2 |
| 0x17a83d | なし | 29／2 | 29／2 |
| 0x38f7e5 | なし | 31／2 | 31／2 |
| 0x3f0d2a | 65566 | 34／3 | 34／3 |
| 0x3b75ca | 262174 | 34／1 | 34／1 |
| 0x357dfd | なし | 31／2 | 31／2 |
| 0x2a031f | なし | 34／2 | 32／2 |
| 0x5a860 | なし | 25／2 | 25／2 |
| 0x35f647 | なし | 34／0 | 31／6 |
| 0x1cf2c2 | なし | 30／0 | 29／2 |
| 0x1a3b03 | なし | 37／0 | 37／0 |
12入力すべてで両方式が勝利。採用版はposition改善3件、同値9件、悪化0件。同positionの9件は装備変更回数もすべて同値だった。平均positionは32.167から31.667へ改善した。positionを短縮した入力では、第二目的の装備変更回数が増えても短いpositionの解を採用している。
採用12件の実測wall時間は1480.043～1485.597ms、最初の勝利は2.184～7.758ms。すべて`exact=1`、`prefix_same=1`、`rejected_replay=0`。全列再生と1ターンずつの再生を照合し、探索後にもCLIから別に再生して一致を確認した。これらは開発中の実測入力であり、独立した未使用holdoutとは称していない。有限時間探索で得られた最良解であり、数学的な最短性証明ではない。
`0x35f647`の採用列は以下。ガナンを含み、position31、変更6回、敵HP0で再生一致した。
```text
38,31,34,30,65598,62,262182,262206,62,262177,34
```
ガナンを使うこと自体は目的ではない。例えば`0x2a031f`は探索配分の変更でposition32を得たが、最終採用列にはガナンがない。`0x869e1`の通常SearchRequest実行ではposition25／変更0回を採用した。
## 本番経路と確認範囲
通常の`newDirectory_gouketu`は既存DEBUG3から既存SearchRequestを呼び、既定variant0／1500msをそのまま使う。CLI引数や手動パラメーター変更は不要。添付のDEBUG3入力`0x4a1ff18 / prefix=31`はposition34／変更2回で勝利した。元ZIPの同入力もposition34／変更2回だったが、元の既定実行時間は約1980ms、採用版は約1480msになった。
SearchRequestを直接呼び、ガナンを使用する勝利列、ガナン装備の固定prefix、装備変更0回の採用を確認した。`wasm_prepare_input → wasm_search_dump → buildDumpOutput → SearchRequest`については、既存のMINGW向け公開関数コンパイル経路を使ったネイティブ実行で確認した。ガナンのprefix入力も同経路で受理され、完全再生済みの勝利を返した。
**実WebAssemblyバイナリのビルド・実行は未確認。** この環境にはEmscriptenがなく、取得もDNSエラー等で失敗した。利用可能なローカルMCPのprefix一覧も確認したがCodespace等の別コンパイル環境は公開されておらず、ローカルのプロジェクト・エミュレータには変更を加えていない。ネイティブの公開関数実行を実Wasm検証として数えていない。
`webassembly/build.sh`は既存のexportを増やさず、引数なしで対象branchを選択し、未知branchのfallbackでもGOUKETUと採用探索をビルドするよう修正した。スクリプト自身がソースrootへ移動するため、呼出元ディレクトリに依存しない。大きなBattleResultを保持する呼出しに合わせ、スタック2MiBを明示した。WebAssemblyのGOUKETU variantも既存SearchRequestから同じ既定値へ接続する。SUPERはガナン装備worldではない既存の別variantとして残している。
## 梱包と再現
提出用ZIPを別ディレクトリへ展開し、採用ソース7ファイルのSHA-256一致、Releaseでのクリーンビルド、通常DEBUG3、SearchRequestのガナン使用入力とガナン装備prefixを確認した。結果は順にposition34／変更2回、position31／変更6回、position34／prefix後変更1回で、すべて勝利・完全再生一致。ZIPの圧縮データ検査も正常だった。
`optimization/Benchmark.cpp`は、添付CMakeに宣言済みだったが実ファイルが欠けていた実測入口へ追加した。入力をCLIで受け取り、探索・完全再生・既存SearchRequestを実行するためのもの。固定world、既知解、unit test、fixtureやテストsuiteは追加していない。メモリ整合性検査も実施していない。
```sh
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release -DBUILD_ERUGIOSU_BENCHMARK=ON
cmake --build build --target newDirectory_gouketu erugiosu_benchmark -j4
./build/newDirectory_gouketu
./build/erugiosu_benchmark --seed 0x35f647 --request
./build/erugiosu_benchmark --seed 0x3b75ca --prefix 262174 --request
./build/erugiosu_benchmark --seed 0x35f647 --variant 10 --dump
bash webassembly/build.sh
```
最後のWebAssemblyビルドには利用者側のEmscripten環境が必要。`--wasm-bridge`は公開関数経路の実測用であり、通常の本番利用には不要。実測ログ・比較・ソースhash・差分は`optimization/results/`、最終12入力と本番経路のログは`optimization/results/final/`に保存した。添付に含まれたGit履歴、既存探索variant、旧報告は削除していない。