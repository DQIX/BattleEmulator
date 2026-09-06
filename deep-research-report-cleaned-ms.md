うん。これ、かなり筋が通ってる。名前を付けるなら **「RNG-position indexed sparse Pareto frontier」** みたいなものになる。ホテル比喩のまま整理すると、かなり綺麗。

まず「世界線」を battle state 全体じゃなくて **乱数位置 `p`** にする。

固定seedなら乱数は一本のテープで、現在のPoCも実際に `RngObs[p]` という形で扱ってる。資料でも、固定seedなら `position=p` が未来乱数列を決めるとしている。

なので外側は本当に、

```text
Hotel[0]
Hotel[1]
...
Hotel[9999]
```

くらいの1D配列でいい。

各ホテル `Hotel[p]` の中に「部屋」がある。

ここで大事なのは、**麻痺組・睡眠組・怒り50%組を上下に順位づけしないこと**。これらは「ロイヤル > スタンダード」ではなく、未来の遷移規則が違うので**別種類の部屋**。

今のsourceを読むと、部屋キー候補はかなり明瞭。

```text
enemy HP rage band
    >= 228
    114..227
    < 114

enemyRage + rageTurns

heroPar + parTurns
heroInactive

heroAcro + acroTurn
heroSC + heroSCT

enemySC
camera counter
```

特にあなたの「25%以下、50%以下、それ以上」は適当な量子化じゃなくて、今の `processRage()` が実際に見ている閾値そのもの。

```wgsl
after * 2 < 456     // 50%
before * 2 >= 456

after * 4 < 456     // 25%
before * 4 >= 456
```

だからこれはかなり自然。

麻痺はboolだけでは足りない。sourceでは `parTurns` を減らして、0以下になったところで解除乱数を消費するので、

```text
非麻痺
麻痺 remaining=4
remaining=3
...
```

は別部屋にする。

同様にrage、acro、special chargeも残りターンがRNG消費や合法行動を変える。camera counterもpositionの進み方を変える。資料側も、future dependencyとしてparalysis/rage/acro/camera等を残すべきだと整理している。

そして部屋の中に「客」を複数入れる。

客自身は**正確なBattleEmulator state**を持ったままでいい。

例えば同じ

```text
p = 1732
rageBand = 25..50%
not raging
not paralyzed
not asleep
camera=2
...
```

という部屋に、

```text
客A: heroHP=65 MP=10 herb=3 enemyHP=180
客B: heroHP=40 MP=16 herb=2 enemyHP=175
客C: heroHP=65 MP=10 herb=3 enemyHP=190
```

みたいに複数人泊まる。

ここで一個のスコア、

```text
score = HP*3 + MP*7 - enemyHP
```

みたいな🤓ヒューリスティクスは使わない。

**Pareto dominance**で見る。

例えば同じ部屋なら、

```text
heroHP : 多い方が良い
MP     : 多い方が良い
herb   : 多い方が良い
enemyHP: 少ない方が良い
```

として、全項目でAがB以上ならBを捨てる。

つまり、

```text
A = HP65 MP16 herb3 enemyHP175
B = HP40 MP10 herb2 enemyHP190
```

ならBはホテルから追い出せる。

一方、

```text
A = HP65 MP5  enemyHP150
B = HP40 MP20 enemyHP180
```

なら優劣がないので両方残す。

これなら「一つの乱数IDへ複数の客を押し込む」というあなたの発想そのもの。

そして**exact BattleEmulatorをそのまま使える**のが強い。

探索は単純にこうなる。

```text
depth t

Hotel[p]
  room R
    guest S
      ↓
全合法actionを exact BattleEmulator で1ターン実行
      ↓
S'
position = p'
      ↓
Hotel[p']
      ↓
roomKey(S')
      ↓
既存guestと比較
```

つまり

$$
S' = T(S,a)
$$

を近似しない。

乱数もdamageも怒りもcameraも麻痺も、全部**正本BattleEmulatorにやらせる**。

近似するのは唯一、

> 「未来を全部保持せず、同じホテル・同じ部屋の中で弱そうな客を捨てる」

ところだけ。

これならWebGPU版みたいにBattleEmulatorそのものをWGSLへ再実装して、C++との差を延々監査する必要もない。

しかもこの構造だとFLEEがものすごく自然。

```text
guest at Hotel[1200]
      ↓ FLEE
exact emulator
      ↓
battle resourcesはほぼ同じ
position=1237
      ↓
Hotel[1237]
```

FLEEはまさに**別ホテルへ移動するための行動**になる。

「攻撃しないから無意味🤓」なんて発想が構造上出てこない。

資料の数式でも一ターンは、

$$
(q,x)\xrightarrow a
\left(F_a(q,x),L^{M_a(q,x)}x\right)
$$

で、状態によって乱数消費量 `M` が変わること自体をtransitionとして扱えばいい、とされてる。

ホテル表現ならほぼそのまま、

$$
(p,\text{room},\text{guest})
\xrightarrow{a}
(p+M,\text{room}',\text{guest}')
$$

になる。

で、怒りが一番面白い。

単に

```text
enemyHPが低い方が優秀
```

としてしまうと危ない。

例えば、

```text
enemyHP=230
enemyHP=300
```

