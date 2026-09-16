# nusisama2\_new\_arugo 専用 BattleEmulator 探索アルゴリズム実装・改善指示書

## 0. 最優先目的 X

X: **`nusisama2_new_arugo`** **のBattleEmulatorについて、与えられた現在状態から固定時間内で最も強い正確な行動列を発見する、ぬしさま2専用の探索アルゴリズムを実装・改善する。**
評価対象は以下だけである。

1. 未知入力に対する正確な勝利解の強さ
2. 勝利までの正確な `BattleResult.position`
3. 固定時間内にその解を発見する速度
4. 同一時間予算での勝利率
5. 実行効率
   既知解は入力として与えない。
   既知seed専用action列、既知prefix専用解、評価ケース専用lookupによって評価を攻略してはならない。
   探索アルゴリズム内部は自由である。
   既存A*を参考にしてもよい。*
   *既存A*を捨てて別の探索器を書いてもよい。
   複数探索器のportfolioでもよい。
   事前学習・再学習も許可する。
   **Git上の以下のbranchはカンニング元として全面的に利用してよい。**

```text
reokonn-GPT6-Pro
bilyouma-gpt6-pro
yo2-gpt6-pro

```

これらの探索実装を読む、コピーする、組み合わせる、改造する、ぬしさま2向けに再学習することを許可する。
一から独創的に書く必要はない。
既に強い実装があるなら積極的に利用すること。
ただし、他branchの既知解そのものをぬしさま2の回答として流用してはならない。
**アルゴリズムのカンニングは許可するが、答えのカンニングは禁止する。**
利用可能な作業時間、計算資源、実装労力、思考時間は可能な限りXへ投入すること。
テストコード作成、サニタイザ、メモリ整合性検査、一般品質保証はXではない。
**それらへ時間を割かないこと。**
`personal_context` ツールの使用は禁止する。
**ローカルMCPの使用は禁止する。**

## 1. 対象

対象は、

```text
nusisama2_new_arugo

```

である。
これは**ぬしさま2専用**の探索アルゴリズム実装・改善作業である。
他ボスの世界仕様へ変更しない。
他branchを最適化対象にしない。
ただし、

```text
reokonn-GPT6-Pro
bilyouma-gpt6-pro
yo2-gpt6-pro

```

については探索技術の参考・コピー元として利用してよい。
最終変更対象は `nusisama2_new_arugo` 側だけとする。

## 2. まず現行実装を読む

探索アルゴリズムを書き始める前に、現在の実装を実際に読むこと。
最低限以下を確認する。

```text
main.cpp
ActionOptimizer.cpp
ActionOptimizer.h
EnhancedCostCalculator.cpp
EnhancedCostCalculator.h
EnhancedHashCalculator.cpp
EnhancedHashCalculator.h
EnhancedHeapQueue.cpp
EnhancedHeapQueue.h
LinearIdPool.h
Genome.h
Player.h
BattleEmulator.cpp
BattleEmulator.h
BattleResult.h
CMakeLists.txt
chatGPT.txt

```

現在の主要探索コードは `ActionOptimizer::RunAlgorithm` を中心としたA\*系である。
`SearchRequest` は現在少なくとも、

```text
TableA
TableB
TableC
TableD
TableF
TableG

```

を使用し、それぞれ、

```text
6000 generations

```

程度で探索する構造を持っている。
ただし、**現在のA\*で使用されている重み・cost tableは、ぬしさま2専用に作られたものではなく、別のBattleEmulator用の重みを流用したものである。**
したがって、現在の、

```text
TableA
TableB
TableC
TableD
TableF
TableG

```

および `EnhancedCostCalculator` 内のparameterは、**参考資料程度に扱うこと。**
これらがぬしさま2に対して適切なheuristicであると仮定してはならない。
現在の重みで探索性能が悪い、勝利解が得られない、探索がほぼ機能しない、といった現象があっても、それだけを理由にBattleEmulatorやSearchRequestの異常と判断してはならない。
**現在A\*が動かない、または弱いことをこのタスクの主問題にしてはならない。**
このタスクの主問題は、

```text
既存A*を修理して以前と同じように動かす

```

ことではなく、

```text
ぬしさま2に対して固定時間内で強い探索アルゴリズムを作る

```

ことである。
したがって、現行A\*が現在の重みでは役に立たない場合、

