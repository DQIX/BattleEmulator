# gadonnko_new_arugo 探索実装
## 本番経路
`SearchRequest` → `GadonnkoSearch::Run` → fresh exact replay → `dumpTable`。
`DEBUG3`は同じ`SearchRequest`を呼ぶ。WebAssemblyも既存の`wasm_search_dump` → `buildDumpOutput` → `SearchRequest`を維持する。新しいexport、WebAssembly専用探索器、接続用の別ラッパーは追加していない。
`buildDumpOutput`で未使用だった`ResultStructure`引数を削除した。これにより、呼び出し前に未格納の`wasmResults[resultIndex]`を参照していた箇所もなくなる。公開exportの引数と名前は変更していない。
## 採用方式
装備変更なし／装備変更ありの探索を同じ1500ms予算内で実行する、可変幅beam＋複数評価portfolio＋suffix repair。小さい幅から勝利を確保し、残り時間で幅・評価・suffixを変えて改善する。終了処理用に20msを確保し、探索中も時刻を確認する。
ReokonnSearchの親リンクによる行動列復元とreplay確認、BilyoumaSearchの幅拡大とrepair、ErugiosuSearchの装備探索・状態照合の構造を参考にした。他bossのPlayer値、world、action集合、既知解、seed別回答は移植していない。
状態には両Player、NowState、RNG位置、BattleResult.position、実際の装備変更回数を保持する。hash一致後に全Playerフィールドと状態を比較する。同一の将来状態・同一positionでは装備変更回数が少ない経路を残す。スコアは順序付けだけに用い、world stateには書き込まない。
候補は初期worldから全列をfresh replayし、別途1ターンずつ実行した状態、および探索内の状態と照合してから採用する。勝利条件は敵HP==0。最終比較はexact BattleResult.position、全列のexact装備変更回数の順であり、turnを第一目的にはしない。
## actionとprefix
旧ACTION_TABLEのMIDHEAL、SPECIAL_MEDICINE、FLEE_ALLY、DOUBLE_UP、PSYCHE_UP_ALLY、BUFF、MULTITHRUST、DEFENCEを調査し、すべて候補に残した。既存InputBuilder/Mainでも使用する基本行動ATTACK_ALLYを追加した。
旧探索のHP70%未満、敵HP180超、攻撃上昇必須といった探索フィルターは世界の合法性とは区別して外した。MP条件は現在のBattleEmulator実装に合わせ、BUFFは3、MIDHEALとMULTITHRUSTは4とした。槍技MULTITHRUSTは装備状態でのみ生成する。使用回数がない薬草は生成しない。
麻痺・睡眠・休み状態では装備切替を生成しない。これらの状態では、現在装備を維持したATTACK入力で既存の状態異常処理を進める。FLEEはターン開始時だけでなく、敵先行で同ターン中に受けた麻痺・休み等を既知のskip処理で無視する遷移も除外する。BattleEmulator本体のFLEE処理は変更していない。
`aActions[350]`の`-1`までを長さに依存しない固定prefixとしてそのままreplayし、その終了状態からだけ探索する。prefix内の装備変更も全列の変更回数に含め、ログにはsuffix分も別に出す。prefixを消したり、変更したり、再探索したりしない。
探索horizonはprefix後40ターンまで。元のRNG配列5000件・BattleResult配列400件を越えない範囲で実行する。元の固定配列でreplayできないprefixは、切り捨てず失敗として返す。
## 実測
Linux x86-64、GCC。先頭2行は旧版・新版とも`-O3 -DNDEBUG`のRelease条件で比較した。既存DEBUG3と同じ入力を使用し、旧版も添付ソースから別途ビルドした。先頭2行の時刻はDEBUG3外側の探索全体の実測であり、初回勝利時刻を総実行時間として扱っていない。新版のSearchRequest内部計測は1480.12ms。prefixなし・素手prefix・追加seedの探索比較は`-O3`（assert有効）で実行し、SearchRequest内部の時間を記録した。
| 入力 | 探索 | 敵HP | exact position | 装備変更回数（全列） | 時間 |
|---|---|---:|---:|---:|---:|
| seed=0x0ac040ac、prefix=62,62 | 添付の旧SearchRequest・Release | 0 | 13 | 2 | 1542.83ms |
| 同じ入力 | 新探索・最終CMakeビルド | 0 | 13 | 0 | 1480.23ms |
| 同じseed、prefixなし | 新探索 | 0 | 13 | 0 | 1480.29ms |
| 同じseed、prefix=65598,65566,65569 | 新探索 | 0 | 15 | 2（prefix内1＋suffix内1） | 1480.23ms |
最後の素手prefixでは、prefix後に装備を変更しない探索でも撃破を確認したがposition=57だった。装備し直す1回を許可してposition=15となり、第一目的でこちらを採用した。
追加のseed 0x13579b、0x2468ac、0x123456、0x314159、0x271828（prefix=62,62）もすべてfresh replayで敵HP==0。positionは順に13、13、15、13、13、全列の装備変更はすべて0回、実測は約1480～1483msだった。
比較したvariantでは代表入力のposition=13をさらに短縮できず、装備変更0回を維持する既定portfolioを採用した。数学的な最適性は証明していない。計測ログのhash_collisionsは省略フィールドを持つfingerprintの一致後に、完全状態比較で区別した件数であり、状態を誤って統合した件数ではない。
## 確認した範囲
CMake Releaseで既存2ターゲットのビルド成功。DEBUG3のSearchRequest実行、prefixなし・通常prefix・素手prefix、評価方式比較、候補のfresh replayを実施した。
元からあるMINGW_BUILD用のネイティブexport経路を共有ライブラリとしてビルドし、実際の`wasm_prepare_input("at 38 at 37")`と`wasm_search_dump(0, 0x0ac040ac, 1, 0)`を実行した。新探索のposition=13、装備変更0回、prefix保持、replay=verifiedを返すことを確認した。これはネイティブでのexport経路確認であり、WebAssembly実行の実測ではない。
`webassembly/build.sh`へ新ソースを追加し、bash構文も確認済み。環境にemccがなく、導入先のDNS解決もできなかったため、実際の.wasm生成・ブラウザー実行は未検証。
BattleEmulator.cpp/.h、Player.h、BattleResult.h、lcg.cpp/.h、camera.cpp/.h、Equipment.h、InputBuilder.cpp/.h、ActionOptimizer.cppは添付ZIPとSHA-256が一致する。main.cppのBasePlayersも変更していない。新規テストスイートやサニタイザ実行は追加していない。
## 実行
```sh
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release -Ddebug3=ON
cmake --build build --config Release --target ebe_lv21_v2
./build/ebe_lv21_v2
./build/ebe_lv21_v2 --seed 0x123456 --prefix "62,62"
./build/ebe_lv21_v2 --prefix "65598,65566,65569"
./build/ebe_lv21_v2 --prefix "" --budget-ms 1500 --variant 0
```
Windowsのmulti-config generatorでは実行ファイルは通常`build/Release/ebe_lv21_v2.exe`。通常入口へ戻す場合は`-Ddebug3=OFF`でビルドする。DEBUG3の無引数入力は元のまま。
## 比較用variant
比較した実装は削除せず残してある。0=採用portfolio＋repair、1=装備変更なしportfolio、2=burst、3=survival、4=offensive、5=exact attack preview、6=phase quotaなし。2～6も初回に装備変更なしの候補を確保する。すべて同じSearchRequest、同じworld、同じreplay条件を通る。
## 変更ファイル
新規`GadonnkoSearch.cpp/.h`、接続・直接診断用引数の`main.cpp`、ソース追加とdebug3 optionの`CMakeLists.txt`、ソース追加の`webassembly/build.sh`。旧探索は参照・既存parameter optimizer用にそのまま残し、本番SearchRequestからは呼ばない。