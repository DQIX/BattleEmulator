# ガナサダイ2 探索実装・実測結果

## 採用結果

対象は `ganasadai2_new_arugo` の `ganasadai2_gouketu`、`GOUKETU=1`。
採用探索は `Ganasadai2Search::Run` の variant **22**。
敵 HP が 0 の候補について、fresh exact replay の `BattleResult.position`、
同値なら `ACTION_EQUIPMENT_CHANGED` の実記録数の順で比較する。
ターン数・RNG cursor・探索内 heuristic で最終比較を置き換えない。

### 既存 DEBUG3 入力での比較

全行とも開始 HP=200 / MP=80、fixed prefix=`30` (BUFF)。
以下の「変更」は prefix を含む replay 全体の装備変更数。
旧版は添付 ZIP の 6 Table の既定探索処理。新探索は総予算 1500ms。

| seed | 旧 position / 変更 | 新 position / 変更 | 新ターン数 | 新総時間 ms | 新終了 HP / MP |
|---|---:|---:|---:|---:|---:|
| 0x4b539adb（既定） | 22 / 2 | 22 / 2 | 8 | 1401.40 | 34 / 65 |
| 0x4b539adc（既存 +1） | 23 / 4 | 22 / 2 | 8 | 1400.99 | 55 / 69 |
| 0x4b539add（既存 +2） | 25 / 6 | 25 / 2 | 9 | 1402.47 | 14 / 65 |
| 0x4b539ae1（既存 +6） | 28 / 2 | 28 / 0 | 10 | 1403.24 | 71 / 41 |

全採用解で enemy HP=0。上記最終実行の replay 不一致棄却数は全て 0。
既定入力では旧版の position=22 を下回れなかった。別 seed では position または同 position の装備変更数が改善した。
最適性の証明ではなく、予算内に得られた best の比較である。

測定はこの Linux コンテナの GCC 14.2.0 / CMake Release、実コンパイラフラグは
`-O3 -DNDEBUG`。ログ冒頭の `No optimization` は既存のバナー判定によるもので、
実際の Release フラグとは一致していない。バナー判定自体は変更していない。
共有実行環境なので探索ノード数・時間には変動がある。Windows / ブラウザーでの同一性能は未測定。

### 既定入力の採用行動列

```text
30, 34, 65567, 65598, 65598, 65598, 65569, 34, -1
```

| ターン | 行動 | 装備 |
|---:|---|---|
| 1 | BUFF（fixed prefix） | 装備あり |
| 2 | MULTITHRUST | 装備あり |
| 3 | MAGIC_MIRROR | 素手へ変更 |
| 4 | PSYCHE_UP_ALLY | 素手 |
| 5 | PSYCHE_UP_ALLY | 素手 |
| 6 | PSYCHE_UP_ALLY | 素手 |
| 7 | DOUBLE_UP | 素手 |
| 8 | MULTITHRUST | 装備ありへ変更 |

これは実測出力の記録であり、探索器にこの seed や行動列を埋め込んでいない。

## 探索予算の使用

1500ms は呼び出し全体の budget。最初の勝利で停止せず、約1400msを実際の探索へ使い、
最大100msを終了処理・返却・スケジューリング変動の余裕として確保する。
余裕時間を sleep 等で消費する処理はない。予算の起点は prefix replay より前。
複数探索を各1500msずつ実行して合計時間を増やす構成ではない。

最終既定実行では、初勝利 **10.1511ms** に対し総時間 **1401.40ms**。
その間に **79,524** 親状態、**2,341,056** 一手遷移を実行し、**17 pass**、best 更新 **2回**。
一手遷移数は実際の `BattleEmulator::Main` 呼び出し数であり、乱数生成数ではない。
開発中に終了処理込みで約1584msとなった実行があったため、最終版では上記余裕を設けた。

## 探索方式・比較した variant

本体は新規 `Ganasadai2Search.cpp/.h` に分離。
幅128から段階的に拡大する beam search を共通基盤とし、variant 22 は
ダメージ進行重視・テンション蓄積重視・反射重視の探索順を一つの deadline 内で交替する。
戦術段階別の保持枠により、ダメージの出ていない蓄積途中の枝も残す。
最大幅に達した後は tie ordering を変えて探索する。world の RNG は変更しない。

状態には両 Player の全フィールド、NowState、RNG cursor、経過ターンを保持する。
ハッシュ一致だけで状態を同一視せず、全フィールドを比較する。
同一状態では実イベント数・装備変更数の小さい経路を残す。
generation 付きの flat hash table と選択した親への参照を使い、
各ノードへ350手の全行動配列を複製する方式にはしていない。