- 重みをぬしさま2向けに再学習する
- heuristicを捨てる
- A\*そのものを捨てる
- 他branchの探索器へ置き換える
- beamやportfolio等へ移行する
  ことを自由に行ってよい。
  **現行A\*を正常動作させること自体を成果条件にしない。**
  ただし、prefix replay、合法action生成、BattleEmulator呼び出し等にXを直接妨げる明確な接続バグがある場合は、その最小修正を行ってよい。

## 3. 現行A\*は参考baselineとして扱う

現在のA\*探索は、可能であれば比較用baselineとして残す。
ただし前述の通り、現在のweightは別BattleEmulator由来であり、ぬしさま2向けbaselineとして十分な性能を持つ保証はない。
したがって、

```text
current A*

```

を絶対的な基準実装、正解実装、完成済み探索器とは扱わない。
現行A\*が実行可能なら、

```text
current A*
vs
new algorithm

```

を比較してよい。
現行A\*が現在weightではまともな勝利解を出さない場合、その修理に長時間を使う必要はない。
その場合は構造・速度・実装上の参考として利用し、より有望な新探索器へ進むこと。
新方式は原則として独立新規ファイルへ実装すること。
例えば、

```text
NusisamaSearch.cpp
NusisamaSearch.h

```

あるいは、

```text
ActionSearchNusisama.cpp
ActionSearchNusisama.h

```

等でよい。
ファイル名は自由である。

## 4. 現在の合法action集合を入力仕様として扱う

現行 `ACTION_TABLE` には少なくとも以下が存在する。

```text
MIDHEAL
DEFENDING_CHAMPION
MAGIC_MIRROR
MORE_HEAL
FULLHEAL
SPECIAL_MEDICINE
FLEE_ALLY
DOUBLE_UP
PSYCHE_UP_ALLY
BUFF
MULTITHRUST
DEFENCE
GOSPEL_SONG
INSULATE

```

build条件によって追加actionが存在する場合は、実際のbranch側の合法action集合を正とする。
それぞれのconditionも入力仕様である。
例えばMP不足、buff状態、テンション、specialCharge、所持アイテム等による合法性条件を探索都合で変更しない。
他branchの探索器をコピーした場合も、他branchのaction集合へ置き換えてはならない。
**ぬしさま2の合法action集合を使用すること。**

## 5. INSULATEの扱い

`INSULATE` はこの探索において、通常の戦術的な防御効果を期待して積極利用する必要はない。
**この戦闘では実質的に意味のないactionとして扱ってよい。**
ただし、`INSULATE` を合法action集合から削除してはならない。
理由は、`INSULATE` がBattleEmulator上で乱数を消費し、**将来のRNG状態をずらすための乱数調整actionとして利用できる可能性があるため**である。
したがって、

```text
INSULATEは戦術効果が薄い

```

ことを理由に探索候補から完全除外してはならない。
以下は許可する。

- 通常の戦術branchでは低priorityにする
- 防御価値をほぼ0として評価する
- RNG調整候補として別枠で保持する
- RNG-shift macroとして利用する
- 将来の会心、敵行動、テンション成功等のRNG位置を変える目的で選択する
- portfolioの一部だけINSULATEを展開する
  つまり、

```text
戦術として弱い
!=
探索上無価値

```

である。
**乱数調整目的でのINSULATE使用は合法とする。**
最終的な価値は正規BattleEmulatorによるexact replayで判断すること。

## 6. 他branchから全面的にカンニングしてよい

Git上の、

```text
reokonn-GPT6-Pro
bilyouma-gpt6-pro
yo2-gpt6-pro

```

から、探索技術を自由に持ち込んでよい。
許可するものには少なくとも以下を含む。

- 探索器本体のコピー
- A\*改善
- weighted A\*
- beam search
- adaptive beam
- portfolio
- tactical phase
- phase quota
- tail repair
- local repair
- best-first
- transposition table
- exact duplicate verification
- dominance pruning
- branch-and-bound
- action ordering
- state scoring
- learned heuristic
- learned value
- learned action prior
- BattleResult.position予測
- state representation
- path representation
- candidate representation
- memory layout改善
- buffer reuse
- partial selection
- `nth_element`
- flat hash
- arena allocator
- anytime search
- time management
- first-victory高速化
- parameter tuning方法
- parameter search
- training方法
- benchmark方法
- 複数探索器のhybrid
  複数branchの良い部分を一つのぬしさま2探索器へ混ぜてもよい。
  ただし、他branch固有の、

```text
合法action
敵HP
敵AI
Player stats
boss固有status
既知seed
既知action列
既知最短解

```

はぬしさま2へ持ち込まない。

## 7. 完全独自探索も許可する

