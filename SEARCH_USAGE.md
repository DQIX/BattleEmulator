# ぬしさま2探索器の使用方法
## 通常ビルド
```bash
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build --config Release -j2
```
実行ファイルはnusisama2。MSVCでは通常build/Release/nusisama2.exe、MinGWではbuild/nusisama2.exeとなる。今回実際にビルド・計測したのはLinux/GCC 14.2.0のRelease構成である。
## 既存の呼び出し口
既存SearchRequestの署名・引数は変更していない。初期Player、seed、既存action prefixを渡すと、既定1000ms・variant6でprefix後のsuffixだけを探索し、正確リプレイしたdumpTableと行動列をstringstreamへ書く。旧6表A*の呼び出しはSearchRequestLegacyとして保持した。
時間予算やvariantを直接指定する場合は次のAPIを使う。
```cpp
#include "NusisamaSearch.h"
// initial: 元の初期世界。prefix: 実行済みコマンド列を先頭に保持する350要素配列。
const NusisamaResult result = NusisamaSearch::Run(
    initial, seed, prefix, prefixLength, 250, NusisamaSearch::DefaultVariant);
// 採用可能条件: result.victory && result.replayVerified
// result.actions = 元prefix + 探索suffix
// result.replay = 元入力から完全再生したBattleResult
// result.finalState = 完全再生後のPlayer/RNG/nowState
```
result.replay.positionは戦闘イベント件数、result.finalState.rngPositionはRNGカーソルであり別物。敵HPはresult.finalState.players[1].hpで確認する。既存dumpTableは各イベントのダメージ適用前HPを表示するため、勝利時の最終行に残HPが表示されても、最終Player状態の敵HPは0である。
## 追加CLI
```bash
./build/nusisama2 --search-variants
./build/nusisama2 --search 0x193e27 250 6 30
```
形式は`--search SEED [BUDGET_MS] [VARIANT] [PREFIX_ACTION ...]`。例の固定prefixはBUFF(30)の1技だけで、以後の行動列は実行時に探索する。成功時はdumpTableを最初に表示し、SEARCH行とACTIONS行を続ける。終了コードは勝利0、勝利解なし2。seedは22bitに限定していないが、提出した評価入力は22bit seedを用いた。
## 評価の再実行
```bash
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release -DNUSISAMA_BUILD_EVALUATION=ON
cmake --build build --config Release -j2
./build/nusisama_benchmark evaluation/holdout.txt 250 -1,2,6 evaluation/replayed.csv
./build/nusisama_benchmark --request 0x193e27 30
bash evaluation/reproduce.sh
```
benchmarkの-1は旧6表A*、0～12は新探索器の各variant。出力CSVに実測値、同名の`.traces.txt`に既存dumpTableと全行動列を保存する。`--request`は新APIだけでなく本番SearchRequest自体を呼び出す。reproduce.shは添付の固定入力で全比較を再実行し、元の実測ログを上書きせずevaluation/reproducedへ出力する。
`--make-cases OUTPUT COUNT GENERATOR_SEED`は、指定した汎用prefixを正規Mainで再生して、生存中・未勝利の現在状態だけを作る評価入力生成機能である。探索器や勝利解を参照してseedを選別しない。入力ファイル末尾に再生したprefix数と採用数を記録する。
## 境界と範囲
この実装ではprefixは0～90ターン、探索horizonは既定prefix+40ターン、絶対上限90ターン。元エミュレータの固定RNGキャッシュとログ配列を変更せず、この枝の短い単独ボス戦を対象にしている。maxTotalTurns引数で90以下の明示horizonも渡せる。prefix中に死亡した世界や、制限時間内に勝利を発見できなかった世界は勝利として返さない。
世界遷移はすべて元BattleEmulator::Main。合法性判定は元ACTION_TABLEの条件に従い、INSULATEも通常候補として残す。FLEEは麻痺・睡眠・inactive中に選ばない。並列実行は導入していない。既存WASMビルドのソース一覧へ新.cppを追加しただけで、WASM exportの増設やWASM環境での実行確認はしていない。