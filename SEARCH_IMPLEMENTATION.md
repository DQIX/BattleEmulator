# 石の番人・探索実装
## 採用方式
標準はvariant 4 `beam-and-bounded-dfs`。幅512のビームで勝利候補を得た後、攻撃・テンション・すてみの楽観的ダメージ上限を用いた深さ制限付き分枝限定DFSで、より小さい結果positionと、同positionでより少ない装備変更を探索する。全工程で単一の約1500ms予算を共有する。探索が終われば早期に返す。
探索本体は`IsinobanninnSearch.cpp`と`IsinobanninnSearch.h`。`ActionOptimizer.cpp`は既存APIからの接続のみ。
## 評価と固定prefix
勝利はfresh exact replay後の敵HPが厳密に0であること。第一評価値は`BattleResult.position`、同値の場合の第二評価値は`ACTION_EQUIPMENT_CHANGED`付き味方ログの件数。LCGのcursorは評価値に使用しない。
`aActions[350]`の`-1`までを固定prefixとして保持する。呼出し側の旧turn数やgeneration数でprefixを置き換えない。探索はprefixの実行後の状態からのみ開始する。
各枝は既存BattleEmulatorの1ターン実行で生成する。採用候補は元のPlayer・seed・固定prefix・探索suffixから新たに一括Main replayする。全Playerフィールド、内部state、LCG cursor、実ログ件数、装備変更件数を探索状態と比較し、不一致なら採用しない。133ターンを超える列は既存BattleResultの400件容量に合わせてreplayを分割する。
装備変更は実際の結果ログで数える。さみだれづきはMP4以上かつ装備ありの枝でのみ生成する。ベホイミはMP4、BUFFはMP3。休み持越し中はFLEEと装備切替を候補にしない。既知勝利列、seed別回答、prefix別回答は実装に含まない。
## 同条件の旧版比較
Linux x86-64、GCC 14.2.0、CMake Release、`-DDEBUG3 -DDEBUG`。既存DEBUG3のseed `0xc5a8c4e`、固定prefix `[30]`をそのまま使用した単回測定。
| 実装 | 敵HP | exact position | 実装備変更 | 総ターン | 探索時間 |
|---|---:|---:|---:|---:|---:|
| 添付旧版 | 0 | 19 | 4 | 7 | 1094.84 ms |
| 採用版 | 0 | 19 | 0 | 7 | 232.82 ms |
採用版のこの実行の全行動列は`30, 62, 62, 62, 33, 62, 34`。先頭の30は入力された固定prefixであり、探索による変更はない。この行動列は測定結果としてのみ記載し、探索コードには埋め込んでいない。
旧版のLCG cursorは315、採用版は323。これらは結果positionの19とは別の値であり、cursorを最小化する比較はしていない。
## 探索方式比較
variant 0は幅512・4096とbest-firstのportfolio、variant 1はbalanced beam幅2048、variant 2はoffensive beam幅8192、variant 3はweighted best-first、variant 4は小幅beam＋bounded DFS。開発中に各構成を実測した。
同じ入力に対する方式比較では、幅8192への拡大やbest-firstでもexact position・装備変更回数の追加改善はなかった。variant 4は同等の解を短時間で返したため標準に採用した。数学的な最適性は主張しない。
## 採用版の別入力測定
すべて敵HP0、fresh replay不一致0件。時間は探索内計測の単回値であり、CPUやビルド条件に依存する。
| seed | 固定prefix | exact position | 実装備変更 | 総ターン | 探索時間 |
|---|---|---:|---:|---:|---:|
| `0x307077` | `30` | 16 | 0 | 6 | 50.24 ms |
| `0x315e07` | `30` | 16 | 2 | 6 | 53.24 ms |
| `0xc5a8c4e` | `30,62,33` | 19 | 2 | 7 | 10.44 ms |
| `0x2a7519` | `30,65598,65598,33` | 22 | 2 | 8 | 6.28 ms |
## 接続と変更範囲
`SearchRequest -> ActionOptimizer::RunAlgorithm -> IsinobanninnSearch::Run`を接続した。既存DEBUG3はSearchRequest呼出しを維持し、引数なし実行で標準variant 4を確認済み。
`buildDumpOutput -> ActionOptimizer::RunAlgorithm`も同じ標準variant 4を利用する。表示は採用候補のfresh replayから作成し、敵HP・ログ件数・装備変更件数・cursor・stateを確認する。未勝利を成功表示しない。
CMakeの共通ソースへ新規探索ファイルを追加した。`webassembly/build.sh`の変更はソース一覧への新規cpp追加だけで、既存のbranch/variant定義は変更していない。添付版には`isinobanninn`用のWebAssembly定義がなくdefaultへ落ちるが、この整合性修復は実施していない。emccのない環境のためWebAssembly実ビルド・ブラウザー実行は未実施。ネイティブのCMakeターゲット`isinobannninn_new_arugo`はビルド・実行済み。
`BattleEmulator.cpp/.h`、`BattleResult.h`、`Player.h`、`lcg.cpp`、`camera.cpp`、`Equipment.h`は添付版とバイト単位で一致する。戦闘処理・乱数・装備値・既存検証を変更していない。FLEEの世界側修正、単体テスト・テスト基盤の追加、サニタイザは行っていない。
ZIPはソース一式であり、`.git`とビルド生成物は含めない。
## 再実行
既存CMakeターゲットをReleaseでビルドする。DEBUG3を有効にしたビルドは引数なしで既存入力を実行する。通常ビルドからも汎用benchmark引数で同じSearchRequestを呼べる。
```text
isinobannninn_new_arugo --search-benchmark 0xc5a8c4e 4 1500 30
isinobannninn_new_arugo --search-benchmark 0xc5a8c4e 1 1500 30
isinobannninn_new_arugo --search-benchmark 0xc5a8c4e 2 1500 30
isinobannninn_new_arugo --search-benchmark 0xc5a8c4e 3 1500 30
```
引数の順序はseed、探索variant、総予算ms、固定prefixの各action。CLI自身が末尾の`-1`を付ける。勝利時の終了コードは0、予算内の勝利なしは2、CLI引数エラーは1。元のtrace経路も保持している。