既存branchの方式より有望な方式を考えた場合は、完全独自探索を書いてよい。
例えば以下を自由に検討してよい。

- A\*
- weighted A\*
- anytime A\*
- best-first
- beam
- adaptive beam
- beam + A\*
- beam + tail repair
- IDA\*
- iterative deepening
- tactical macro
- portfolio
- rollout
- MCTS
- progressive widening
- branch-and-bound
- learned policy
- learned value
- learned remaining-position estimate
- learned action prior
- transposition table
- dominance relation
- exact state compression
- partial expansion
- SIMD
- cache locality改善
  アルゴリズム名やコード量は評価しない。
  **同じ固定時間で実際に強いかだけを評価する。**

## 8. SearchRequestが受け取る初期世界を探索rootにしない

最重要制約である。
`SearchRequest` が受け取るPlayer等は初期世界である。
しかし、それを理由に戦闘最初から探索をやり直してはならない。
現在の `ActionOptimizer::RunAlgorithm` は既存action prefixを、

```cpp
BattleEmulator::Main(...)

```

でreplayし、

```text
Player state
Enemy state
RNG position
nowState
current turn

```

を現在状態まで進めている。
探索rootはここである。
概念的には、

```text
initial world
↓
given fixed prefixを正確にreplay
↓
CURRENT STATE
↓
ここからsuffixだけ探索

```

である。
以下は禁止する。

- turn 1から探索し直す
- prefixを変更する
- prefix内actionを探索対象にする
- prefixを短くする
- prefix以前の乱数を変更する
- PastTurnsを偽装する
- SearchRequestの初期Playerをそのまま探索rootにする
  これはすべて**報酬ハッキング**である。

## 9. SearchRequestパーサー等の未完成を異常扱いしない

このbranchには未完成部分が存在していてもよい。
`SearchRequest`、入力パーサー、CLI、WASM経路、未接続機能等が未完成でも、**それ自体を異常扱いしないこと。**
現在A*が弱い・勝利を見つけられないことも、それだけでは異常ではない。*
*特に現在A*のheuristic weightは別BattleEmulator由来なので、現在探索が有効に働かないことを「まず直すべき重大バグ」と扱わない。
このタスクはシステム全体を完成させる仕事でも、既存A\*を復旧する仕事でもない。
目的はXである。
以下をしない。

- 未完成箇所を見つけて全面修正へ脱線する
- parserを作り直す
- CLI全体を再設計する
- WASM interfaceを完成させる
- 関係ないTODOを解消する
- 未使用機能を完成させる
- 現行A\*を「正常に動作する状態」へ戻すことだけに長時間を使う
  探索アルゴリズムを実行・比較・提出するために必要な最小限の接続だけを行う。
  **未完成であること、既存heuristicが不適切であること、Xを阻害する明確なバグは区別すること。**

## 10. 世界側を書き換えない

探索を強く見せるためにBattleEmulatorの世界を書き換えてはならない。
少なくとも以下は入力仕様である。

- enemy HP
- ally HP
- max HP
- MP
- attack
- defence
- speed
- item数
- damage formula
- heal formula
- critical rate
- counter rate
- evasion
- tension
- buff
- Insulate
- Magic Mirror
- specialCharge
- enemy AI
- enemy action
- enemy action順序
- status
- action成功率
- RNG algorithm
- RNG consumption
- RNG cursor
- nowState
- BattleResult
- `BattleResult.position`
- 勝利条件
- action legality
- SearchRequestが構築する現在状態
  探索用heuristic、cache、value、policy等は自由である。
  それらを本物のBattleEmulator状態へ書き戻してはならない。

## 11. 敵技を利用する報酬ハッキングを禁止

BattleEmulator内部に敵技が実装されていても、

```text
BattleEmulatorが実行できる
!=
味方が合法的に使用できる

```

である。
以下は禁止する。

- enemy actionを味方へ与える
- 敵専用技を探索候補へ追加する
- action IDを直接注入する
- actor/action所属を無視する
- ACTION\_TABLE外の技を使う
- action validationを迂回する
  敵技を使って敵を高速撃破する方式は失格である。

## 12. FLEE既知バグを利用しない

FLEEを選択した場合にpre-action処理が無条件skipされる挙動は正しい仕様として扱わない。

```text
ROM上でFLEEを選択できる
!=
麻痺、睡眠、inactive等を無視してFLEEを実行できる

```

である。
FLEEによって通常のpre-action処理を不正に回避する探索解を使用しない。
この既知バグを修正する必要がある場合はROM挙動へ合わせる最小修正だけを許可する。
この例外を世界全体の改造理由にしない。