| variant | 比較内容 | 既定入力の position / 変更 |
|---|---|---:|
| 0 | ダメージ進行重視・global beam | 22 / 2 |
| 1 | 蓄積火力重視・戦術別保持 | 22 / 2 |
| 2 | 反射重視・戦術別保持 | 22 / 2 |
| 3 | 蓄積への加点を弱めた順序 | 22 / 2 |
| 4, 5 | 装備変更0回に制限した比較 | 25 / 0 |
| 6, 7 | 反射・ためる・すてみ・さみだれ中心の候補集合 | 22 / 2 |
| 8, 9 | HPへの加点なしでHP帯別の保持枠も確保する比較 | 22 / 2 |
| 10 | 将来の反射・蓄積・すてみ・攻撃計画を見積もる順序 | 22 / 2 |
| 11 | 蓄積順序と将来計画順序の交替 | 22 / 2 |
| **22** | **0/1/2を共通deadline内で交替する採用構成** | **22 / 2** |

装備変更0回の比較は position が悪化したため採用しなかった。
将来計画型等でも追加の改善が出なかったため、比較を終了した。
0〜11は比較用に選択可能だが、通常実行・WebAssemblyの既定は22。

## 入力・行動条件

開始 HP200 / MP80、最大 HP301 / MP161 の既存値を変更していない。
HP200やMP80を探索途中の下限として使わず、HP/MP残量を評価値へ加点しない。
生存した低HP状態は保持対象であり、最終HPや最終MPの追加条件もない。
MPは各行動の実消費量と比較する。例えば MULTITHRUST=4、BUFF=3、
DEFENDING_CHAMPION=3、MAGIC_MIRROR=4。既存の回復行動・アイテム候補も残した。

ATTACK_ALLYは装備ありの一手 terminal probe としてのみ扱い、
そのターンで倒せない場合は探索の次状態へ入れない。
MULTITHRUSTは素手候補を作らない。装備変更不能状態の実装も枝生成側で扱う。
ターン開始へ inactive / sleeping / paralysis が持ち越されているときは、
現在と違う装備の候補を作らない。同状態のFLEEも候補から外す。
BattleEmulator側のFLEE処理や既存の検証処理は変更していない。

prefixは配列の`-1`までを一般に処理する。prefixを削除・変更・再探索しない。
4手prefix `30,34,65567,65598` でも、先頭4手を保持して
8ターン / position22 / 装備変更2回を返した（1400.67ms）。
ターン1から探索し直してはいない。

各採用候補は、元の Player 値・RNG cursor=1・NowState=0と完全行動列から
独立に full replay し、HP、MPを含む両 Player 全体・NowState・RNG cursor・
BattleResult.position・装備変更数を探索状態と照合する。
記録上の通常攻撃も最終ターンのみ、通常攻撃・さみだれも装備ありであることを確認する。
不一致候補は採用しない。

## 接続と変更ファイル

```text
DEBUG3（既存のseed・prefix既定値）
  -> SearchRequest
     -> Ganasadai2Search::Run(variant=22, budgetMs=1500)

wasm_search_dump
  -> buildDumpOutput
     -> SearchRequest
        -> 同じ Ganasadai2Search::Run

ActionOptimizer::RunAlgorithm / RunAlgorithmAsync
  -> 同じ Ganasadai2Search::Run
```

新規: `Ganasadai2Search.cpp`, `Ganasadai2Search.h`。
接続変更: `ActionOptimizer.cpp/.h`, `main.cpp`, `CMakeLists.txt`, `webassembly/build.sh`。
旧探索は `RunAlgorithmLegacy` と比較用 `--search-variant -1` でのみ参照できる。
通常のSearchRequestが旧6探索を実行する構成にはしていない。

dumpTableは採用したfresh replayだけから生成し、prefixを含む完全行動列を表示する。
そのために、終端ターンで片側の行動しか記録されない場合のHP持越表示と、
Tab表示の1ターンずれも表示側だけで修正した。

BattleEmulator.cpp/.h、Player.h、BattleResult.h、Equipment.h、lcg.cpp/.h、
camera.cpp/.hは元ZIPとバイト一致を確認した。戦闘計算・敵AI・乱数・装備値は変更していない。

## ビルド・再現

ネイティブの対象ターゲットは `newDirectory_gouketu`。

```sh
cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=Release
cmake --build build --target newDirectory_gouketu
```

Windowsでの既定入力の実行例:

```powershell
.\build\newDirectory_gouketu.exe --search-variant 22 --search-ms 1500
.\build\newDirectory_gouketu.exe --search-variant -1
.\build\newDirectory_gouketu.exe --search-seed 0x4b539adc --search-variant 22
.\build\newDirectory_gouketu.exe --search-prefix "30,34,65567,65598"
```

引数なしでも既存DEBUG3のseed/prefixで採用探索を実行する。
`--search-variant -1` は旧探索比較用であり、旧処理には総時間制御がない。

WebAssemblyの `SRC_FILES` に新規探索を追加し、既存の大きいBattleResultレコードを
使うreplay呼び出し用にスタックを2MiB指定した。export名・呼び出し経路は維持。

```sh
bash webassembly/build.sh --branch ganasadai2_new_arugo
```

**WebAssembly実ビルドは未確認**。この環境では `emcc: command not found` で停止した。
ビルド定義と経路の接続は変更済みだが、WASMの実コンパイル・ブラウザー実行を
検証済みとは扱っていない。ネイティブReleaseのビルドと実行は確認済み。

`search_benchmarks/` に採用解・旧版比較・開発variant比較のログを同梱。