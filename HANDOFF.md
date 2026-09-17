# 引き継ぎ
Latest checkpoint ZIP: /mnt/data/erugiosu_checkpoint_02.zip
Final ZIP: /mnt/data/erugiosu_search_final.zip
## 目的と対象
対象は添付`erugiosu_new_arugo(1).zip`のbranch `erugiosu_new_arugo`、HEAD `10b1381223fec3acf6546d4957cfd6721b013cf0`。`GOUKETU=1`の`newDirectory_gouketu`へ実装した。目的Xは初期Playerと固定prefixの正確な再生から得られた現在状態以降だけを探索し、約1500msで敵HPを0にする行動列を求めること。勝利を前提に`BattleResult.position`を最優先、prefix後の実際の装備変更回数を第二優先とする。RNG cursorの最小化ではない。prefixや世界を変更して短い解に見せることは厳禁である。
## 実装済み
`ErugiosuSearch.h/.cpp`を新規追加し、SearchRequestのシグネチャを変更せず通常GOUKETU経路へ接続した。幅拡張beam、arena経路、意味的な完全状態比較付きflat hash、戦術phase枠、複数評価関数、現在の探索で発見した解の末尾再探索を組み合わせたanytime探索である。装備固定laneを必ず最初に実行し、その後も装備対応laneと合計1500ms内で競わせる。20msを終了処理用に残すため、実測では約1480msになる。
カンニング元は`reokonn-GPT6-Pro`、`bilyouma-gpt6-pro`、`yo2-gpt6-pro`と別添optimization。各commitと適用した技法は`optimization/PROVENANCE.md`にあり、参照ソースも`optimization/donors/`に保存した。参照先の敵・技集合・世界値・既知seed回答は持ち込んでいない。エルギオス2用の評価値は手設計と開発入力比較であり、機械学習済みと呼ばない。
通常actionと装備維持／切替を独立に展開する。bit16は希望装備状態、bit17は出力markerであり探索入力には生成しない。麻痺・眠り中は装備を維持し、1ターン最大1回の実変更しか起こさない。rootが素手なら装備固定laneも素手を保持する。Player全メンバー、装備defaultATK、派生atk、NowState、RNG cursor、ログpositionの完全比較を行う。hash一致だけでは統合しない。実際の装備変更はMain前後のdefaultATK変化で数え、素手bitの数ではない。
実装済みの味方技20種類を展開し、スカラ、フバーハ、回復、すてみ、ミラー、ためる、さみだれづき等を利用する。一閃づき・疾風づき・きゅうしょづき、敵技、未実装enumは生成しない。FLEEはターン開始時の麻痺・眠りだけでなく、敵先行で新規麻痺を受けた際のpre-action skip悪用も拒否する。Cローテーションでミラーを維持し、敵のメラゾーマを反射して削りながらテンションをため、高テンションさみだれづきへ繋ぐ価値を評価する。ただし技列は固定しない。
最終候補は初期世界＋固定prefix＋suffixを全列Mainで再生し、1ターンずつのMain再生とも一致した勝利だけ採用する。実測ドライバでも別途再生し、prefix不変と最終状態一致を確認済み。敵先行の最終反射で味方ログが出ない場合があるため、装備markerだけに依存せず実変更数を確定する。
## 結果
開発の共通生存root6件でA〜Fを含む10variantを比較した。variant0、2、6、8が平均position34.333で並び、反射単独、damage-value、burst、攻撃previewは上回らなかった。追加4件を含む10件ではvariant0と6が同値で、装備固定平均39.300に対し採用34.600。改善7件、同値3件、悪化0件。`dev07`はprefix終了時死亡、`dev08`は死亡後のターンもprefixに含むため対象外で、元の記録は残している。十分な成果とvariant間の収束を認め、既定variant0を採用して追加調整を終了した。
ソース固定後に未使用holdout16件を生成・実行した。15件は生存rootで、装備固定・採用の両方が全件勝利、独立再生一致、rejected_replay=0。残る`hold10`はprefix終了時点でHP0であり、両方とも勝利なし。生存15件中、採用は12件でposition改善、3件同値で変更0回、悪化0件。平均positionは装備固定39.067、採用33.267。ただし素手root4件を含む。通常の戦闘開始8件だけでは32.625対30.625、装備あり途中状態3件では40.000対39.000、素手root4件では51.250対34.250となる。holdoutを見た再調整は行っていない。
現在bestの代表として、`0x17a83d`は装備固定position34／0回に対し採用position29／2回／10ターン。反射＋ためる＋高テンションさみだれづきを含む。holdoutの`0x869e1`は装備固定・装備対応ともposition25／0回で、0回を採用した。異なるseed同士のpositionを同一入力の改善として比較してはいけない。素手prefix`0x3f0d2a / 65566`は装備固定52、採用34、prefix後3回の変更である。
採用holdoutのwall時間は1480.016〜1483.754ms、初勝利は1.569〜4.989ms。原データ、全variant、各caseのdump、holdout集計、代表exact replay、本番SearchRequest実行ログは`optimization/results/`にある。SearchRequestの実呼出しを初期状態・装備ありprefix・素手prefixの3種類で確認し、すべて勝利・replay_verified=1だった。
## 保存内容と残る範囲
Linux GCC14.2.0／CMake3.31.6のReleaseで本番ターゲットと実測ドライバをビルドした。Windows／MinGW／WebAssemblyは今回実機ビルド・実行していない。既存WebAssembly exportは追加せず、build.shへ新ソースと定義を加えた。世界側10ファイルは添付とSHA-256一致している。新規テスト、sanitizer、メモリ整合性検査、無関係なrefactor、ローカルMCP、personal_context、Web検索は行っていない。
探索は既定で全戦闘60ターン、引数指定で最大99ターン、RNG cursor6500未満の追加展開、非zero seedという実装範囲を持つ。有限時間heuristic探索なので数学的最短性や全未知seedの勝利を保証しない。既存の非GOUKETU経路や未完成parser全体は今回の目的外として残した。詳細な再現コマンドと制約はREPORTに記載済みである。
コード、比較、holdout、exact replay、dump、REPORTが揃っており、今回の探索方式改善は終了した。今後別作業で変更する場合も固定prefix／世界不変／position優先／実変更回数第二優先を守り、今回のholdoutを再学習用に使った後で未使用holdoutと呼ばないこと。サンドボックス継続を前提にせず、上記Final ZIPを展開すれば全ソース、結果、参照資料を利用できる。