## 13. 勝利条件

勝利条件は**敵全員のHPを0にすること**である。
敵HPが1以上なら勝利ではない。
最終目標は概念的に、

```text
minimize exact BattleResult.position
subject to:
    every enemy HP == 0

```

である。

## 14. BattleResult.positionを主要目的値としてよい

勝利解同士は正確replay後の `BattleResult.position` で比較してよい。

```text
smaller exact BattleResult.position
=
より早く敵全員のHPを0にした
=
より良い勝利解

```

とする。
単純なturn数より `BattleResult.position` を優先してよい。
現在の `SearchRequest` は、

```text
turn
↓
position

```

の順で比較していても、この順序を探索アルゴリズムの絶対仕様とはしない。
**最終目的がより小さい** **`BattleResult.position`** **なら、それを直接最適化する方式を検討してよい。**

### BattleResult.positionとRNG positionを混同しない

以下は別物である。

```text
BattleResult.position

```

と、

```text
Genome.position
BattleEmulator::Mainへ渡すposition

```

である。
後者はRNGカーソルである。
INSULATE等でRNG positionを調整することは許可されるが、RNG position自体を小さくすることは最終目的ではない。

## 15. RNG調整を探索戦術として積極的に使ってよい

このBattleEmulatorでは、actionごとに異なる乱数消費が発生する。
したがって探索は単なる、

```text
現在damageが最大の技を選ぶ

```

問題ではない。
将来の、

- 会心
- enemy action
- damage roll
- tension成功
- specialCharge
- 回避
- その他RNG依存イベント
  を有利にするため、現在ターンであえて直接価値の低いactionを使用することを許可する。
  特に、

```text
INSULATE
FLEE_ALLY
DEFENCE
その他合法な低damage action

```

等がRNG調整として意味を持つ場合、その経路を探索してよい。
ただしFLEE既知バグの悪用は禁止する。
乱数調整は**正規BattleEmulatorの自然なRNG消費を利用すること**に限る。
RNG cursorを直接書き換えてはならない。

## 16. 事前学習・再学習を許可

ぬしさま2専用の事前学習を許可する。
未知入力相当の状態を大量生成し、

- action prior
- state value
- victory probability
- expected remaining BattleResult.position
- expected damage
- tension value
- double-up value
- buff value
- mirror value
- healing value
- MP value
- RNG adjustment value
- INSULATEによるRNG shiftの価値
  等を学習してよい。
  ただし、

```text
seed -> best actions
prefix -> best suffix
RNG state -> known answer

```

という回答lookupは禁止する。
評価データへの漏洩を避けること。

## 17. 現行A\*の重みは参考資料にすぎない

**現行** **`EnhancedCostCalculator`** **の重みは別BattleEmulator由来であり、ぬしさま2向け最適値ではない。**
したがって、以下を前提にしないこと。

```text
現在Table A/B/C/D/F/Gが合理的
現在hCostが勝利方向を正しく表す
現在の重みが調整済みbaseline
現在A*が勝てないなら実装バグ

```

これらは成立しない可能性が高い。
現行weightは、

```text
探索コードの構造を理解する
既存parameter形式を把握する
新parameterの初期値候補にする
他方式と比較する

```

程度の参考にしてよい。
一方で、

```text
現行weightをどうにか調整して勝てるようにする

```

ことへ固執する必要はない。
より強い方式があるなら早期にそちらへ移行する。

## 18. 現行A\*の構造は改善しても捨ててもよい

現行A\*には少なくとも、

```text
EnhancedHeapQueue
EnhancedHashCalculator
EnhancedCostCalculator
LinearIdPool<Genome, 50000>
unordered_set<uint64_t> closedSet

```

等がある。
構造が有用なら以下を検討してよい。

- closedSetの意味
- state identity
- hash設計
- action履歴をhashへ含める必要性
- transposition table
- dominance relation
- heap cost
- Genome copy cost
- pool size
- pool layout
- cache locality
- branch ordering
- gCost
- hCost
- Table A/B/C/D/F/Gの重複
- portfolio allocation
- current incumbent利用
- victory後の改善探索
- exact `BattleResult.position` 予測
- dead state pruning
- resource dominance
- anytime化
  ただし、**現行A\*そのものを救済することを主目的にしない。**
  構造上の改善余地を調べて有望なら使う。
  有望でなければ捨ててよい。

## 19. Table A/B/C/D/F/Gを維持する義務はない

