# gilyumei1 / gilyumei2_rubii 新探索実装
## 実行経路と本番設定
`DEBUG3 → SearchRequest → GilyumeiSearch::Run`、通常の`SearchRequest → GilyumeiSearch::Run`、`wasm_search_dump → buildDumpOutput → SearchRequest → GilyumeiSearch::Run`に接続した。SearchRequestは旧`ActionOptimizer::RunAlgorithm`を呼ばない。旧探索へのfallback・旧探索とのruntime portfolioはない。
本番の呼び出しは予算・variantを省略し、`GilyumeiSearch.h`の共通既定値`ProductionBudgetMs=1500`、`ProductionVariant=0`、`ProductionWidth=96`を自動使用する。variant=0は新探索内のheuristic切り替えであり、旧探索への切り替えではない。探索は出力・解放用に5msを残し、幅を96から最大3072へ広げる。検証用の環境変数や特別な探索フラグを本番で設定する必要はない。
## 探索方式
実BattleEmulatorで1ターンずつ展開する、幅を段階的に広げるbeam探索を実装した。装備変更なし・装備変更ありの探索を交互に行い、共通の時間予算と勝利候補を共有する。テンション・強化・行動不能状態ごとの候補枠を残し、複数の評価関数と発見済みsuffixの末尾repairを使用する。与えられたprefixをrepair対象にはしない。
状態のハッシュ一致後には両Playerの全メンバー、nowState、RNG位置、結果positionを実比較する。同じ将来状態・同じpositionの場合だけ、装備変更回数の少ない経路を残す。ハッシュ一致だけで異なる状態を併合しない。
勝利候補は元のPlayer状態とseedからprefix・suffixを再実行する。ターン単位の実行と独立した連続実行の終状態、RNG位置、position、実装備変更回数を照合し、一致した敵HP=0の候補だけを採用する。比較順序は`BattleResult.position`、実装備変更回数であり、ターン数ではない。dumpTableへ渡すBattleResultもこのfresh replayのものを使用する。
## 候補技・装備・prefix
旧候補集合の技は調査したうえで維持した。通常攻撃は自由選択候補に入れず、既存worldの行動不能ターンの内部placeholderとしてのみ使用する。prefix内の通常攻撃は勝手に削除しない。MULTITHRUSTは当該ターンの装備requestがEquippedATKになる場合だけ生成する。
旧探索の`mp >= 10`や攻撃強化段階・HP比率による枝削減を使用条件としてコピーしていない。実処理の消費に合わせ、MULTITHRUSTはMP4、DEFENDING_CHAMPIONとBUFFはMP3、MIDHEALはMP4、MORE_HEALはMP8、FULLHEALはMP24などを候補生成で確認する。道具の残数と必殺チャージの使用可能状態も確認する。
麻痺・睡眠・ターン開始時からの休み持越しでは装備変更枝を生成しない。休みでなく開始し、ターン途中で休みを受けるケースは、worldのターン開始時の装備処理に従う。FLEEが敵先行の休み付与をすり抜ける既存挙動を探索解で利用しないよう、該当遷移を探索側で除外した。worldのFLEE実装そのものは変更していない。
装備変更回数はrequest数ではなく、実行前後のdefaultATKの変化を数える。生存したreplayでは実ログの装備変更フラグ数とも照合する。RUBIIのganannを含め、装備値はworldに追加した読み取り専用`equipmentAttack`から取得する。
prefixは任意長を既存350要素配列の終端まで読み、変更せず再実行する。prefix中に死亡した場合はそこから探索を開始し直さず、未勝利を返す。prefixですでに撃破している場合は追加行動なしでそのfresh replayを返す。
## 実測結果
測定環境はLinux x86_64、Intel Xeon Platinum 8573C、GCC 14.2.0、CMake 3.31.6、Releaseビルド。時間は当該コンテナでの実測値であり、他のCPU・Windows・ブラウザで同じ展開数や解を保証するものではない。
次の表は既存DEBUG3の7 seed、共通prefix=`30`での比較。旧値は入力ZIPの既存探索が報告した最良position、新値は新探索のfresh exact replayのposition。旧探索と新探索の時間配分を同一化した比較ではない。括弧内は新解の実装備変更回数。
| seed | gilyumei1 旧→新（変更回数） | gilyumei2_rubii 旧→新（変更回数） |
|---|---:|---:|
| 0x4a1ff382 | 28→22（2） | 25→22（2） |
| 0x4a1ff383 | 31→28（2） | 25→22（4） |
| 0x4a1ff384 | 22→19（2） | 16→16（0） |
| 0x4a1ff388 | 22→19（4） | 19→16（2） |
| 0x4a1ff38c | 25→22（2） | 22→19（2） |
| 0x4a1ff3aa | 28→25（2） | 22→22（0） |
| 0x4a1ff3c8 | 22→22（0） | 28→25（2） |
14件すべて撃破し、11件でposition短縮、3件で同値。新探索の実測は1494.15～1499.24ms。候補のfresh replay不一致は0件。数学的な最適性は証明していない。
別seed・prefix長0～5の入力を両targetで計20件測定した。prefix後に生存していた13件はすべて撃破した。残り7件はprefix再実行で既に味方HP=0であり、入力自体は有効、探索展開は0件、未勝利として返した。20件すべてでprefixは保持され、fresh replay不一致は0件だった。
既存WASM export関数のネイティブshimでも4件を実行した。gilyumei1のprefix長1・5、およびRUBIIの空prefix・ganann付きprefixで、既存parserからSearchRequestへ到達し、`budget_ms=1500 variant=0`で撃破した。ganann付きprefix=`262174`、seed=`0x4a1ff384`ではposition=16、全体の実変更回数=2、suffixでの変更回数=1だった。
撃破済みprefixの確認では、gilyumei1のseed=`0x4a1ff384`、prefix=`30,62,62,65598,33,34,34`を保持し、追加展開0、suffix変更0、position=19のfresh replayを返した。これらの入力は測定用ファイルだけに置き、探索実装に答えやseed別分岐を入れていない。
## WebAssemblyの確認範囲
`webassembly/build.sh`は新探索sourceを含み、引数なしでgilyumei1とRUBIIの正しいworld定義を付ける。実行ディレクトリに依存せず、明示2MiBのスタックと本番共通パラメーターを使用する。既存export関数名は変更・追加していない。shell構文確認は成功した。
この環境には`emcc`がなく、実ビルドは`emcc: command not found`、終了コード127で停止した。そのため実WASM生成、ブラウザでの起動・探索時間は未検証。上記のネイティブexport確認は実WASM実行の代用として成功扱いしていない。ZIPに新しいビルド済みWASM/JavaScriptは含めていない。
## ビルドと実行
通常のネイティブビルドとSearchRequest入口は次のとおり。
```sh
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build --config Release --target gilyumei1 gilyumei2_rubii
./build/gilyumei1 --search 0x4a1ff384 30
./build/gilyumei2_rubii --search 0x4a1ff384 30
```
MSVCなどのmulti-config generatorでは実行ファイルは通常`build/Release/`内になる。既存の対話入口も維持した。`--search`は既存SearchRequestへの直接入口であって、別探索器ではない。
既存DEBUG3を有効にする場合は次のとおり。探索の本番パラメーター変更は不要。
```sh
cmake -S . -B build-debug3 -DCMAKE_BUILD_TYPE=Release -DBATTLE_DEBUG3=ON
cmake --build build-debug3 --config Release --target gilyumei1 gilyumei2_rubii
./build-debug3/gilyumei1
./build-debug3/gilyumei2_rubii
```
Emscripten環境での既存WASMビルド入口は次のとおり。
```sh
bash webassembly/build.sh
```
任意の比較測定用driverは本番にはリンクされず、次の設定でのみ追加される。stdin形式は`seed|prefixのaction列`。引数省略時も本番パラメーターを使用する。`bridge`は既存exportのネイティブshim実行、`dump`はfresh replayの表を追加出力する。
```sh
cmake -S . -B build-measure -DCMAKE_BUILD_TYPE=Release -DBATTLE_SEARCH_BENCHMARK=ON
cmake --build build-measure --config Release --target gilyumei1_benchmark gilyumei2_rubii_benchmark
./build-measure/gilyumei1_benchmark < benchmark/results/holdout-inputs.txt
./build-measure/gilyumei2_rubii_benchmark < benchmark/results/holdout-inputs.txt
./build-measure/gilyumei1_benchmark 1500 0 bridge < benchmark/results/bridge-g1-inputs.txt
```
## 変更範囲と同梱物
新規探索は`GilyumeiSearch.h/.cpp`。既存ファイルの変更は`main.cpp`、`CMakeLists.txt`、`webassembly/build.sh`、装備値の読み取り関数を加えた`BattleEmulator.h/.cpp`に限定した。初期Player値、敵AI、ダメージ計算、RNG、状態異常、装備変更の実処理、BattleResultの意味は変更していない。Player、Equipment、BattleResult、camera、lcgの各既存ファイルは入力ZIPと同一だった。
旧探索コードは比較・参照用として残しているが、本番SearchRequestからは呼ばれない。既存`gilyumei2_rubii_opt`は従来の比較用targetであり、新探索の起動に使う必要はない。
`benchmark/results/`に旧版・新版DEBUG3、別seedのCSV、入力、ネイティブexport確認ログ、WASMビルド停止ログを同梱した。元の`.git`履歴とIDE個人設定は配布ZIPから除き、元のプログラムsource・画像・既存文書は保持した。