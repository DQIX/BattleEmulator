# ゲルニック camera 分岐・総当たり・探索改修（2026-10-01）

## 対象と実行状況

本体は `C:\Users\owner\Documents\tunnelworkspace\br\BattleEmulator`、ブランチは `gerunikku1_new_arugo`。`BattleArrow\BattleEmulator` の C++ は別戦闘用なので変更していない。共通フロントエンドだけは同ディレクトリの `public/app.js` と `public/worker.js` に存在するため、この2ファイルを変更した。

`br\BattleEmulator\issue.txt` の最新指示「clion回せないけど書類送検で直して」に従い、改修後のコンパイル、ROM照合、ベンチマーク、WebAssembly実行、デプロイは行っていない。開始時のROM観測は改修前であり、改修の一致・性能の証拠ではない。今回の確認はソースと差分の読み合わせのみ。速度倍率、300/1500msで得られる最終position、複数seed一致は未測定。

## 総当たりの変更

`BattleEmulator::CameraContinuation` を追加。既存 `Main` の戦闘部分を一度実行し、既存 `camera::Main` の直前で、action/actor/target/slot1子情報、先制状態、skyAttack、記録数、装備変更数を保存する。カメラ候補ごとに戦闘1ターンをやり直さず、同じ直前状態からカメラ部分だけ実行する。

`PrepareSearchTurn` / `CompleteSearchCamera` は戦闘・カメラ計算の代替実装ではない。元の Main/camera::Main を呼び、既存のフル SearchState と camera runtime をコピーして使う。

`main.cpp::BruteForceObservationMatcher` は観測不一致を戦闘部分で早期排除し、必要な境界だけ actor × param5 を試す。兄弟枝はDFSで遅延保持する。旧 `next.size() < width` による候補切捨てを撤廃し、同一観測cursorかつ完全に等しいSearchStateだけを統合する。非cached LCG のcursorも分岐ごとに復元する。未来の直積を先に作らず、同時保持量を深さ×分岐数に抑える。ただし、区別できない観測しかない任意の入力について、最悪計算時間が多項式になるという保証ではない。

## 探索の変更

勝利は主人公生存かつ敵3slotのHPが全て0。安全性を満たす候補群の中で、勝利時の `(BattleResult.position, equipmentChanges)` を辞書順比較する。ターン数とカメラ回数を勝利順位・枝刈りの上位条件から除去した。将来の探索順のheuristicにも `depth*6` ではなく実際に生成されたrecord数を渡す。

装備あり/素手を両方探索し、素手の槍特技を候補から除外する。装備変更回数は指示bitの回数ではなく、Main内で実際に装備変更が受理された箇所で加算する。変更拒否の既存挙動自体は変更していない。

既定予算1500ms、WASM設定は300〜1500ms。最初の勝利で止めず、幅を広げるroot再探索と、検証済み勝利手順の途中から後続全体を再計画する探索を交互に行う。後者も同じカメラ候補展開を通す。期限が来たときは評価済みの安全候補を捨てず、未確認の危険な入力を安全な代替なしと決めつけない。

観測済みprefixも必要な枝だけDFSで展開し、観測との一致を途中で確認する。探索rootの保存上限や期限に達した場合は不完全性を明示し、seed不一致と偽って扱わず、追加の入力選択を止める。

未解決rootが複数ある場合は、次の同一入力を全rootに対して確認する。未来のcamera結果以後は、観測・再探索を前提とした別の完全SearchStateとして展開する。異なる未来の枝に同じ固定入力列を延々と強制する構造は除去した。分岐数を確率に読み替えていない。`fatalOutcomes` は診断用の候補数であり死亡確率ではない。

## 検証経路と公開境界

勝利更新・最終出力は既存のfresh Main replayを通し、全SearchState、record数、装備変更回数を比較する実装を維持・拡張した。このコード経路を今回実行して確認したという意味ではない。

`--trace-main-sequence`、DEBUG2/DEBUG3、SearchRequestの既存経路を残している。`battlePreparations` / `cameraReplays` / `equipmentChanges` / `fatalOutcomes` を結果統計へ追加した。

通常dumpは最初の未確認cameraがあるターンの手前で止める。0ターン公開時の `gene[-1]` を解消した。境界の `NEXT_INPUT` はcameraの予測bitを除いた1入力のみで、以後の予測結果ではない。SearchRequest失敗時に無検証Mainを再実行して全dumpを出す経路は削除した。「予算内に勝利を確認できない」と「必ず負ける」を区別する。

## Worker と観測再投入の契約

Search Workerは完了時に破棄しない。対応する同一moduleを再検索するときも既存workerを再利用する。初期化完了後の各requestには巨大なSINGLE_FILEソースを再送せず、moduleKeyと入力だけを送る。WASMの共有atomic generationをdocument側から更新し、C++側が探索境界で中断を確認する。prepare/searchはworker内で直列化し、旧generation・旧リクエストの結果を表示しない。別moduleへの切替やpagehideでは終了する。

追加export: `wasm_search_generation_address`、`wasm_configure_search(milliseconds, generation, confirmedTurns)`、`wasm_get_observed_turn`。新しい共有中断経路には変更後のWASM再ビルドが必要。配布済みバイナリは未更新。

観測側からは `window.researchBattleFromObservation({ seed, input, confirmedTurns, milliseconds })`、または同じdetailを持つ `battle-observation` イベントで再投入する。

- `seed`: この既存APIでは初期seed。現在LCGの生の値を初期seedとして渡さない。
- `input`: 既存の敵行動 / 主人公入力 / damage履歴形式による最新実測。これを元に現在のSearchStateを再構成する。
- `confirmedTurns`: 観測側が確認したターン数。予測dumpの到達ターンから作らない。未指定時に勝手な確認済み扱いはしない。
- `milliseconds`: 300〜1500、未指定1500。

今回追加したのは観測受け口と再探索経路であり、画面の自動取得・映像からの不一致検出器や任意の生savestate/SearchStateメモリを直接取り込むABIは追加していない。

## 変更していない領域

敵AI、ダメージ計算、乱数生成式、浮動小数点kernel、renderer residue互換関数、freecam判定本体は変更していない。新規テスト基盤・サニタイザ・一般メモリ検査は作成・実行していない。ユーザーのissue.txt、opencode.json、および別戦闘の生成済みcamera headerは変更していない。コミットとpushはしていない。