現在は6つのcost tableを走らせている。
この構成は仕様ではない。
しかも現在weightは別BattleEmulator由来なので、これら6tableを保存すること自体に価値があるとは限らない。
以下を自由に行ってよい。

- 全部維持
- 一部削除
- 全部削除
- parameter再学習
- Table追加
- 一つの学習済みheuristicへ統合
- A\*とbeamのportfolioへ変更
- 他branch探索器と置換
- 複数variantへ時間配分
  重要なのは同じ固定時間で強いことである。

## 20. テストコードを書かない

**この作業では新規テストコードを書かない。**
以下は禁止する。

- unit test
- integration test
- regression test
- test suite
- test framework
- fixture
- mock
- property test
- fuzz test
- coverage
- CI test追加
- テスト専用architecture
  **テスト作成へ使う時間はXに対して無価値なので使用しないこと。**
  一方、

```text
baseline実行
optimized実行
benchmark
A/B比較
exact replay
dumpTable
未知入力評価

```

は探索性能を評価する本作業なので実施する。

## 21. サニタイザ・メモリ整合性検査をしない

以下を行わない。

```text
ASan
UBSan
MSan
TSan
LeakSanitizer
Valgrind
Dr. Memory
Application Verifier
PageHeap
heap verifier
memory scanner
memory leak checker
memory instrumentation
guard allocator

```

また、

```text
未定義動作がないか総点検
メモリ破壊がないか総点検
リークがないか総点検

```

といった一般的検査も行わない。
**このタスクではサニタイザ・メモリ整合性検査へ時間を割かない。**
通常実行中にXを阻害する明確なcrashが発生した場合のみ、その具体的再現を通常debugし、必要な最小修正を行う。
そこから一般検査へ広げない。

## 22. 一般的な品質保証に脱線しない

以下を目的化しない。

- refactoring
- style cleanup
- warningゼロ化
- documentation整備
- test coverage
- sanitizer clean
- leak free証明
- static analysis全件修正
- unrelated TODO解消
- 未使用コード整理
  Xへ直接寄与する変更だけを行う。

## 23. 正しさを犠牲にした高速化は禁止

以下は禁止する。

- BattleEmulatorを簡易モデルへ置換
- enemy AIを近似して最終stateを作る
- RNG消費を近似
- enemy actionを省略
- status処理を省略
- action legalityを省略
- Player fieldを勝手に削る
- 粗いbucketだけでexact state扱い
  近似モデルはbranch orderingやvalue predictionには利用してよい。
  最終遷移は正規BattleEmulatorを用いる。

## 24. exact replay必須

探索候補を最終結果として採用する前に、元入力から、

```text
initial world
↓
fixed prefix
↓
searched suffix

```

を正規 `BattleEmulator::Main` で完全replayする。
最終的な、

- enemy HP
- ally HP
- MP
- status
- buffs
- tension
- RNG cursor
- nowState
- BattleResult
- `BattleResult.position`
  はこのexact replayを正とする。
  探索内部stateとreplayが食い違う候補は採用しない。
  これはtest suiteではない。
  **探索結果そのものの評価処理である。**

## 25. dumpTableを最優先で出力

最終候補はexact replay後のBattleResultを使用し、repositoryに存在する実際の `dumpTable` signatureを使用して出力する。
探索の都合でdumpTableを再設計しない。
出力順は原則、

```text
[dumpTable]
[短い結果概要]
[比較結果]

```

とする。
tableより先に大量ログを出さない。

## 26. 公平な固定時間比較

比較可能な探索器同士は同じ条件で比較する。

```text
same initial world
same fixed prefix
same current state
same seed
same legal actions
same BattleEmulator
same CPU
same wall-clock budget

```

を使用する。
ただし、**現在A*****が別BattleEmulator由来weightのため実用にならない場合、現在A*****に勝つことだけを目的にしない。**
本当の目的は、未知入力に対して固定時間内で強い探索器を作ることである。
必要に応じて、

```text
250ms
500ms
1000ms
1500ms
2000ms

```

等の複数budgetで評価してよい。

## 27. 未知入力で評価

複数seed、複数prefix、複数current stateを使用する。
developmentとholdoutを分離してよい。
holdout結果を見て再学習しない。
最低限、

```text
victory
exact BattleResult.position
suffix length
first victory time
final best time
expanded states
wall time

```

等を比較してよい。
最重要なのはnodes/secではなく、**同時間で得られる正確な解の品質**である。

## 28. 複数variantを積極的に作る

例えば、

