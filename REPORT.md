# nusisama2_new_arugo 探索アルゴリズム実装報告
## 代表解のdumpTable
既存dumpTableを変更せず出力した代表結果は`evaluation/results/representative-prefix10.txt`、本番SearchRequestの出力は`evaluation/results/production-SearchRequest.txt`に保存した。全比較入力の表と行動列は各CSVと対になる`.csv.traces.txt`にある。
```text
seed: 0x3741eb
固定prefix: 30 30 62 37 62 37 62 33 37 34
探索suffix: 30 62 37 33 62 62 62 34
予算: 250ms / variant 6 (survival)
全18ターン、suffix 8ターン
exact BattleResult.position: 52
最終ally HP: 75 / 最終enemy HP: 0
最終RNG cursor: 921 / 最終nowState: 73728
exact replay: 一致
```
この代表解の最終dumpTable行にある敵HP=2008はダメージ適用前の値である。最後のMULTITHRUSTのダメージ2226を適用した最終Player状態では敵HP=0。表示形式やログの意味は変更していない。
## 実装結果
NusisamaSearch.cpp/.hを新設し、既存SearchRequestへ接続した。本番既定はvariant6、1000ms。初期世界と固定prefixを正確に再生し、その後のsuffixだけを探索する。敵HP=0を満たす候補を完全再生で照合し、BattleResult.positionが小さい候補を採用する。世界側・合法技条件・既存dumpTable・SearchRequestの署名は維持した。
13variantを開発入力で比較し、採用設定を固定してから未使用seed20件のholdoutを評価した。holdoutを参照した重み調整・variant変更はしていない。固定時点の記録は`evaluation/FROZEN_SELECTION.md`、探索ソースのSHA-256は`evaluation/frozen-source.sha256`にある。
## 独立holdout
20入力すべてが生存中・未勝利の現在状態であり、prefix長は0、1、3、6、8、10ターンを含む。開発入力とのseed重複はない。初期Playerはこのbranchのmain.cppが定義する元の値を使用した。
|予算|旧6表A*の勝利|新variant6の勝利|新平均position|新平均初勝利ms|新実行平均ms|新実行最大ms|
|---:|---:|---:|---:|---:|---:|---:|
|25ms|未計測|20/20|47.3000|1.8434|24.0425|24.2181|
|250ms|19/20|20/20|47.3000|1.7159|241.9828|247.1860|
|1000ms|20/20|20/20|47.3000|1.6443|504.5030|735.4740|
250msの入力別比較は7件改善、12件同値、1件追加勝利、悪化0件。両方が勝った19入力に限定した平均positionは旧49.8947、新46.5789。全20入力の新平均47.3000と、19勝のみの旧平均を同じ母数として比較していない。
1000msでは全20入力で両者が勝利し、平均positionは旧50.2000から新47.3000へ改善した。7件改善、13件同値、悪化0件。改善幅は平均2.9イベントであり、RNGカーソルの短縮ではない。今回のholdoutの最小positionは37（seed 0x19b5a8、prefixなし、13ターン）。これは評価集合内の最小値であり、全域最適性の証明ではない。
比較用variant2は25msで平均position47.6000、250msと1000msでは47.3000だった。採用variant6は25msでも長い予算と同じ平均positionを達成した。以上はこの20入力と測定環境の実測であり、未知の全seed・全Player設定に対する勝利保証ではない。
全holdoutの勝利結果は再度exact replayを行い、prefix一致、全Player field、RNG cursor、nowState、BattleResult.position、suffix各イベントと合法性を照合した。採用結果の不一致0件、探索内rejectedReplay=0。variant6のholdout実行で予算超過は0件だった。
## 開発比較と採用理由
development.txtは8入力中6入力、development-extended.txtは20入力中13入力が生存rootだった。合計9入力は固定prefix中に死亡しており、eligible=0として全variantの結果に残した。prefixを変えて生存させたり、戦闘開始から探索し直したりしていない。生存19入力ではvariant6は250msで全件勝利した。
さらにdevelopment-live.txtで、生存root20入力を探索結果に依存せず生成して比較した。
|予算|旧6表A*の勝利|variant6の勝利|variant6平均position|variant6初勝利平均ms|
|---:|---:|---:|---:|---:|
|25ms|未計測|20/20|47.0500|1.6154|
|250ms|17/20|20/20|47.0500|1.7238|
|1000ms|20/20|20/20|47.0500|1.7997|
25msではvariant0/2/3/5/7が平均47.20、variant6が47.05で最良だった。250msと1000msでは有力variant0/2/6が平均47.05で同値。攻撃preview、2-ply展開、best-first、低いテンション価値、portfolio、suffix repair、開始beam幅増加も比較したが、有力方式を超える一貫した改善は得られなかった。best-firstは25msで19/20勝利となり、採用していない。
以上からvariant6を採用した。250msから1000msへ延ばしてもこの開発集合のpositionは改善せず、試した範囲では解品質が収束したため提出する。幅制限のある探索であり、完全探索・最適性証明ではない。
## 探索の構成
採用variant6は、実遷移を用いたbeamを64→256→1024→4096→16384へ拡幅する。各層で合法actionを展開し、PlayerとRNGを含む正確な状態の重複を排除する。評価は残敵HP、味方HP、MP、ためたテンション、すてみの準備、防御buff、回復資源、現在HPに対する危険度を用いる。高テンションのさみだれづきと、そこへ到達するまでの生存・回復を優先する。
テンション0～4とすてみ状態の有無で10区分のquotaを設け、準備状態も残す。スコアは枝順序のみに使用し、Playerやダメージ式、敵AI、RNGへ書き戻さない。INSULATEとMAGIC_MIRRORにはこの敵への直接防御価値を与えず、合法なRNG調整候補として展開する。
候補の比較はBattleResult.positionを第一にし、同値の場合にsuffix長と残味方HPでtie-breakする。現在イベント数から最低1イベント必要という下限だけで、既存勝利を超えられない枝を除く。近似スコアを最短性の下限として使っていない。
path arenaには選択された親リンクと行動を保持し、全action配列の各nodeへのコピーを避ける。flat hashのfingerprintはbucket選択用であり、衝突時には全Player field、RNG cursor、nowState、イベント数を比較する。hash一致だけで状態を捨てない。
各遷移は既存Mainで1ターン実行する。このbranchはnowState内の実ターンを基に全体action配列を参照するため、他branchの1要素geneは渡さず、350要素scratchの実ターン位置へ技を設定する。採用前には元の初期世界から固定prefixとsuffixを全再生し、さらにsuffix各ターンのログを全再生ログに照合する。採用のための再生時間は探索予算に含める。
期限判定はsteady_clockを使い、終了処理分として最大8msを残す。幅16384まで終わった場合は予算を使い切らず早期返却する。1000ms時の実行平均が約505msなのはこのためである。壁時計の停止は協調的なチェックであり、OSの長い停止まで含むハードリアルタイム保証ではない。
## 保存したvariant
|番号|名前|主要な違い|
|---:|---|---|
|0|tension-portfolio|テンションbeam、repair、低テンション評価beamのportfolio|
|1|damage-greedy|直近ダメージ寄りの評価|
|2|tension-balanced|標準テンション評価、quotaなし|
|3|tension-quota|戦術quotaを強める|
|4|attack-preview|次のMULTITHRUSTを実遷移でpreview|
|5|super-tension|ためたテンションの価値を高める|
|6|survival|HP・危険度・防御buffを重視。本番採用|
|7|tension-repair|標準評価とsuffix repair|
|8|weighted-best-first|イベント数と状態価値によるbest-first|
|9|two-ply-tension|2ターン単位の枝選択|
|10|low-credit-quota|準備状態の評価を低める|
|11|wide-tension|開始beam幅256|
|12|preview-heavy|攻撃previewの重みを高める|
未採用variantも削除していない。開発中にvariant0の1000ms実行で最大1007.24ms、variant10の25ms実行で最大25.77msという予算超過を記録した。これらを本番採用せず、ログにも残した。時間条件を延ばした結果として採用していない。
## 参考branchと変更範囲
対象branchの元HEADは4379054a70c432dc41222f6899fd3c8e8c934d3e。参考は添付Gitの以下3branchである。
|branch|参照commit|
|---|---|
|reokonn-GPT6-Pro|8f1b2c37ef380955cb3207f35015a09b7285eecc|
|bilyouma-gpt6-pro|a39276d8063f0a867127da52ce71a7545e011454|
|yo2-gpt6-pro|4317f4863e79f1bdc861f4faea1f94613285211d|
参照したのはcompact path、段階的beam拡幅、exact照合付きtransposition、戦術quota、局所suffix repair等の探索技術。敵HP、味方stats、敵技、各branchの既知解は移植していない。探索ソースにseed別・prefix別の答えlookupはなく、実測action列は評価ログにのみ保存した。
本番の既存ファイルへの変更はmain.cpp、CMakeLists.txt、webassembly/build.shの接続のみ。新規本体はNusisamaSearch.cpp/.h。ActionOptimizer、BattleEmulator、Player、BattleResult、Genome、lcg、camera、Equipment、既存cost/hash/heap/pool関連ファイルは添付元とバイト一致を確認した。元のA*重みは他BattleEmulator由来のまま温存した。
FLEEは麻痺・睡眠・inactive中の選択を探索と採用時の両方で拒否する。この敵の行動集合は、それらの状態を同一ターン途中に新規付与しない。通常状態でのFLEEやINSULATEによるRNG変化は、Mainを普通に実行することでのみ得る。RNG cursorを検索側で直接増減していない。
既存SearchRequestは新探索器を既定使用し、旧実装をSearchRequestLegacyへ残した。既存パーサーやWASM exportは変更していない。WASMはソース一覧への追加のみで、ビルドや実行は未検証。指定されたchatGPT.txtは添付対象treeに存在しなかった。
## 評価方法と環境
Linuxコンテナ、AMD EPYC 9V74、GCC 14.2.0、CMake 3.31.6、C++17、Releaseの-O3 -DNDEBUG、NUSISAMA2定義で計測した。同時に別の探索を走らせず、同一実行ファイル・初期世界・seed・固定prefixをvariantごとに順番に実行した。探索は単一スレッド。
旧A*比較にはevaluation/BaselineTimed.cppを使用した。元ActionOptimizerのコピーへ壁時計deadlineを追加した評価用で、元のA/B/C/D/F/G表を残り予算で順番に実行する。重み、探索方針、pool、30ターン制限を修理していない。6回のprefix再生と候補のexact replayも全体時間に含む。元の6000 generations固定呼び出しそのものではなく、同じ旧探索を同じ壁時計条件へ合わせた比較である。
旧A*のfirst_msは各tableが戻って検証済み候補が得られた時点であり、内部で初めて勝利nodeを見た時刻ではない。新探索器のfirst_msと厳密な内部初勝利速度として比較しない。旧A*のexpanded/duplicatesは未計測のためCSVでは0。新方式のexpandedはpreviewとanchor再生も含む1ターン実遷移数で、完全再生による検証分は含まない。
生存root生成では、準備・回復技の固定テンプレートをMainで再生し、味方・敵がともに生きているseedだけを採用した。development-liveは35回のprefix再生から20入力、holdoutは27回から20入力。勝利解、勝利率、探索難易度では選別していない。元の死亡prefix入力は別の開発ログに残している。
## 再現と残る範囲
SEARCH_USAGE.mdにビルド、API、CLI、再現手順を記載した。evaluation/reproduce.shは固定入力と全variant比較を再実行し、元ログを上書きしない。evaluation/resultsにはCSV、集計CSV、dumpTable、全行動列、本番SearchRequest出力を保存した。新規テストsuiteやサニタイザ実行は含めず、通常ビルド、探索benchmark、解そのもののexact replayを実施した。
最終ZIPには完全ソースtree、参考branchを含む元.git、全13variant、評価コード、固定入力、実測値、REPORT、HANDOFFを含めた。build cacheや実行バイナリは含めない。commit/pushは行っていない。
この実装はprefix0～90ターン、既定horizonはprefix+40、絶対上限90ターン。元の固定長RNGキャッシュとイベントログを拡張せず、この単独ボス戦へ適用する範囲とした。異なる初期Playerや極端に長い戦闘を含む全ケースの勝利、全域最短性、Windows・WASM上の実測性能は未確認。今回の本branchへの接続、複数現在状態からの合法suffix探索、固定時間比較、正確再生、成果物保存は完了した。