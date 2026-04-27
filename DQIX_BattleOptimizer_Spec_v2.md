# DQIX バトル行動最適化 探索エンジン設計仕様書

**v2.0 — RNGイベント駆動 IDDFS 方式**
`BattleEmulator.cpp / camera.cpp / Player.h / lcg.cpp 対応版`

---

## 目次

1. [目的](#1-目的)
2. [非採用方針](#2-非採用方針)
3. [全体アーキテクチャ](#3-全体アーキテクチャ)
4. [Phase 0：RNGイベントスキャン](#4-phase-0rngイベントスキャン)
5. [Phase 1/2：DFS / IDDFS 探索](#5-phase-12dfs--iddfs-探索)
6. [行動順序仕様](#6-行動順序仕様rngイベント距離スコア)
7. [安全な楽観上界による枝刈り](#7-安全な楽観上界による枝刈り)
8. [Step API 仕様](#8-step-api-仕様)
9. [メモリ仕様](#9-メモリ仕様)
10. [出力仕様](#10-出力仕様)
11. [実装優先順位](#11-実装優先順位)
12. [最適保証チェックリスト](#12-最適保証チェックリスト)
13. [関連ファイル対応表](#13-関連ファイル対応表)

---

## 1. 目的

DQIXバトルエミュレータにおいて「味方行動列を探索し、敵を最短ターンで撃破する行動を求める」探索エンジンの設計を定義する。

探索空間は分岐数10・深さ10〜15で最大 **10^10〜10^15 ノード**に達するため、完全探索は非現実的である。

本仕様では以下の三段構成を採る。

1. **RNGイベントスキャン** — seed から将来の乱数列を走査し「当たり位置」を先に列挙する
2. **RNGイベント駆動 DFS/IDDFS** — 当たり位置へ到達する行動列を優先的に掘る
3. **安全な楽観上界による枝刈り** — 最適保証を維持しつつ証明探索を高速化する

---

## 2. 非採用方針

### 2.1 A\* 主探索は採用しない

A\* は admissible heuristic があれば最適解を保証できるが、本問題では主探索に採用しない。

| 理由 | 詳細 |
|------|------|
| 会心・RNGスパイク | 強い admissible heuristic を構築困難 |
| 状態再訪がほぼ発生しない | closed set の効果が薄い |
| priority_queue push/pop | 毎ノードのオーバーヘッドが大きい |
| unordered_set hash lookup | 状態コピーと合わせて重い |
| Genome / Player / actions 配列コピー | 各ノードで大きなコピーが発生する |
| hCost が「状態の良さ」を見る | RNGイベント到達性とミスマッチ |

> A\* は実験用・デバッグ比較用・近似解生成の一部としてのみ残す。

乱数の位置から、何個目に会心を引けるかどうかは、簡単に事前に定義できるようにする。
ただし、バトルエミュレーターと異なるシステムがあると、メンテナンスコストが爆増するため、テンプレートでの自動化ができるとなおよし
ただしメインループは2000万回程実行するのでパフォーマンスの影響への考慮は必須

### 2.2 生乱数ランダム近似探索は主戦力にしない

ランダムに行動列を生成して「たまたま良いRNGイベントを踏む」のは非効率である。

本問題では**乱数列が seed で固定されている**ため、先に当たり位置を特定し、そこへ到達する行動列を探す方が直接的である。

```
生乱数ランダム探索  → 当たりを引くのを待つ
RNGイベント駆動探索 → 当たり位置を先に知り、そこへ行動列を合わせる  ← 採用
```

### 2.3 固定5ターン rolling horizon は主探索にしない

窓外の準備行動・RNG調整を見落とすため、初期解生成・探索順序付けの補助としてのみ使用する。

### 2.4 評価関数ベースの支配削除は禁止

会心・specialCharge・アクロバットスター・カメラ乱数消費などにより、低評価に見える状態が未来で逆転する。

`NowState` / `position` / `players[2]` が一致しない限り未来は同一ではないため、評価ベースの支配削除は**禁止**する。

EnhancedCostCalculator.cppは完全に捨てる

---

## 3. 全体アーキテクチャ

### 3.1 全体フロー

```
入力
  players[2] / seed / position / NowState
  actions / currentTurn / maxTurn / 時間制限
        ↓
  目的の位置(既知の行動)まで進める
        ↓
Phase 0: RNGイベントスキャン
  seed から先 500〜2000 個の乱数を走査
  重要イベントの当たり位置表を生成
        ↓
Phase 1: RNGイベント駆動近似探索
  当たり位置を目標とした DFS greedy rollout
  乱数がどの位置で重要なイベントが発生するかは、バトルエミュレーターを実際に実行すると遅いのでなんとかする。バトルエミュレーターは1回あたり300nsかかるので
        ↓
暫定ベスト記録
  bestTurn / bestActions / bestGenome
        ↓
Phase 2: IDDFS + Branch and Bound 証明探索
  bestTurn より短い解の不存在を証明する
        ↓
出力
  最良行動列 / 撃破ターン / 証明済み範囲 / 探索打ち切り情報
```

### 3.2 探索問題の変換

スコア最大化ではなく以下の**決定問題列**として定義する。

```
N ターン以内に敵を倒せるか？（N を currentTurn+1 から順に増やす）
```

近似探索で `bestTurn` が得られている場合、証明対象は `currentTurn+1 〜 bestTurn-1` のみに限定する。

```
例: 近似探索で 12 ターン解が見つかった場合
    → 11 ターン以内に解がないことを証明すれば 12 ターンが最適確定
```

---

## 4. Phase 0：RNGイベントスキャン

### 4.1 目的

探索開始前に seed / position から将来の乱数列を走査し、**重要イベントの当たり位置**を列挙した表を生成する。

これにより探索エンジンは「評価関数で状態を見る」のではなく、**「固定済み乱数列の当たり位置へ行動列を合わせる」** 方針を取れる。

### 4.2 走査対象イベント

| イベント種別 | 判定条件（lcg.cpp の API 基準） |
|-------------|-------------------------------|
| ATTACK_ALLY 会心 | `getPercent(pos, 0x2710) < kaisinnP` |
| DRAGON_SLASH 会心 | `getPercent(pos, 0x2710) < DragonSlashKaisinnP` |
| 呪文暴走相当 | `getPercent(pos, 0x2710) < 100` |
| specialCharge 発動 | `getPercent(pos, 100) < 1` |
| アクロバット スター 回避 | `getPercent(pos, 100) in [0, 49]` |
| アクロバット カウンター | `getPercent(pos, 100) in [50, 74]` |
| カウンター 会心 | `getPercent(pos, 0x2710) < kaisinnP` |
| 敵行動の種類 | `getPercent(pos, 敵行動数)` で決定 |
| 毒 / デバフ など | 各判定閾値に従う |

この時、バトルエミュレーターを実行するとどう頑張っても300ns確定なので、ダメージ計算と、乱数消費を分けるなどするべき。
例えば、どの乱数列がどのように使用されるかというマッピングを作り、ダメージ計算はそれに従って消費するだけなど(この案は重い)
乱数のprecalcTop32から直接条件を満たすか近似で求めるだけならそこまで負荷はかからない。
ただ、分けた分だけ重くなるので、そこは考える。

### 4.3 出力形式

```cpp
struct RNGEvent {
    int       position;   // 乱数 index
    EventType type;       // CRITICAL / SPECIAL_CHARGE / COUNTER / ...
    float     probability; // 0-1（参考値）
};

std::vector<RNGEvent> scanEvents(int startPos, int scanRange, uint64_t seed);
```

出力例：
```
pos 421: ATTACK_CRITICAL
pos 438: SPECIAL_CHARGE
pos 502: ACROBATIC_COUNTER
pos 503: COUNTER_CRITICAL
pos 570: DRAGON_CRITICAL
```

### 4.4 重要イベントパターン

単発イベントではなく以下の**連鎖パターン**をターゲットとして優先する。

| パターン名 | イベント連鎖 |
|-----------|-------------|
| Pattern A | specialCharge → ACROBATIC_STAR → counter → counter critical |
| Pattern B | attack critical → dragon critical → attack critical |
| Pattern C | ACROBATIC_STAR → counter → counter → attack critical |
| Pattern D | specialCharge → dragon slash → dragon slash（会心×2） |

---

## 5. Phase 1/2：DFS / IDDFS 探索

### 5.1 IDDFS 外枠

```cpp
for (int targetTurn = currentTurn + 1;
         targetTurn <= std::min(maxTurn, bestTurn - 1);
         targetTurn++) {
    if (dfs(currentState, targetTurn, /*depth=*/0)) {
        // targetTurn が最適
        break;
    }
}
```

### 5.2 探索ノード構造

`Genome` / `actions[350]` のノードごとコピーは**禁止**。DFSスタック上で行動列を管理する。

```cpp
// バックトラック管理
actions[depth] = action;
dfs(nextState, remaining - 1, depth + 1);
actions[depth] = NONE; // backtrack
```

### 5.3 DFS 終了条件

| 種別 | 条件 |
|------|------|
| 成功 | `enemy.hp <= 0 && ally.hp > 0` |
| 失敗① | `ally.hp <= 0` |
| 失敗② | `remainingTurns <= 0` |
| 失敗③ | 安全な楽観上界でも敵HPを削り切れない（枝刈り） |
| 失敗④ | 時間切れ / ノード上限到達 |

### 5.4 DFS 擬似コード

```cpp
bool dfs(State& s, int remaining, int depth) {
    if (s.enemy.hp <= 0 && s.ally.hp > 0) { record(); return true; }
    if (s.ally.hp <= 0 || remaining <= 0)  return false;
    if (cannotKillWithOptimisticBound(s, remaining)) return false;

    auto acts = collectLegalActions(s);
    sortByRNGEventPriority(acts, s.position, rngEventTable); // 順序付けのみ、枝刈りではない

    for (auto act : acts) {
        State next = step(s, act);
        if (next.ally.hp <= 0) continue;
        actions[depth] = act;
        if (dfs(next, remaining - 1, depth + 1)) return true;
    }
    return false;
}
```

---

## 6. 行動順序仕様（RNGイベント距離スコア）

> **重要原則：評価は「並び順」にのみ使う。評価が低くても枝は捨てない。**
ただしメモリ制限は2GBなので注意。これはwebassembly側の都合。
> EnhancedHeapQueue.cppは最適化こそされてるものの、重いことには変わらない。
Genome.hも巨大な状態をすべて持ってて重いので、破棄する。

### 6.1 スコア計算

```cpp
int actionScore(State& s, int action, const vector<RNGEvent>& events) {
    State next = step(s, action);
    int score = 0;

    // 1. 即撃破
    if (next.enemy.hp <= 0) score += 100000;

    // 2. このターン内でイベントを踏んだか
    for (auto& e : events) {
        if (e.position >= s.position && e.position < next.position) {
            score += eventWeight(e.type);
        }
    }

    // 3. 次の重要イベントへの距離（近いほど高スコア）
    int nextEvent = nearestEventPosition(next.position, events);
    score -= (nextEvent - next.position);

    // 4. 直接ダメージ（補助）
    score += estimateDamage(s, action) / 10;

    return score;
}
```

### 6.2 優先順位表

| 優先度 | 行動カテゴリ | スコア加算量（参考値） |
|--------|-------------|----------------------|
| 1 | 即撃破可能な行動 | +100000 |
| 2 | このターンにイベント（会心/カウンター等）を踏む | +10000〜50000 |
| 3 | アクロバットスター発動関連（specialCharge 獲得含む） | +8000 |
| 4 | 次の重要イベントへの距離が近い行動 | 距離×(−1) |
| 5 | 直接ダメージ量 | damage/10 |
| 6 | 回復・バフ準備 | +500 |
| 7 | 防御・逃げ相当（RNG調整目的） | +100 |

---

## 7. 安全な楽観上界による枝刈り

### 7.1 基本条件

枝刈りに使ってよいのは「**どれだけ都合よく見ても敵HPを削り切れない**」と証明できる条件のみ。
証明方法は、数学的にすべての会心を踏めた場合のダメージなどで考える。

```cpp
bool cannotKillWithOptimisticBound(const State& s, int remaining) {
    int upperBound = remaining * maxTheoreticalDamagePerTurn(s);
    return s.enemy.hp > upperBound;
}
```

### 7.2 上界の過大評価原則

**上界は必ず過大評価にする。過小評価は誤枝刈りになり最適保証が壊れる。**

| 要素 | 楽観的扱い方 |
|------|-------------|
| 会心 | 常に最大倍率で計算 |
| 乱数バラつき | 常に最大値で固定 |
| バフ | 常に最大バフ状態を仮定 |
| MP制約 | 残りターンで獲得しうる最大MPを加算 |
| アクロバットスター | 残りターン全てでカウンター会心が出ると仮定 |

```
実際の最大ダメージ 1000
上界             2000  ← 安全（甘くても保証は壊れない）
上界              800  ← 危険！誤枝刈りで最適保証が壊れる
```

### 7.3 段階的改良

最初は単純な「残りターン × ターン最大ダメージ」で十分。段階的に以下を追加する。

- MP制約・薬制約・specialCharge 残りターン
- バフ残りターン・コンボ最大倍率
- 行動不能（麻痺・睡眠）状態

---

## 8. Step API 仕様

### 8.1 目的

探索用ホットパスとして**1ターンだけ進める軽量API**を定義する。汎用シミュレーション関数とは別に用意する。
nowstateの内部ターンも進める。

### 8.2 インターフェース

```cpp
struct StepState {
    Player   players[2]; // [0]=味方  [1]=敵
    int      position;   // lcg position
    uint64_t nowState;   // カメラカウンタ等（bits[11:8] = counter）
    int      turn;
};

bool Step(
    StepState& state,  // in-place 更新
    int        action, // BattleEmulator:: 定数
    uint64_t   seed    // lcg::init に渡した seed
);
// 戻り値: true=味方生存, false=味方死亡または続行不能
```

### 8.3 Stepの責務

- 現在状態から action を1ターン適用する
- `players[2]` を更新する（HP・MP・バフ・状態異常）
- `position` を更新する（lcg 消費分を正確に加算）
- `nowState` を更新する（camera の counter 含む）
- `turn` をインクリメントする

### 8.4 Stepで避けること

- `actions[350]` の毎回コピー
- `Genome` の巨大コピー
- `priority_queue` / `unordered_set`
- double cost 計算（整数で十分な部分）
- optional result / nullable 分岐
- 複数ターン実行ループ

### 8.5 camera.cpp との統合

`camera::Main()` / `onFreeCameraMove()` が消費する `position` を Step 内で正確に再現すること。

特に以下に注意する。

- `ATTACK_ALLY` 連続時の `preemptive` フラグの挙動
- `NowState` の `bits[11:8]`（counter）の管理

```cpp
// camera.cpp の NowState 管理（参照）
auto counter = (nowState >> 8) & 0xf;
// ... 処理 ...
nowState &= ~0xf00;
nowState |= (counter << 8);
```

---

## 9. メモリ仕様

### 9.1 OPEN/CLOSED は持たない

A\* のような大量の OPEN/CLOSED リストは持たない。メモリ使用量は `O(depth)` の DFS スタックのみ。

### 9.2 visited は原則使わない

状態再訪が少ないため厳密な visited は不要。使う場合も以下を厳守する。

| 用途 | 可否 |
|------|------|
| visited で枝を切る | **基本禁止**（最適保証に影響する可能性あり） |
| visited を統計・順序補助に使う | 可 |
| 完全一致状態（players/position/nowState 全一致）のみ限定使用 | 可 |

### 9.3 lcg.cpp のメモリ運用

- `OPTIMIZE_MODE` マクロにより `thread_local` / 非 `thread_local` を切り替え可能
- 探索中に seed を切り替える場合は `lcg::init()` を呼び直すこと
- `ARRAY_SIZE=5000` を超える `position` アクセスは assert で検出される。探索深度が深い場合は `ARRAY_SIZE` を調整すること

---

## 10. 出力仕様

### 10.1 結果ステータス

| ステータス | 意味 |
|-----------|------|
| `OPTIMAL_PROVEN` | 11ターン以下不存在証明済み + 12ターン解あり → **12ターンが最適確定** |
| `BEST_EFFORT` | 12ターン解あり + 10ターン以下証明済み + 11ターン探索未完了 |
| `NO_SOLUTION_FOUND` | 解未発見 + 10ターン以下証明済み + 11ターン以上未確定 |

### 10.2 出力データ構造

```cpp
struct SearchResult {
    SearchStatus      status;               // OPTIMAL_PROVEN / BEST_EFFORT / NO_SOLUTION_FOUND
    int               bestTurn;             // 発見された最短撃破ターン（-1 = 未発見）
    int               provenNoSolutionBelow;// このターン以下は不存在証明済み
    int               incompleteTurn;       // 探索未完了ターン（-1 = なし）
    std::vector<int>  bestActions;          // 最良行動列
    uint64_t          nodesVisited;
    double            elapsedMs;
};
```

### 10.3 出力例

```
best found    : 12 turns
best actions  : [ATTACK, DRAGON_SLASH, DEFENCE, ATTACK, ACROBATIC_STAR, ...]
status        : BEST_EFFORT
proven no solution <= 10 turns
11 turns search: incomplete
nodes visited : 12,345,678
time          : 892 ms
```

---

## 11. 実装優先順位

### 優先度 A（最初に実装）

| ID | タスク |
|----|--------|
| A-1 | A\* 主探索の停止・無効化 |
| A-2 | IDDFS + DFS フレームの実装 |
| A-3 | `actions[350]` のノードコピー廃止・スタック管理への移行 |
| A-4 | 評価関数を枝刈りではなく行動順序にのみ使用 |
| A-5 | 安全な楽観ダメージ上界の実装（単純版） |

### 優先度 B（次に実装）

| ID | タスク |
|----|--------|
| B-1 | RNGイベントスキャン実装（`scanEvents`） |
| B-2 | イベント距離スコアによる行動順序付け |
| B-3 | Step API 実装（軽量1ターン進行） |
| B-4 | Genome / Player コピーの最小化 |
| B-5 | アクロバットスター専用探索モード |

### 優先度 C（最後でよい）

| ID | タスク |
|----|--------|
| C-1 | WASM worker 対応 |
| C-2 | SharedArrayBuffer 対応 |
| C-3 | Service Worker による COOP/COEP 回避 |
| C-4 | 高度な確率モデル（Cross entropy method 等） |
| C-5 | 大規模ビーム探索 |

---

## 12. 最適保証チェックリスト

| 条件 | 可否 | 理由 |
|------|------|------|
| 評価値が低いから枝を捨てる | **禁止** | 評価は順序付けのみに使う |
| イベントが遠いから枝を捨てる | **禁止** | イベント距離は順序付けのみ |
| 期待値・会心率が低いから捨てる | **禁止** | 上界枝刈りのみ許可 |
| 上界を実際の最大より小さく設定する | **禁止** | 誤枝刈りで最適保証が壊れる |
| 楽観上界でも倒せないから捨てる | **許可** | 安全な枝刈り |
| `ally.hp <= 0` で捨てる | **許可** | 安全な枝刈り |
| `remainingTurns <= 0` で捨てる | **許可** | 安全な枝刈り |

---

## 13. 関連ファイル対応表

| ファイル | 役割 | 探索エンジンとの関係 |
|---------|------|---------------------|
| `BattleEmulator.cpp` | 行動定数定義・1ターン戦闘ロジック | Step API の実装基盤 |
| `camera.cpp` | カメラ乱数消費（`onFreeCameraMove`） | `position` 更新の正確な再現が必要 |
| `Player.h` | Player 構造体・HP/MP/バフ等の状態 | `StepState` の `players[2]` の型 |
| `lcg.cpp` | 線形合同法乱数生成・`getPercent` / `getTop32` 等 | RNGイベントスキャンと探索中の乱数取得 |

### 注意事項

**camera.cpp の NowState 管理**（`bits[11:8]` = counter）は Step API 内で必ず再現すること。

**lcg.cpp の `init_mode`**（`init=true` でキャッシュ生成、`false` でジャンプ計算）を探索用途に合わせて選択すること。探索中に多数の `lcg::init()` 呼び出しが発生する場合、`init=false`（ジャンプ計算モード）の方が適している可能性がある。


はい、その理解でかなり正しいです。

今回の問題では、**生の乱数による近似解を無理にやる必要は薄い**です。  
というより、普通の意味での

```
ランダムに行動列を生成する
ランダムに少し変異する
評価関数で良さそうなものを残す
```


みたいな近似探索は、あまり本質を突いていません。

理由はあなたの言う通りで、この問題の勝敗を決めているのは主に、

```
会心を引けるか
アクロバットスターを早く発動できるか
アクロバットスター中にカウンターを引けるか
そのカウンターで会心を引けるか
敵行動と乱数消費が噛み合うか
```


だからです。

つまりこれは、

```
評価関数を賢くする問題
```


ではなく、

```
特定のRNGイベントを踏める行動列を探す問題
```


です。

なので、方針を少し修正した方がいいです。

---

# 修正版の結論

## 生の乱数近似探索は主戦力にしなくていい

やるとしても補助で十分です。

主戦力にすべきなのは、

```
RNGイベント駆動探索
```


です。

つまり、

```
どの行動が強そうか
```


ではなく、

```
この行動を選ぶと、次にどのRNG indexへ進むか
その index で会心を踏めるか
アクロバットスター発動判定を踏めるか
カウンター判定を踏めるか
```


を見る。

探索の中心を、

```
状態評価
```


から

```
乱数位置合わせ
```


へ移すべきです。

---

# なぜ評価関数最適化では限界があるか

普通の探索評価はこうです。

```
敵HPが低いほど良い
味方HPが高いほど良い
MPが多いほど良い
火力行動を優先
会心が近そうなら加点
```


しかし今回の最速撃破は、おそらくこういう形になります。

```
一見弱い行動で乱数をずらす
↓
アクロバットスターを早く発動する
↓
敵攻撃を回避/カウンターする
↓
カウンターで会心を引く
↓
想定外の大ダメージで短縮
```


この場合、途中の行動は評価関数上は弱く見えます。

例えば、

```
防御
回復
低ダメージ行動
逃げ相当の行動
```


でも、乱数消費をずらす目的なら価値があります。

だから評価関数をいくら調整しても、

```
未来の特定RNGイベントに到達するための低評価行動
```


を拾いにくいです。

---

# 問題の本体

この問題はたぶん、

```
戦闘最適化
```


というより、

```
RNG制約付き経路探索
```


です。

状態はこうです。

```
battle state
+ rng position
+ nowState
```


そして欲しいのは、

```
短いターン数で、良いRNGイベント列を踏む経路
```


です。

つまり重要なのは、

```
各行動の期待値
```


ではなく、

```
各行動がRNG positionを何個進めるか
各イベント判定がどの乱数を使うか
その乱数が当たりかどうか
```


です。

---

# 仕様修正：近似探索ではなく RNGイベント駆動探索

以下の仕様に修正するのがよいです。

---

## Phase 1：RNGイベント表を作る

まず、seed から将来の乱数列を見て、重要イベントの当たり位置を列挙します。

対象イベントは例えばこれです。

```
味方通常攻撃の会心
ドラゴン斬りの会心
ヒャド/バギ系の暴走相当
specialCharge 1%
アクロバットスター中の回避
アクロバットスター中のカウンター
カウンター会心
敵行動の種類
眠り/毒/デバフなどの敵行動結果
```


このような表を作る。

```
rngIndex 123: attack critical possible
rngIndex 140: special charge possible
rngIndex 155: acrobatic counter possible
rngIndex 156: counter critical possible
rngIndex 170: dragon critical possible
```


---

## Phase 2：行動ごとの乱数消費遷移を測る

各状態・各行動について、

```
beforePosition
afterPosition
delta = afterPosition - beforePosition
```


を測る。

さらに、そのターン中に何のイベント判定を踏んだかを記録する。

```
action = ATTACK
before = 310
after = 335
events:
  allyCriticalCheck at 319
  specialChargeCheck at 330
```


```
action = DEFENCE
before = 310
after = 331
events:
  enemyAction at 312
  enemyHit at 318
  allyDummyAttack at 323
```


これで初めて、

```
この行動は強い/弱い
```


ではなく、

```
この行動は次の会心位置へ合わせやすい
```


が分かります。

---

## Phase 3：イベント到達探索をする

探索の優先順位を以下にする。

```
1. 即撃破
2. カウンター会心に到達できる
3. アクロバットスター発動に到達できる
4. 味方会心に到達できる
5. specialCharge を引ける
6. 敵行動が都合よい
7. 通常ダメージが高い
```


ここで重要なのは、評価関数ではなく、

```
イベント到達性
```


を見ることです。

---

# 生乱数ランダム探索との違い

生乱数ランダム探索はこうです。

```
行動列をランダム生成
↓
たまたま良い乱数イベントを踏むのを待つ
```


これは、会心やアクロバットスターが低確率だとかなり非効率です。

一方で RNGイベント駆動探索はこうです。

```
先に良い乱数位置を知る
↓
そこに到達する行動列を探す
```


こちらの方が本質的です。

今回やるべきはこれです。

```
乱数を振って探す
```


ではなく、

```
既に決まっている乱数列に行動列を合わせる
```


です。

---

# A\*を捨てる理由もここで変わる

A\*が弱い理由は、単に重いからだけではないです。

もっと本質的には、

```
A*の評価関数が「状態の良さ」を見ている
```


からです。

でも今回必要なのは、

```
未来のRNGイベントへ到達できるか
```


です。

だから A\* の `hCost` をいくら調整しても、根本的にはズレます。

もしA\*を使うなら、`hCost` はこうでないといけません。

```
敵HPの近さ
味方HPの安全性
MP効率
```


ではなく、

```
目標RNGイベント列までの到達しやすさ
```


です。

でもこれを正しく作るのはかなり難しい。

だったら A\* ではなく、

```
DFS + イベント到達順序付け
```


の方が軽くて扱いやすいです。

---

# 具体的な探索方針

## 1. まずイベントスキャン

現在 `position` から、例えば先 500〜1500 個くらいの乱数を走査します。

見るべきものは、判定ごとの閾値です。

例：

```
Attack critical:
  getPercent(pos, 0x2710) < kaisinnP

Dragon critical:
  getPercent(pos, 0x2710) < DragonSlashKaisinnP

Spell critical:
  getPercent(pos, 0x2710) < 100

Special charge:
  getPercent(pos, 100) < 1

Acrobatic counter:
  getPercent(pos, 100) in [50, 74]

Acrobatic avoid:
  getPercent(pos, 100) in [0, 49]
```


これで、

```
pos 421: attack critical
pos 438: special charge
pos 502: acrobatic counter
pos 503: counter critical
```


のような表を作る。

---

## 2. 良いイベント列をターゲット化する

最短撃破に効くのは単発イベントではなく、たぶん連鎖です。

例えば、

```
specialCharge
↓
Acrobatic Star
↓
enemy attack
↓
counter
↓
counter critical
```


このようなパターンをターゲットにする。

ターゲットパターン例：

```
Pattern A:
  specialCharge within 1-3 turns
  acrobaticStar next
  counter within 1-2 turns
  counter critical

Pattern B:
  attack critical
  dragon critical
  attack critical

Pattern C:
  acrobaticStar
  counter
  counter
  attack critical
```


---

## 3. DFSの行動順をイベント距離で並べる

各候補行動を一手進めて、

```
afterPosition
```


を見る。

そして、次の重要イベントまでの距離を計算する。

```
distance = targetEventPosition - afterPosition
```


距離が小さいもの、またはターン内でイベントを踏んだものを先に試す。

```
score =
  immediateKillBonus
  + eventHitBonus
  - distanceToCritical
  - distanceToSpecialCharge
  - distanceToCounterCritical
```


ただし繰り返しですが、これは順序付けだけです。

捨てない。

---

## 4. アクロバットスター専用探索を作る

通常探索と別に、

```
アクロバットスター早期発動ルート探索
```


を持った方がいいです。

目的はこれ。

```
specialCharge を引く
↓
ACROBATIC_STAR を使う
↓
敵攻撃で counter を引く
↓
COUNTER の会心を引く
```


これ専用の探索順序にする。

通常のダメージ評価だと、アクロバットスター準備は弱く見えやすいので、専用モードが必要です。

---

# 仕様書修正版

## 方針名

```
RNGイベント駆動 IDDFS
```


---

## 目的

評価関数最適化ではなく、固定された乱数列上の重要イベントを踏む行動列を探索する。

---

## 探索の主目的

```
最短撃破
```


ただし探索の中間目標として以下を置く。

```
会心を踏む
specialChargeを踏む
アクロバットスターを発動する
カウンターを踏む
カウンター会心を踏む
```


---

## 探索構成

```
1. RNGイベントスキャン
2. イベントターゲット列生成
3. DFS/IDDFS
4. 行動順序をイベント距離で並べる
5. 安全な上界でのみ枝刈り
6. 証明できた範囲を返す
```


---

## 保証

最適保証を残す場合、

```
イベントが遠いから捨てる
```


は禁止。

イベント情報は、

```
探索順序
```


にのみ使う。

枝刈りに使ってよいのは、

```
理論最大ダメージでも倒せない
味方が死亡済み
残りターンがない
```


のような安全条件だけ。

---

# 生乱数近似探索を外してよい理由

今回の最終仕様では、以下は必須ではないです。

```
ランダム行動列生成
ランダム変異
焼きなまし
遺伝的アルゴリズム
Cross entropy
```


理由は単純です。

```
RNG列は既に seed で固定されている
```


ので、乱数で行動列を振るより、

```
固定RNG列の当たり位置に合わせる
```


方が直接的です。

---

# 最終的な修正結論

前回の、

```
近似探索で強い上界を作る
```


は一般論としては正しいです。

ただし今回の問題では、より正確にはこうです。

```
生ランダム近似探索で上界を作る
```


ではなく、

```
RNGイベント駆動探索で、会心・アクロバットスター・カウンター会心を踏む候補を先に作る
```


です。

つまり修正版はこれです。

```
A*を主探索から外す
生乱数ランダム近似も主戦力にしない
RNGイベント表を作る
重要イベントに到達する行動列をDFSで優先的に試す
枝刈りは安全な上界だけ
証明探索はIDDFS/B&Bで行う
```


---

# 端的に言うと

あなたの言う通りです。

```
早く倒せるかどうかは、
評価関数の良し悪しより、
良いRNGイベントを踏めるかでほぼ決まる。
```


なので、次にやるべきは、

```
評価関数最適化
```


ではなく、

```
RNGイベント位置合わせ探索
```


です。

会心、アクロバットスター、カウンター会心を **先に乱数列から検出して**、  
そこへ到達できる行動列を優先的に掘る。

これが一番筋がいいです。