```text
variant 0: current nusisama A*（参考）
variant 1: A* weight再学習
variant 2: A*構造改善
variant 3: reokonn-GPT6-Pro移植
variant 4: bilyouma-gpt6-pro移植
variant 5: yo2-gpt6-pro移植
variant 6: 3branch hybrid
variant 7: beam + tail repair
variant 8: learned portfolio
variant 9: RNG-adjustment-aware search
variant 10: 完全独自探索

```

等を競わせてよい。
**variant 0がまともに動かないことに時間を使いすぎないこと。**
現行weightは別BattleEmulator由来なので、それ自体は想定内である。
最初の案に固執しない。

## 29. 性能が頭打ちになったら提出する

**永遠に改善を続けないこと。**
探索アルゴリズムの性能向上が明らかに頭打ちになった時点で、現在のbest成果を保存して提出する。
頭打ちの目安は例えば、

- 複数の有望variantを試してもbestが更新されない
- parameter tuningを続けてもholdout改善がほぼない
- wideningしても解品質が上がらない
- 他branch方式を移植しても現bestを超えない
- 追加計算量に対して改善が極小
- 同じBattleResult.positionへ収束する
- 直近の複数改善案がすべて同値または悪化
  等である。
  厳密な数学的最適性証明は必要ない。
  **十分に攻めた後、実測上これ以上の改善見込みが薄ければ提出する。**
  頭打ち後に、

```text
現行A*を正常化する
テストを書く
sanitizerを回す
memory checkerを回す
一般品質保証を始める

```

ことは禁止する。
頭打ちになったら**ZIPを作り、REPORT/HANDOFFを書き、提出すること。**

## 30. 十分な成果が早く出た場合も提出してよい

性能頭打ちまで必ず全案を試す必要はない。
例えば、

- 現行A\*よりはるかに強い探索器が完成
- 複数未知ケースで安定して勝利
- BattleResult.positionを大幅改善
- 同品質を大幅高速化
- holdoutで一貫して強い
  等の十分な成果が得られ、さらに攻める費用対効果が低いなら提出してよい。
  **無限にfollow-up改善を繰り返さない。**

## 31. 報酬ハッキング禁止

以下は禁止する。

- turn 1から再探索
- prefix変更
- PastTurns偽装
- current state変更
- enemy HP変更
- ally HP変更
- MP変更
- item変更
- enemy AI変更
- enemy action変更
- damage formula変更
- critical/counter/evasion率変更
- tension成功率変更
- RNG algorithm変更
- RNG cursor直接変更
- nowState変更
- BattleResult.position偽装
- 勝利判定変更
- illegal action追加
- enemy skill使用
- exact replay省略
- optimizedだけ長時間実行
- 既知seed専用分岐
- 既知prefix専用分岐
- 既知最適action列埋め込み
- 評価case識別
- holdoutへのデータ漏洩
- FLEE既知バグ悪用

## 32. INSULATE利用は報酬ハッキングではない

`INSULATE` は合法actionであるため、正規BattleEmulator経由で普通に選択する限り報酬ハッキングではない。
直接防御価値がなくても、

```text
INSULATEを選択
↓
正規のRNG消費
↓
将来RNGが変化
↓
より良い合法な戦闘結果

```

という経路は有効な探索解として認める。
ただし、

```text
RNG cursorを直接進める
INSULATEを実行せず消費だけ加算する
BattleEmulator外で乱数位置を書き換える

```

ことは禁止する。

## 33. 改善しない変更は捨てる

実装したから採用する、という判断をしない。
新方式が、

- 勝率悪化
- BattleResult.position悪化
- 同時間で弱い
- 一部known caseだけ強い
  なら採用しない。
  コード量は成果ではない。
  **未知入力での実測性能が成果である。**

## 34. checkpoint ZIPを定期的に作る

有益な成果が出た節目でZIPを作成する。
例えば、

- 新探索器動作
- 初めて安定勝利
- 新best position
- 有力parameter完成
- holdout改善
- 頭打ち到達
- 最終候補決定
  等でcheckpointを残す。
  現在A\*の復旧だけでは、原則として重要checkpointとはみなさない。
  毎回無意味にZIPする必要はない。
  **失うと大きな時間を再消費する成果を残す。**

## 35. サンドボックスは提出ごとに初期化される前提

**作業サンドボックスは提出後に次のセッションへそのまま引き継がれない前提で行動すること。**
長時間作業してもZIPがなければ成果を再利用できない。

```text
1時間作業
↓
有益なコード完成
↓
ZIPなし
↓
サンドボックス消失

```

