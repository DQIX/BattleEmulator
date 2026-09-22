# ZuoSearch 実装・実測結果

## 対象と採用結果

対象は添付 `BattleEmulator(20260922-030705).zip` の `zuo_v2_new_arugo`。
`webassembly/build.sh` の実対象 `zuo_v6`、`-Dz_lv20=1 -DMULTITHREADING=1` を使用した。
探索本体は新規 `ZuoSearch.cpp` / `ZuoSearch.h`。
採用 variant は **0: hybrid-portfolio**。

既存 DEBUG3 の無引数入力は変更していない。

```text
seed = 0x0d58941f
fixed prefix = [BUFF] = [30]
```

採用版のネイティブ Release 実測は **enemy HP=0 / BattleResult.position=22 /
実装備変更2回 / 8ターン / 54.991 ms**。
初勝利は0.841 ms、最後の改善は36.094 ms、328,134回のターン遷移を実行。
採用候補は全て元の初期状態からの fresh exact replay で比較し、replay不一致による棄却は0件だった。

旧 DEBUG3 / SearchRequest の既定実行は、8ターン・position=22・変更4回・24.439 ms。
旧版はその既定の世代数等で動かした参考値であり、1500msに延長した旧版の測定ではない。
新旧で position は同じだが、実装備変更回数が4回から2回になった。

今回の採用列（実測結果であり、探索コードに埋め込んではいない）：

```text
30, 36, 62, 62, 62, 62, 65569, 34
スカラ〔固定prefix〕 → ベホイミ → ためる×4
→ 素手に変更してすてみ → 槍を装備してさみだれづき
```

最後のさみだれづきは1140ダメージ。dumpTableのehp列は各ターン開始時の値なので、
撃破の判定には同じfresh replayの最終Player.hp=0を使用する。

## 比較した探索 variant

同じ上記入力、同じ1500ms総予算で比較。全行で enemy HP=0、fresh exact replay一致。
内部で複数passを回す場合も、一つのdeadlineを共有する。
終了した枝刈り探索を無意味に繰り返さず、完了した場合は予算を使い切る前に戻る。

| ID | 方式 | exact position | 実装備変更 | ターン | elapsed ms |
|---:|---|---:|---:|---:|---:|
| 0 | hybrid-portfolio | 22 | 2 | 8 | 54.991 |
| 1 | balanced-beam | 22 | 2 | 8 | 1480.040 |
| 2 | burst-beam | 22 | 2 | 8 | 1480.040 |
| 3 | survival-beam | 22 | 2 | 8 | 1480.050 |
| 4 | damage-beam | 22 | 2 | 8 | 1480.160 |
| 5 | tactical-beam | 22 | 2 | 8 | 1480.020 |
| 6 | best-first | 22 | 2 | 8 | 1480.060 |
| 7 | zero-change-beam | 25 | 0 | 9 | 1480.010 |
| 8 | wide-balanced | 22 | 2 | 8 | 1480.050 |
| 9 | wide-burst | 22 | 2 | 8 | 1480.030 |
| 10 | bounded-dfs | 22 | 2 | 8 | 193.412 |

初期実装では異なる評価関数のbeam、装備固定lane、広幅beam、best-firstを比較した。
次に楽観的なダメージ上限による枝刈りとDFSを追加し、同じ最良値への到達・探索終了を短縮した。
最終variant 0は、小幅beamで勝利候補を確保してから枝刈りDFSを実行する。
DFSが予算内に終わらない入力では、残り時間内で評価関数を変えたbeam、幅拡大、
その実行中に見つかったsuffixの修復を続ける。最終比較は常にposition、次いで実装備変更回数。
数学的な大域最適性を報告するものではない。

## 別の現在状態での実測

全行で採用variant 0、enemy HP=0、prefix維持、fresh exact replay一致。
新しいテスト基盤は作らず、同じDEBUG3 → SearchRequestのbenchmarkオプションで実行した。

| seed / fixed prefix | prefix長 | exact position | 実装備変更 | elapsed ms |
|---|---:|---:|---:|---:|
| `0x0d589420 / BUFF` | 1 | 31 | 2 | 165.407 |
| `0x0d589421 / BUFF` | 1 | 19 | 2 | 7.477 |
| `0x0d589427 / BUFF` | 1 | 22 | 2 | 28.026 |
| `0x0d58941f / BUFF,MIDHEAL,PSYCHE_UP_ALLY` | 3 | 22 | 2 | 1.657 |

