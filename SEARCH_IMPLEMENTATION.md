# nusisama1_v2_new_arugo 探索実装
## 対象と目的
対象は既存の `webassembly/build.sh` が定義する `nusisama1_v6_tamahane` と `nusisama1_v6_hagane`。実際の BasePlayers の攻撃力121／104、守備力96、敵HP1256など、世界側の値・敵AI・ダメージ式・乱数処理・カメラ処理は変更していない。macro名に含まれる125／108／93を実値として上書きしていない。
目的は、与えられた固定prefixを保持し、総予算1500msの中で、fresh exact replayにより敵HPが0になる行動列の `BattleResult.position` を小さくすること。数学的な最適性の保証はない。
## 探索と結果
`ActionOptimizer::RunAlgorithm(players, seed, actions, budgetMs = 1500)` を、段階的に幅を増やすビーム探索へ置換した。小さい幅で勝利候補を確保し、幅と生存資源の評価を変えて改善する。全passが同一deadlineを共有する。状態はPlayer全体、NowState、RNG cursorを引き継ぎ、実際の `BattleEmulator::Main` で1ターンずつ進める。行動列は親リンクで保持する。同一状態の重複除去ではhashだけで同一と見なさず、Playerの全フィールドも比較する。
探索候補は通常攻撃、ドラゴン斬り、防御、逃げる、特やくそう、ホイミ、ヒャド、ヒャダルコ、アクロバットスター。回復や防御も、HPだけで勝手に除外しない。MP条件はBattleEmulatorの実処理に合わせて2／3／8とし、特やくそうの所持数、必殺チャージと残りターンも見る。睡眠・麻痺中にはFLEEを生成しない。敵専用技は追加していない。
prefixは350要素の配列の `-1` までを一般に処理する。入力はconstであり、長さ・行動・順序を変えない。生存できないprefixやモデルの既存バッファ容量を超える入力では、prefixを捨てた再探索をしない。
各辺のコストは実際に記録したBattleResultのイベント数。`Genome.position` は従来どおりRNG cursor、`Genome.fitness` はfresh replayの `BattleResult.position`、`Genome.processed` は完全行動列のターン数であり、別の量として扱う。勝利候補は原状態・原seedからprefixとsuffixをまとめてfresh replayし、敵HP=0に加えてPlayer全フィールド・NowState・RNG cursor・イベント数の一致を確認して採用する。返却直前にも採用列をfresh replayする。`Initialized=false` は検証済み勝利なしを表す。
## 接続
`DEBUG3 -> SearchRequest -> ActionOptimizer::RunAlgorithm` と `wasm_search_dump -> buildDumpOutput -> ActionOptimizer::RunAlgorithm` の両方が同じ最終探索を使う。既存のWebAssembly公開関数の引数は変更していない。どちらの呼び出し元も完全行動列をfresh replayしてから同じ結果のdumpTableを出す。探索・出力の100ターン打ち切り、WebAssembly側のprefix短縮、未撃破のまま成功扱いする判定は取り除いた。
従来の世代数引数とseedOffset引数は探索APIから外し、総時間予算に置換した。対象内の呼び出し元は接続済み。nativeのDEBUG3は引数なしの既存seed・1手prefixを維持し、任意の診断入力も `DEBUG3実行ファイル seed actionID...` で同じSearchRequestへ渡せる。専用の解テーブルやテストスイートはない。
## 実測
Linux x86-64、GCC、CMake Release、各variantの実compile definition。時間はSearchRequest内の探索と呼び出し元fresh replayまでの実測値。既定seedは元コードの `0x2751013`、固定prefixは `[25]`。
| variant | 旧版のexact position | 最終版のexact position | ターン数 | 敵HP | 実測時間 |
| --- | ---: | ---: | ---: | ---: | ---: |
| tamahane | 43 | 43 | 22 | 0 | 1490.85ms |
| hagane | 54 | 52 | 26 | 0 | 1491.98ms |
別seed `0x2751014`、prefix `[25]` ではtamahaneがposition=37／19ターン、haganeがposition=45／23ターン。5手prefix `[25,26,25,56,59]`、seed `0x2751013` ではそれぞれposition=44／22ターン、position=53／27ターン。いずれも敵HP=0、fresh replay一致、約1491〜1492msであった。これらのseedやsuffixを探索器の既知解として使用していない。
探索幅の追加拡大、評価重みの追加、RNG状態別の多様性枠は、この入力で解の改善が得られなかったため最終版に含めていない。実測ログは `search_benchmarks/` に同梱した。これらは実行記録であり、新規テストケース・テスト基盤ではない。
## ビルドと確認範囲
対象2つのnative Releaseビルドと、既存DEBUG3/SearchRequestでの実行を確認した。診断ビルドは次のとおり。
```sh
cmake -S . -B build-search -DCMAKE_BUILD_TYPE=Release -DDEBUG3=ON
cmake --build build-search --parallel
```
通常ビルドでは `DEBUG3` は既定OFFであり、従来の入力経路を使う。添付ZIPの `webassembly/build.sh` はCRLFのためbashの構文確認で失敗したので、処理内容とvariant定義は変えずLFへ正規化した。WebAssemblyは次の既存ビルド定義で同じ探索器をコンパイルする。
```sh
bash webassembly/build.sh --branch nusisama1_v2_new_arugo
```
この作業環境にはEmscriptenがないため、実際の `.wasm` ビルドとブラウザ内実行は未実施。代わりに既存の公開関数を含むネイティブ共有ライブラリを両variantでビルドし、`wasm_prepare_input("a37") -> wasm_search_dump(0, 0x2751013, 4, 0)` を実行して、同じ最終探索・prefix・検証済みdumpへ到達することを確認した。この確認はWebAssembly実行そのものではない。