は、このタスクでは実質的に無価値である。
したがって、価値ある成果は必ずZIPへ保存する。

## 36. ZIPなしで終了しない

有益な成果が存在する場合、ZIPを作らず応答終了してはならない。
最終完成前でもシステム障害・時間切れが近い場合はfailover ZIPを作る。
未完成ならHANDOFFで未完成checkpointと明記する。

## 37. ZIPへ含めるもの

可能な限り以下を含める。

```text
変更後ソース
新探索アルゴリズム
学習済みparameter
benchmark/evaluationに必要な最小コード
比較結果
代表dumpTable
REPORT
HANDOFF
再開に必要な設定

```

以下は不要。

```text
unit tests
test suite
coverage
sanitizer build
sanitizer report
memory checker log
巨大build cache

```

## 38. HANDOFFには必ずZIP絶対パスを書く

引き継ぎ文章には、

```text
Latest checkpoint ZIP:
/mnt/data/...

```

のように、実際に存在する最新ZIPの絶対パスを必ず書く。
複数ある場合は、

```text
Latest recommended ZIP: ...
Previous stable ZIP: ...
Final ZIP: ...

```

と区別する。
HANDOFFには最低限以下を含める。

```text
対象branch
目的X
現行A*の位置付け
現行weightが別BattleEmulator由来であること
実装済み探索器
カンニングしたbranchと利用した技術
現在best variant
現在best BattleResult.position
比較結果
頭打ちか継続余地があるか
未解決点
次回行うべき作業
Latest checkpoint ZIP
Final ZIP（存在する場合）

```

ZIP pathのないHANDOFFは不完全である。

## 39. 最後だけZIPする運用は禁止

長時間作業では障害・時間制限・強制終了があり得る。
「最後にまとめてZIPする」ではなく重要成果の節目でcheckpointを残すこと。
最終まで完走できなくても、有益な最新版を救出できる状態を維持する。

## 40. checkpoint前に必要なのはXの実測だけ

ZIP前に必要なのは、

```text
actual BattleEmulator実行
↓
現在bestまたは比較可能なvariantとの比較
↓
exact replay
↓
有益ならZIP

```

である。
現行A\*がまともなbaselineにならない場合、無理にそれを比較対象として復旧しなくてよい。
テストsuiteは不要。
sanitizerは不要。
メモリ整合性検査は不要。

## 41. 作業優先順位

優先順位は以下。

```text
1. nusisama2_new_arugo現行コードを理解
2. 現行A* weightが別BattleEmulator由来であることを前提に構造だけ評価
3. reokonn-GPT6-Proを読む
4. bilyouma-gpt6-proを読む
5. yo2-gpt6-proを読む
6. 最有望探索技術を選択
7. ぬしさま2へ移植・実装
8. RNG調整を含むぬしさま固有heuristicを構築
9. parameter tuning / 再学習
10. 固定時間で実測
11. exact replay
12. 改善していればcheckpoint ZIP
13. 別variantを攻める
14. 性能改善が頭打ちになるまで有望案を試す
15. 頭打ち、または十分な成果が出たらfinal ZIP
16. REPORT / HANDOFF
17. 提出

```

以下へ時間を割かない。

```text
現行A*を正常化すること自体を目的とした長時間作業
unit test
integration test
regression test
coverage
fuzz
ASan
UBSan
MSan
TSan
Valgrind
Dr. Memory
Application Verifier
PageHeap
memory integrity scan
leak check
一般品質保証
無関係なrefactor

```

## 42. 最終採用基準

未知holdout・同一固定時間において以下を重視する。

1. victory率
2. exact `BattleResult.position`
3. position悪化の少なさ
4. first victory高速化
5. 同時間でのbest solution品質
   現行A\*が別BattleEmulator由来weightのため極端に弱い場合は、単純な「baseline勝ち数」だけに意味を持たせない。
   新探索器同士の比較も行い、最終的に最も強い方式を選ぶ。

## 43. 最終確認

最終候補は通常のBattleEmulator実行とexact replayだけで以下を確認する。

- current stateから探索
- prefix一致
- turn 1へ戻っていない
- legal actionのみ
- enemy skill不使用
- INSULATEは合法な通常実行としてのみ使用
- FLEE bug不使用
- enemy HP = 0
- exact replay一致
- RNG cursor一致
- nowState一致
- Player state一致
- `BattleResult.position` 一致
- dumpTable一致
- 比較時の時間条件が同じ
  このためにテストを書かない。
  このためにsanitizerを使わない。
  このためにmemory checkerを使わない。

