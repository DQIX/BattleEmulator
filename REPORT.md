# 過去の報告：ガナン対応前
本ZIPの最新状態・採用パラメーター・実測値・未確認範囲は`GANANN_REPORT.md`を参照すること。以下は添付ZIPに含まれていた旧報告を履歴として保存したもので、今回の実測を表すものではない。
# エルギオス2 装備変更対応探索 実装・実測報告
## 実装
対象は添付`erugiosu_new_arugo`の`GOUKETU=1`経路。新しい`ErugiosuSearch.cpp/.h`を追加し、既存シグネチャを維持した`SearchRequest`から呼び出す。通常の`newDirectory_gouketu`が新探索を使用する。旧A*、OPTIMIZE_MODE、SUPER向けの既存処理は削除していない。
最終比較は「正確な勝利 → exact replayのBattleResult.position → prefix後に実際に発生した装備変更回数」。ターン数やRNG cursorを第一目的にはしていない。prefix中の装備変更は固定された過去として別集計する。
探索器は幅を段階的に広げるbeam、正確な状態比較付きflat hash、戦術phase別保持枠、複数評価関数のportfolio、現在の探索で得た解の末尾再探索を組み合わせる。経路はarena形式で保持し、actionは装備bitを失わないint32_tで格納する。seed別回答、固定開始列、既知最短解のlookupはない。
最初に装備固定laneを実行し、装備対応laneと同じ1500msの予算内で競わせる。後続passにも装備固定laneを配分する。装備固定とはrootの装備を維持することであり、rootが素手なら素手のままである。各passを最大220msに区切り、全体の停止時刻は終了処理用20msを除いた約1480msとする。時間切れでも既にexact replayを通過したbestを保持する。
## 合法性と正確性
各枝は正規の`BattleEmulator::Main`を1ターン実行する。装備維持／切替は通常actionと独立に展開し、1ターンに最大1回、麻痺・眠り中は装備維持のみとする。入力へbit17を設定しない。装備状態、defaultATK、派生atk、全Playerメンバー、NowState、RNG cursor、ログpositionを含む意味的な完全一致を確認してから状態を統合する。hash一致だけで統合しない。
採用前に初期Playerと固定prefixから全行動列を再生する。全列Mainと1ターンずつの再生で、敵味方HP、MP、全補助状態、RNG、NowState、ログpositionが一致した候補だけを採用する。さらに実測ドライバが呼出し後に独立した再実行を行い、prefix不変と最終状態一致を確認する。
装備変更回数は実際のターン前後のdefaultATKの変化から集計する。素手bitの個数ではない。最終ターンの敵先行メラゾーマ反射で敵が倒れ、味方のログが出ないケースでも実際の装備変更を取り落とさない。dumpTableの`sude`／`on`表示は既存実装のまま維持した。
探索対象は実装された味方技20種類。攻撃、ためる、ミラー、すてみ、スカラ、さみだれづき、回復、防御、大ぼうぎょ、フバーハ、ひっさつ、回復アイテム、合法状態の逃げを含む。一閃づき、疾風づき、きゅうしょづき、敵専用技、未実装enumは生成しない。ターン開始時の麻痺・眠りだけでなく、敵先行で新規麻痺が入るFLEEのskip悪用も拒否する。
世界側10ファイルは添付とバイト単位で一致する。HP、MP、攻撃力、装備攻撃力324／179、AI、反射・ダメージ式、乱数、カメラ、状態異常、勝利条件は変更していない。
## 戦術と参照元
Cローテーション中のミラー維持、反射ダメージとテンション蓄積の同時進行、高テンションさみだれづきの将来価値を評価する。生存に必要なスカラ、フバーハ、回復、MP／アイテムも評価するが、どの技も固定解として強制しない。合法技は全展開し、状態選択の優先度を評価関数とphase枠で調整する。
`reokonn-GPT6-Pro`、`bilyouma-gpt6-pro`、`yo2-gpt6-pro`、別添optimizationの探索技術を読み、組み合わせた。参照元のcommitと適用箇所は`optimization/PROVENANCE.md`、参照ソースは`optimization/donors/`に保存した。値関数はエルギオス2用の手設計と開発入力による比較であり、機械学習を実施したという主張ではない。
## 開発比較
全variantで予算1500ms、同一初期世界・同一prefixを使用した。A〜F相当のvariantに、burst、防御反射、実遷移による攻撃previewを加え、計10種類を実行した。下表は全10種類を比較した共通の生存root6件の平均で、全件勝利・独立再生一致である。
| variant | 内容 | 平均position | 平均装備変更回数 |
|---|---|---:|---:|
| 0 | 採用：複数評価関数＋末尾再探索 | 34.333 | 1.667 |
| 1 / A | 装備変更禁止portfolio | 36.000 | 0.000 |
| 2 / B | 装備対応・balanced | 34.333 | 1.667 |
| 3 / C | 装備固定・反射テンション重視 | 36.000 | 0.000 |
| 4 / D | 装備対応・反射テンション重視 | 34.500 | 1.667 |
| 5 / E | 参照探索器のdamage-valueを適応 | 35.167 | 1.667 |
| 6 / F | 合法技全展開multi-value portfolio | 34.333 | 1.667 |
| 7 | burst重視 | 35.000 | 1.000 |
| 8 | 防御反射重視 | 34.333 | 1.667 |
| 9 | 正規Mainによる攻撃preview | 34.500 | 1.667 |
追加4件を含む生存root10件では、variant0と6は同じposition／装備回数となった。variant0は装備固定に対して7件でpositionを改善、3件は同値かつ変更0回、悪化0件。平均は装備固定39.300、採用34.600。ただし2件は素手rootなので、その改善幅を通常装備rootの改善率と混同しない。
`dev07`はprefix終了時点で味方HP0、`dev08`は2ターン目にHP0となった後の3ターン目もprefixに含むため拒否された。これらは勝利可能な現在状態の比較から外し、元の入力と結果を削除せず保存した。prefixを書き換えて救済してはいない。
原データは`optimization/results/development-a.csv`、`development-b.csv`、`development-c.csv`、各ケースのdump、集計CSVにある。初回の別seed`0x17a83d`は装備固定position34・変更0回に対し、採用はposition29・変更2回・10ターンで勝利した。代表dumpは`optimization/results/initial/`にある。
## 独立holdout
開発比較後に探索ソースと本番接続を固定し、`optimization/frozen-source-sha256.txt`と固定時刻を保存した。その後、新規の非zero 22bit seed16件を生成し、初期状態8件、2ターンのスカラ＋ミラーprefix4件、素手スカラprefix4件でvariant0と装備固定variant1を比較する。holdoutを見た再調整はしない。
16件中、`hold10 / 0x117842 / prefix=30,31`はprefix終了時点で味方HP0だった。復活やprefix変更は行わず、両variantとも勝利なしで終了した。残る生存root15件は両variantとも全件勝利し、採用解の独立再生15件すべて一致、rejected_replay=0だった。全入力での勝利数は15/16、生存rootでの勝利数は15/15と区別する。
| 生存rootの種類 | 件数 | 装備固定の平均position | 採用の平均position | 採用の平均装備変更回数 |
|---|---:|---:|---:|---:|
| 戦闘開始状態 | 8 | 32.625 | 30.625 | 1.750 |
| スカラ＋ミラー後、装備あり | 3 | 40.000 | 39.000 | 1.333 |
| 素手スカラ後 | 4 | 51.250 | 34.250 | 2.500 |
| 合計 | 15 | 39.067 | 33.267 | 1.867 |
採用は装備固定より12件でpositionを改善、3件で同値、悪化0件。同値3件では装備変更0回を採用した。素手rootの4件は装備へ戻す操作も探索対象になるため、通常装備rootとは分けて示した。
| case | seed | prefix | 装備固定position | 採用position | prefix後の装備変更回数 |
|---|---|---|---:|---:|---:|
| hold01 | 0x8878 | なし | 32 | 29 | 2 |
| hold02 | 0x2b4dfa | なし | 33 | 31 | 2 |
| hold03 | 0x386c6d | なし | 37 | 33 | 2 |
| hold04 | 0x869e1 | なし | 25 | 25 | 0 |
| hold05 | 0x21d23f | なし | 34 | 32 | 2 |
| hold06 | 0x1ceba3 | なし | 34 | 33 | 2 |
| hold07 | 0x33d860 | なし | 34 | 31 | 2 |
| hold08 | 0x375f1c | なし | 32 | 31 | 2 |
| hold09 | 0xb9ed8 | 30,31 | 40 | 37 | 4 |
| hold10 | 0x117842 | 30,31 | prefixで死亡 | prefixで死亡 | — |
| hold11 | 0x1ca8eb | 30,31 | 43 | 43 | 0 |
| hold12 | 0x187d46 | 30,31 | 37 | 37 | 0 |
| hold13 | 0x39 | 65566 | 52 | 35 | 1 |
| hold14 | 0x2ad491 | 65566 | 50 | 34 | 5 |
| hold15 | 0x35a376 | 65566 | 51 | 34 | 1 |
| hold16 | 0x3b75ca | 65566 | 52 | 34 | 3 |
採用variantの探索呼出しwall時間は1480.016〜1483.754ms、最初のexact勝利は1.569〜4.989ms、平均2.969msだった。数値はこのLinux環境での実測であり、他のCPUやWebAssemblyで同一の展開量・結果になる保証ではない。`hold13`では最終改善が1464.815msに発生しており、早期の勝利確保後も予算内で改善している。
原データは`optimization/holdout.tsv`、`optimization/results/holdout.csv`、`holdout-comparison.csv`、`holdout-summary.csv`と、`optimization/results/holdout/`の各dumpに保存した。固定後のソース照合結果は`optimization/results/source-frozen.txt`にあり、holdout後も一致している。
## 本番接続と代表的な完全再生
実際の`SearchRequest`経路を3種類の入力で実行した。初期状態`0x17a83d`はposition29・変更2回、装備ありprefix`0x11d983 / 30,31`はposition40・変更4回、素手prefix`0x3f0d2a / 65566`はposition34・prefix後変更3回で勝利した。すべてreplay_verified=1で、prefixは出力列の先頭にそのまま残っている。出力は`optimization/results/searchrequest-*.txt`に保存した。
代表の`0x17a83d`は、ミラーを維持して反射しながらテンションを蓄積し、8ターン目に素手へ、9ターン目に装備へ戻し、すてみ後の高テンションさみだれづきと反射で撃破する。固定解として埋め込んだものではなく、この実行で探索された結果である。全列は`36,31,30,62,31,62,62,65598,33,34`。別途`--replay`だけでも全列を実行し、position29・敵HP0・装備変更2回を確認した。`optimization/results/representative-exact-replay.txt`に完全なdumpと最終状態がある。replay-onlyモードのroot欄は探索rootを作らないため未設定であり、最終状態はfinal欄を参照する。
## 再現方法
実測環境はLinux、GCC 14.2.0、CMake 3.31.6、Release。単一プロセス・逐次実行で比較した。Windows／MinGWおよびWebAssemblyの実機ビルド・実行は今回実施していない。WebAssemblyの既存exportは増やさず、既存build.shに新ソースとビルド定義だけを追加した。
```sh
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release -DBUILD_ERUGIOSU_BENCHMARK=ON
cmake --build build --target newDirectory_gouketu erugiosu_benchmark -j4
./build/erugiosu_benchmark --seed 0x17a83d --variants 0,1 --budget-ms 1500 --dump
./build/erugiosu_benchmark --cases optimization/holdout.tsv --variants 0,1 --budget-ms 1500 --output results
./build/erugiosu_benchmark --seed 0x3f0d2a --prefix 65566 --request
./build/erugiosu_benchmark --seed 0x17a83d --replay --actions 36,31,30,62,31,62,62,65598,33,34 --dump
```
`--request`は既存SearchRequestインターフェースを実行する。通常の`--variants`は各variantの実際の探索を呼び、独立再生も行う。出力CSVのequipment_changesはprefix後、total_changesはprefixを含む。positionは全戦闘ログのpositionである。first_msは最初に完全再生を通過した勝利の時間、best_msは最終採用解が見つかった時間。elapsed_msとwall_msは探索呼出しの時間で、呼出し後の独立再生とdump出力は含まない。
探索期間は既定で戦闘開始から60ターンまで、実測CLIの`--max-turns`とRun引数で最大99まで指定可能。添付RNGキャッシュの上限を変えず、cursor6500以上の追加展開は行わない。非zero seedを入力条件とする。これらの実装上の範囲は最短性証明ではない。
## 完了範囲
正しい世界での有限時間探索、装備固定比較、prefix保持、完全再生、dump、参照variant、開発・holdout原データを提出する。新規unit test／回帰テスト／テストフレームワークやsanitizerは追加していない。数学的な最短保証や全seed勝利保証は行わない。複数の有望variantが同水準に収束し、十分な成果が得られたため、追加の探索方式実験を終了して提出する。