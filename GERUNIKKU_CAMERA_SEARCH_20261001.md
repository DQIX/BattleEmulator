# ゲルニック camera 分岐・総当たり・探索改修（2026-10-01）
## 対象
本体は `C:\Users\owner\Documents\tunnelworkspace\br\BattleEmulator`。今回の目的は、camera分岐を含むゲルニック総当たりの高速化と、300〜1500ms帯を想定した探索改善である。
## 総当たり
`BattleEmulator::CameraContinuation` を使い、1ターンの戦闘処理を一度だけ進めた後、必要になったcamera候補だけを同じcamera直前状態から評価する。戦闘計算・camera計算そのものは既存 `Main` / `camera::Main` を使い、独自近似へ置換しない。
`BruteForceObservationMatcher` は観測不一致を可能な限りcamera処理前に落とし、必要になった境界でのみ actor × param5 を展開する。幅上限によるseed候補の無言切捨ては行わず、完全一致stateだけを統合する。
## 探索
勝利条件は主人公生存かつ敵3slotのHPが0。安全性を満たす勝利候補では `BattleResult.position` を最優先し、同値なら実際に成立した装備変更回数が少ない候補を優先する。ターン数やcamera回数をこの順位より上には置かない。
装備あり／素手を探索対象に含め、装備変更回数は実際に変更が成立した箇所で数える。
探索予算の既定値は1500ms。CLIの既存milliseconds引数で300〜1500ms帯を評価できる。WASM専用の新しい設定・保護exportは追加しない。
## seed入力
初期seedの入口は既存経路を維持する。フロントエンドではUI入力文字列を `parseInput()` し、先頭の時刻部分からseed探索範囲を作り、総当たりで一意になったseedを既存Searchへ渡す。
seedを別の新規API引数として再投入する経路は追加しない。
## WASM API
既存exportのみを維持する。
`wasm_prepare_input`
`wasm_get_last_error`
`wasm_bruteforce_range`
`wasm_get_turn_processed`
`wasm_get_found_seeds`
`wasm_search_dump`
今回追加してしまった未承認のWASM保護・観測用exportは全て撤去した。
## 実行確認
`issue.txt` の現指示に従い、今回の修正後ビルド、ROM実測、benchmarkは実行していない。速度倍率や複数seedでの一致は未測定であり、静的差分確認のみ。
