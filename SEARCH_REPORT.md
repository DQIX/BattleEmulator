# シャルマナ探索実装・実測結果

対象は `silyarumana_new_arugo` / `silyarumana_v6`。実worldのcompile definitionsは `-Derusionn_lv21=1 -DMULTITHREADING=1`。添付ZIPの `BasePlayers` と戦闘処理を使用した。

## 採用方式

`SilyarumanaSearch.cpp` / `.h` に探索本体を分離した。採用方式はテンション重視の段階拡幅beam。幅は **256 → 2048 → 16384 → 65536**。全pass、prefix再生、勝利候補のfresh replayを同じ約1500msの予算に含め、終了処理用に20msを確保する。狭いpassで得た勝利を保持したまま広げ、frontierを一度も切らずに完走したpassは再実行しない。

分岐は実 `BattleEmulator::Main` を1ターンずつ実行する。状態にはPlayerの全semantic field、NowState、RNG cursorを保持し、同一状態だけをtranspositionで統合する。RNG cursorは目的関数にしない。勝利候補は元のPlayer、元のseed、変更していないprefixと探索suffixからfresh replayし、敵HP=0、終状態、RNG cursor、`BattleResult.position`、実装備変更回数が探索結果と一致した場合にのみ採用する。

候補比較の優先順位は **敵HP=0 → exact BattleResult.position最小 → 同positionならexact装備変更回数最小**。装備変更はfresh replayの `ACTION_EQUIPMENT_CHANGED` を数え、prefix内の変更も含める。最終dumpTableは、その採用済みfresh replayをそのまま使う。

## 戦法・prefix・制約

テンション、すてみ、武器装備状態のさみだれづきを主軸に、攻撃、防御、合法なFLEE、スカラ、ベホイミ、特やくそう、魔法の聖水、GOSPEL_SONGを探索する。さみだれづきとベホイミは実消費MP4、スカラはMP3を基準に分岐する。

マヌーサ中は、実際に使用できる必殺チャージがあればGOSPEL_SONGを候補へ入れる。自然解除までの経過と解除後の攻撃も実戦闘処理で評価する。MP不足にはMAGIC_WATERを含める。開始状態を正常状態に作り直さず、prefix終了時のマヌーサ、MP、チャージ、残りアイテムをそのまま引き継ぐ。

`-1`までのprefixは、既存のaction/log/RNGバッファ容量内で任意長として扱う。1手や3手専用の処理、prefix削除、seed別の回答、既知勝利列の埋め込みはない。休みを次ターンへ持ち越した状態では探索側で装備を固定し、FLEEを生成しない。ターン開始時に休みを持ち越していない場合まで装備変更を禁止することはしない。

BattleEmulator本体の変更は、持ち越し休み・睡眠・麻痺中のFLEEをATTACKへ正規化する6行だけ。既存の装備変更チェックを削除せず、追加の包括的validatorも入れていない。HP、MP、装備値、敵AI、ダメージ、RNG消費、状態異常処理は変更していない。

## 最終コードの実測例

Linux x86-64 / g++ 14.2.0 / `-O3`。すべて既存 `DEBUG3 -> SearchRequest` から実行。WASMやユーザーPC上の時間ではない。時間制限探索のため実行負荷によって返す行動列・品質は変わり得る。

| 入力 | prefix長 | prefix後の状態 | enemy HP | exact position | 実装備変更回数 | elapsed_ms |
|---|---:|---|---:|---:|---:|---:|
| 元DEBUG3、seed 0x1145 | 3 | HP81 / MP74 / マヌーサ | 0 | **22** | **2** | **320.618** |
| seed 0x1007 | 3 | HP36 / MP72 / 休み持ち越し | 0 | **37** | **2** | **1484.75** |
| seed 0x1145、MP不足prefix | 10 | HP88 / MP3 / マヌーサ | 0 | **52** | **8** | **1488.26** |
| seed 0x1145、必殺可能prefix | 5 | HP128 / MP49 / マヌーサ / 必殺可能 | 0 | **40** | **2** | **1484.65** |

上の4入力の `replay_rejected` はすべて0。MP不足prefixの勝利では16ターン目にMAGIC_WATER、必殺可能prefixの勝利では9ターン目にGOSPEL_SONGを使用した。装備変更回数8の内訳はprefix内6回、suffix内2回。入力prefixはサンプラーが実BasePlayersから合法な行動を再生して得たもので、HP/MPや状態異常を直接作ったものではない。サンプラーは提出物に含めていない。

