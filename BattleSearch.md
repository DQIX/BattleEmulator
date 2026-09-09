# BattleSearch

探索本体は `BattleSearch.cpp`、公開APIは `BattleSearch.h`。
`SearchRequest` が既存処理で構築した Player 2体・乱数位置・NowState をそのまま受け取る。
遷移はすべて `BattleEmulator::Main(..., RunCount=1, logicalTurnStart=true)` を使う。

時間制限付きの並列ビーム探索。短時間で最初の解を得た後、幅を96から32768まで拡大し、
発見した経路の接尾部も再探索する。勝利、少ない追加ターン数、残HP、乱数位置の順に比較する。
スターの残り時間、必殺チャージ、回復資源、行動不能を評価し、乱数位置の異なる候補を残す。
通常会心・カウンタ会心・回避の効果も、エミュレータによる実際のHP変化で評価する。
同一状態は全フィールドを比較して重複を除去する。既知の解・事前学習データは使わない。
有限幅探索なので大域最適の保証はない。

通常攻撃、防御、ドラゴン斬り、ホイミ、ヒャド、やくそう、アクロバットスター、逃げるを探索する。
MP・所持数・必殺チャージの制約を適用する。逃げるは行動不能時に使わず、敵が先行する場合も
通常攻撃の正確な試行でそのターンの行動不能を確認してから分岐する。

既定予算は5000ms。アプリでは環境変数 `BATTLE_SEARCH_MS`、独立評価では
`Options::budget` を指定する。`Options::threads` と `initializeWorker` で並列実行を指定できる。
初期化コールバックがない場合は、呼び出しスレッドの既存エミュレータ状態で実行する。
探索開始から初期化・探索終了までを計時し、表示用のリプレイは探索後に行う。

`printResult` は選択した行動列を正確にリプレイし、最終状態の全フィールドを検証した後、
既存 `dumpTable` の出力を最初に表示する。概要と計測値はその後に出す。

ビルドは新規作成した `build-search-winlibs-20260909` を使う。

```powershell
& 'C:\Program Files\CMake\bin\cmake.exe' --build build-search-winlibs-20260909 --target ybe_v5_lv10 battle_search_checks --parallel 4
$env:BATTLE_SEARCH_MS = '5000'
# 検証: 1入力の予算ms、スレッド数、入力数、生成シードの起点
.\build-search-winlibs-20260909\battle_search_checks.exe 300 4 12 8123417
```

検証ターゲットは実際の `dumpTable` と `SearchRequest` を使う。生成した複数入力で
一括リプレイと1ターン単位のリプレイの全状態・全記録を比較し、途中状態、時間予算ゼロ、
行動列の上限、終了済み状態、入力の不変性も確認する。ビルド・実行ログは保存しない。