はどちらも「50%以上組」だけど、10damageなら230だけ50%境界を踏んでrage RNGを消費する。

だから230と300を同じ部屋に入れて、

> 230の方がHP低いから300を追放！

とすると、世界線そのものが変わる。

ここはあなたの「怒り強制消費さえ賢くすれば」がまさに核心。

かなり良い処理は、単なるHP bandより一段だけ賢くして、

**そのpositionにおける rage-transition signature**

を部屋キーへ含めること。

例えばその客について、各合法actionを見て、

```text
ATTACK:
  threshold crossing = 50%
  rage RNG consumption = +1
DRAGON:
  threshold crossing = none
  rage RNG consumption = 0
CRACK:
  threshold crossing = 25%
  rage RNG consumption = +2
...
```

みたいなsignatureを作る。

同じ `p` なら乱数は固定だから、各actionのdamage候補もほぼ決定的に出せる。

そうすると、

```text
position
+ status/control mode
+ rage transition signature
```

まで同じ客同士だけ比較できる。

これなら「HP帯が同じだけど、次の攻撃で片方だけ怒り閾値を踏む」という厄介なのをかなり分離できる。

これはweighted heuristicではなく、**BattleEmulator自身から作った離散的なcontrol classification**。

ただ、ここには線を一本引いておいた方がいい。

Paretoだけで、

```text
既存客が新客を完全dominance
```

したときだけ捨てるなら、客数の上限は保証されない。

つまり、

$$
\text{world size}
=
\sum_p
\sum_{\text{occupied rooms}}
\text{Pareto width}
$$

になる。

実際に平均で

```text
10000 positions × 10 guests
≈ 100,000 states
```

になれば大成功だけど、理論的に必ず10万にはならない。

もし、

> 各部屋最大8人

などを入れて9人目を必ず追い出すなら、それがbeam部分。

そこだけが明確に**最適解保証を捨てる場所**になる。

これ、設計上かなり綺麗。

```text
Exact BattleEmulator     ← 正確
RNG tape                 ← 正確
room classification      ← 決定的
exact duplicate removal  ← 正確
Pareto dominance         ← 定義次第でかなり保守的

room capacity K          ← ここだけ近似
```

つまり「どこから先が嘘なのか」が一箇所になる。

今までみたいにWGSL移植、state packing、hash、shard、GPU overflowなどあちこちへ証明上の疑問点を撒き散らさない。

そして実装形としても、かなり小さい。

```cpp
struct Guest {
    SearchState exact;
    Parent parent;
    Action action;
};

struct RoomKey {
    RageBand rageBand;
    bool enemyRage;
    int rageTurns;

    bool paralyzed;
    int parTurns;

    bool inactive;

    bool acro;
    int acroTurns;

    bool specialCharge;
    int specialChargeTurns;

    bool enemySC;

    int camera;

    RageSignature rageSig;
};

Hotel hotels[10000];
```

実際には全組み合わせの部屋を確保しない。

```text
Hotel[p]
    unordered_map<RoomKey, SmallList<Guest>>
```

みたいに**使われた部屋だけ存在**させる。

だから理論的な

```text
rage × paralysis × acro × SC × camera × ...
```

の巨大直積をメモリに作る必要はない。

さらに探索はlayer-by-layerなら、

```text
currentHotels
nextHotels
```

の2世代だけでいい。

`depth`までホテルIDに含めなくていい。

そして、これはあなたが最初に言った

> 乱数上の5000個 × いくつかの行列

という発想ともかなり近い。

厳密には巨大行列を作らず、

```text
p=1732, room=R5
    ATTACK → p=1751, room=R8
    FLEE   → p=1744, room=R2
    HEAL   → p=1757, room=R4
```

という**疎なtransition table**を必要なところだけ覚える感じ。

同じ `(p, room, action)` がまた出たら、control部分の遷移は再利用できる余地もある。ただしguestのexact HP等で結果が変わる部分は正本emulatorをそのまま走らせればいい。ここは無理にキャッシュしなくていい。

なので、お風呂案を一文にするとこう。

> **乱数positionを世界線IDとし、positionごとに未来のRNG消費規則が似たbattle-control stateを部屋分けし、各部屋にはexact BattleEmulator stateのPareto frontierだけを保持する。全遷移は正本BattleEmulatorで実行し、客数が増えすぎた部屋だけ有限幅に切る。**

これは現在の「3億個の完全stateを全部世界として覚える」方式とは根本的に違う。

そしてかなり重要なのが、**この方式ならWebGPUは本質じゃない**。

状態数が本当に10万程度へ落ちるなら、

```text
100,000 states × 最大8 action
```

程度なので、既存C++ exact BattleEmulatorをCPU並列で叩く方が、まず圧倒的に素直。

GPUのためにBattleEmulatorをもう一個実装する必要がなくなる。

ダークライ店長の冷蔵庫で言うと、

今までは「材料3億個に全部個別の棚番号を振って巨大冷蔵庫を建設🏗️」。

今回の案は、

**「産地＝乱数位置ごとにホテルを作り、状態の種類ごとに部屋へ入れて、明らかに上位互換の客だけ残す」**。

これはようやく元の「量子化BattleEmulator」の方向に戻ってる。