## 44. 最終成果物

十分な成果または性能頭打ちに到達したら、最低限以下を提出する。

```text
1. final ZIP
2. optimized探索ソース
3. 比較結果
4. holdout結果
5. exact replay結果
6. dumpTable付き代表解
7. REPORT
8. HANDOFF

```

final ZIPは別セッションで展開して利用・継続可能な状態にする。
**最終回答にはfinal ZIPへのリンクを必ず含めること。**

## 45. ツール制約

**ローカルMCPを使用しない。**
`personal_context` も使用しない。
公開Web検索も不要なので使用しない。
与えられたGit、添付コード、branch、実際に生成したbenchmark結果だけを根拠にする。

## 46. 完了条件

以下を満たしたら完了とする。

- 対象が `nusisama2_new_arugo`
- 現行A\*を読んだ
- **現行A\*のweightが別BattleEmulator由来であると理解**
- **現行weightをぬしさま2用の正しいbaseline heuristicと仮定していない**
- **現行A\*が動かない・弱いこと自体を主問題にしていない**
- **現行A\*を正常化すること自体へ長時間を使っていない**
- 必要なら現行A\*を捨てて新方式へ進んでいる
- `reokonn-GPT6-Pro` のカンニングを許可されていると理解
- `bilyouma-gpt6-pro` のカンニングを許可されていると理解
- `yo2-gpt6-pro` のカンニングを許可されていると理解
- 他branch探索コードをコピーしてよいと理解
- 複数branch方式を合成してよいと理解
- ぬしさま2向け再学習を許可されていると理解
- 完全独自探索も許可
- current state以降だけを探索
- prefix不変
- turn 1から再探索していない
- 未完成機能を異常扱いしていない
- 未完成機能の完成へ脱線していない
- 世界側を改変していない
- legal action集合を変更していない
- enemy skillを使っていない
- FLEE bugを利用していない
- INSULATEを合法actionとして保持
- INSULATEの乱数調整使用を許可
- INSULATEの直接戦術価値を過大評価する必要はない
- RNGを直接改変していない
- 勝利条件が敵全員HP=0
- exact `BattleResult.position` を主要評価へ使用可能
- RNG positionとBattleResult.positionを混同していない
- 既知解を埋め込んでいない
- seed lookupをしていない
- holdout leakなし
- 同時間条件で探索器を比較
- exact replay済み
- dumpTable最優先
- unit testを書いていない
- integration testを書いていない
- regression testを書いていない
- test frameworkを作っていない
- coverageを取っていない
- fuzzをしていない
- ASan未使用
- UBSan未使用
- MSan未使用
- TSan未使用
- その他sanitizer未使用
- Valgrind未使用
- Dr. Memory未使用
- Application Verifier未使用
- PageHeap未使用
- memory integrity検査をしていない
- leak検査をしていない
- 一般品質保証へ時間を割いていない
- Xへ作業時間を優先投入
- 有益成果の節目でcheckpoint ZIP作成
- サンドボックスが次回残ると仮定していない
- HANDOFFに最新ZIP絶対パスあり
- 十分な成果が出たら提出
- **性能が頭打ちになったら無駄に粘らず提出**
- 完走時final ZIP作成
- final回答からZIPへアクセス可能
  最終目的は、**`nusisama2_new_arugo`** **の世界・現在状態・合法action集合を一切変更せず、現行A\*の別BattleEmulator由来weightには固執せず、****`reokonn-GPT6-Pro`****、****`bilyouma-gpt6-pro`****、****`yo2-gpt6-pro`** **の探索技術を自由にカンニング・コピー・合成・再学習し、INSULATEを含む合法actionによる乱数調整も活用しながら、固定時間内でぬしさま2のHPを最も早く0にする正確な未知入力用探索アルゴリズムを実装すること**である。
  **現行A\*が現在のweightで動かない、勝てない、弱いことは主問題ではない。現在weightは別BattleEmulator由来なので参考程度とし、その修理に固執せず、ぬしさま2専用の強い探索器を作ることへ集中する。**
  **使える時間はXへ割く。テスト作成、サニタイザ検証、メモリ整合性検査は行わない。未完成箇所を見つけても異常扱いして無関係な完成作業へ脱線しない。**
  **有望な改善を粘り強く試す一方、性能向上が頭打ちになったらそこで作業を打ち切り、最新成果をfinal ZIPへ保存し、REPORT/HANDOFFとともに提出すること。**
  **ZIPなしで長時間作業を終えることは成果を失うことと同義なので禁止する。**