## 実装上の要点

- 1ターン遷移は実際のBattleEmulator::Mainを記録モードで実行する。
  RNG cursor、NowState、Player全メンバーを保持し、hash一致後も全メンバーで同一stateを確認する。
  RNG cursorとBattleResult.positionは別々の値として扱う。
- `-1`までの固定prefixをそのまま取り込む。探索・suffix修復ともprefixの後から開始する。
  勝利候補はoriginal world + unchanged prefix + searched suffixをfresh replayし、
  1ターンずつの結果と連続実行の結果を照合してから採用する。
- 装備変更回数はfresh replayのACTION_EQUIPMENT_CHANGEDを数える。
  予定変更回数やRNGカーソルで結果を置き換えない。表示にも採用候補の同じreplayを使用する。
- 休み持越しはターン境界のPlayer.inactive / sleeping / paralysisを基準にする。
  持越し時は装備を維持し、FLEEを生成しない。前の表示がInactiveでも、ターン境界に
  フラグが残っていなければ通常のactionと装備変更を展開する。
  ターン開始時に選択可能だったFLEEは、このターン中に休みになったという理由では除外しない。
- 既存の9種の候補を確認したうえで、通常攻撃と所持しているまほうのせいすいを追加した。
  スカラMP3、ベホイミMP4、さみだれづきMP4を実処理から使用。
  さみだれづきは槍装備時のみ。素手から同ターンに槍へ変更して使う枝は残す。
  回復アイテムは実際の所持数を使用。敵技や別武器専用技は追加していない。
- 枝刈り用上限は、現worldの通常攻撃・さみだれづき・ためる・すてみから構成する。
  全命中・会心、MP無制限、敵の妨害なし、装備制約なし等を仮定して過大評価する。
  この上限は実ダメージや状態に書き込まず、候補の最終評価にも使わない。
- BattleEmulator.cppの変更は、持越し休み時のFLEEをATTACK_ALLYに正規化する8行の追加のみ。
  既存の装備変更条件や検証処理は削除していない。HP、MP、装備値、敵AI、RNG、
  ダメージ処理、状態異常の処理、勝利条件の改変はない。

## 接続とビルド

```text
DEBUG3 → 既存SearchRequest → ZuoSearch::Run
ActionOptimizer::RunAlgorithm → ZuoSearch::Run（既存API互換）
wasm_search_dump → buildDumpOutput → ZuoSearch::Run
```

CMake SOURCESとWebAssembly SRC_FILESに新規探索ファイルを追加した。
zuo_v6の対象variantとcompiler definesは変更していない。
再帰探索用のWebAssembly stack容量を1MiBに設定し、大きな探索contextはheap上に置いている。

この環境ではCMake 3.31.6 / GCC 14.2.0 / Releaseで実ターゲット `zuo_lv20_v2` の
compile・link・既存DEBUG3実行に成功した。時刻の未来にある元ZIPのファイルmtimeについて
makeのclock-skew警告が出たが、コンパイルとリンクは完了している。

**WebAssemblyの実ビルド・ブラウザー実行は未確認。**
この環境にemccがなく、SDK取得もgithub.comの名前解決失敗で実行できなかった。
ビルド定義と呼び出し経路の接続はZIPに含むが、WASMでの実行時間を測ったとは主張しない。

プロジェクトrootからのビルド例：

```sh
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build --target zuo_lv20_v2 --config Release
# Emscripten環境を有効にした上で：
bash webassembly/build.sh --branch zuo_v2_new_arugo
```

ネイティブ既存DEBUG3経路での比較用オプション：

```sh
./zuo_lv20_v2
./zuo_lv20_v2 --search-variant 10 --search-budget-ms 1500
./zuo_lv20_v2 --search-seed 0x0d589420
./zuo_lv20_v2 --search-prefix 30,36,62
```

省略時はvariant 0・総予算1500ms・元のDEBUG3 seedとBUFF prefix。
`--search-prefix`は指定された列を新たな固定prefixとして扱い、探索対象へ戻さない。
新規unit test、テストスイート、sanitizer、メモリ整合性検査は追加・実行していない。

`search_benchmark/`に最終比較の生ログ、`zuo_search_changes.patch`に添付ZIPからのコード差分を収録した。
このZIPはソース一式であり、元の.git履歴やネイティブ生成物は含まない。