追加の別seed 0x3145ではposition25 / 変更0 / 855.969ms。別入力として明示的に空prefixを与えた場合はposition22 / 変更2 / 1492.36msだった。空prefixの結果を、prefix付き入力の代わりとして使ってはいない。

全行動と戦闘ログは `SEARCH_BENCHMARK.txt` に記録した。

## 比較した方式と採用理由

テンションbeam、ダメージ・残り手数beam、状態別quota beam、これらの混成、2種のbest-first、勝利列からの1手/2手置換・削除による近傍修復、複数の拡幅配分を実測した。

持ち越し休み入力ではテンションbeamとダメージbeamがposition37を得た一方、混成・状態別quota・best-first・近傍修復の比較例はposition40だった。元DEBUG3のposition22、MP不足prefixの52、必殺可能prefixの40は、比較した変更では改善しなかった。

最終的に拡幅配分を256/2048/16384/65536へ調整した。休み持ち越し入力でposition37を発見する時点は、旧96/384/1536/6144/24576/65536の約1283ms・1,820,556展開から、約679ms・1,021,835展開へ前倒しできた。この発見時刻は計測用出力を加えた開発ビルドの値で、上表のelapsedは計測出力のない最終コードでの総時間である。

改善しなかったbest-firstと近傍修復の本体は最終コードへ残していない。最適性の数学的証明はしていない。

## 接続

```
DEBUG3 -> 既存SearchRequest -> SilyarumanaSearch::Run
通常Main -> SearchRequest -> SilyarumanaSearch::Run
WASM wasm_search_dump -> buildDumpOutput -> SilyarumanaSearch::Run
ActionOptimizer::RunAlgorithm / RunAlgorithmAsync -> SilyarumanaSearch::Run
```

SearchRequest内の旧TableA/TableB個別探索は置換した。通常Mainの失敗時に旧Dropbug引数だけ変えて同じ探索を2回実行する経路も除き、1要求の予算を二重に使わないようにした。DEBUG3の元seed/prefixとSearchRequest経由は維持している。

CMake共通SOURCESと `webassembly/build.sh` のSRC_FILESへ新規探索ファイルを追加した。対象variant、compile definitionsは変更していない。比較用CLIは `--search-variant 0` が採用方式、`1` が同じテンション評価、`2` がダメージ/残り手数評価、`3` が状態別quotaである。WASMと通常のSearchRequestは採用方式0を使う。

## 実行方法

ソースのルートで、実WASM対象と同じvariant定義を付けてネイティブビルドする。

```bash
g++ -std=c++20 -O3 -Derusionn_lv21=1 -DMULTITHREADING=1 \
  main.cpp lcg.cpp BattleEmulator.cpp camera.cpp debug.cpp \
  ActionOptimizer.cpp SilyarumanaSearch.cpp \
  EnhancedCostCalculator.cpp EnhancedHashCalculator.cpp EnhancedHeapQueue.cpp \
  InputBuilder.cpp -o silyarumana_search

./silyarumana_search
./silyarumana_search --seed 0x1007
./silyarumana_search --prefix '65598,65561,36,65572,36,62,36,65572,30,30'
./silyarumana_search --prefix '65563,65561,65572,65586,36'
```

`--budget-ms` で比較用の総予算を指定できる。既定値は1500ms。通常のCMakeビルド定義にもソース追加を反映したが、上記実測はこのO3コマンドによる。

WASM用の既存コマンドは以下。

```bash
bash webassembly/build.sh --branch silyarumana_new_arugo
```

**WASMの実ビルド・ブラウザ実行は未確認。** ビルドを実際に試行したが、この作業環境にはEmscriptenがなく、対象 `silyarumana_v6` を選択したところで `emcc: command not found`（exit 127）となった。接続とソース追加は実施済みだが、WASMバイナリの生成成功やブラウザ上1500msの性能を確認済みとは扱わない。

提出ZIPには元ソース一式（.gitを除く）、新規探索2ファイル、この報告、実測ログを含める。テストスイート、sanitizer、メモリ整合性検査は追加・実行していない。
