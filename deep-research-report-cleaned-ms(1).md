# BattleEmulator を本当に行列にすると何を掛ければよいか
## 結論
この問題で使うべき「行列」は一種類ではなく、**二種類を明確に分離する**のが正しいです。<br>
一つ目はLCGそのものを進める、$\mathbb Z/2^{64}\mathbb Z$ 上の小さな $2\times2$ 行列です。<br>
$$
\boxed{
L=
\begin{pmatrix}
\texttt{0x5d588b656c078965} & \texttt{0x0000000000269ec3}\\
0&1
\end{pmatrix}
\pmod{2^{64}}
}
$$
二つ目が本命で、BattleEmulator の全合法行動を表す **Boolean疎行列**です。<br>
$$
\boxed{
A_{ji}=1
\iff
\exists a\in U(S_i)\;:\;T(S_i,a)=S_j
}
$$
そして「現在の最適解より1ターン短い $H$ ターン以内に撃破可能か」を表す到達可能ベクトルを $r_H$ とすると、
$$
\boxed{
r_{t+1}=A\odot r_t
}
$$
をBoolean semiring、すなわち「掛け算=AND、足し算=OR」で反復します。<br>敵死亡状態を表すベクトルを $g$ とすれば、
$$
\boxed{
g^\top\odot
\left(I\lor A\right)^H r_0=0
}
$$
がそのまま
$$
\boxed{D(H)=\mathrm{false}}
$$
の証明です。<br>ここにはヒューリスティクスも確率近似もハッシュ衝突もありません。<br>
しかも実装時に巨大な $A$ を作る必要はありません。<br>BattleEmulator の `StepSearchState` 自体がすでに「$A$ の非零要素をその場で生成する関数」になっています。<br>`StepSearchState` は source を destination にコピーし、指定された `forcedHeroAction` について `Main(..., RunCount=1, mode=-2)` を一度実行するため、まさに決定的な $T(S,a)$ です。<br>探索対象の合法コマンドも `ATTACK_ALLY`, `DRAGON_SLASH`, `DEFENCE`, `FLEE_ALLY`, `MEDICINAL_HERBS`, `HEAL`, `CRACK_ALLY`, `ACROBATIC_STAR` の8種類としてコードに明示されています。<br>
したがって狙うべきものは「巨大なdense matrixを作ってGPUの普通のmatmulへ入れること」ではありません。<br>
$$
\boxed{
\text{BattleEmulatorをexactな疎Boolean線形作用素として評価する}
}
$$
これが数学的に正しい行列化です。<br>
## 行列の基底になる状態
まず、状態を普通のHPベクトル
$$
(h_{\rm hero},h_{\rm enemy},p,\ldots)
$$
として線形変換しようとしてはいけません。<br>BattleEmulator には比較、条件分岐、HPのclamp、状態異常、会心、回避、怒り、乱数消費数の変化があるので、この数値ベクトルに対する固定の小さな
$$
x'=Mx
$$
では表現できません。<br>実際、`Main` は先攻判定だけでも二つの `floatRand` を読み、さらに1位置進め、敵行動選択や状態異常によって後続の乱数消費数も変えています。<br>
代わりに、**状態そのものを基底ベクトルにします**。<br>
固定seedの探索について、まず素朴な完全状態を
$$
S=(P_0,P_1,p,n)
$$
とします。<br>ここで $P_0,P_1$ は二つの `Player`、$p$ は `position`、$n$ は `NowState` です。<br>実際の `SearchState` を初期化するコードも `players[0]`, `players[1]`, `position`, `nowState` を保持し、その全体を `StepSearchState` がコピーして次状態へ更新しています。<br>
`Player` にはHP、MP、攻撃力、防御力、素早さ、必殺チャージ、麻痺、怒り、アクロバットスター、薬草数、inactiveなどが存在します。<br>
ただし、**この構造体をそのまま状態とする必要もありません**。<br>今回の目的は `D(H)=false` なので、未来の撃破可能性に影響しない情報はexactに商状態へ落とせます。<br>現在の8コマンド限定コードについて、ターン境界で実際に変化し、かつ未来へ影響する主要成分は概念的には
$$
q=
\bigl(
h_0,h_1,
mp_0,
sc_0,sct_0,
par_0,parL_0,parT_0,
acro_0,acroT_0,
herb_0,
inactive_0,
sc_1,
rage_1,rageT_1,
c,
p
\bigr)
$$
のようになります。<br>ここで $c$ は free-camera counter です。<br>これは「この17フィールドで絶対に完成」と今ここで決め打ちするという意味ではなく、**future-dependency analysis によって不要フィールドを除いた商状態の形**です。<br>現在のコードでは `NowState` からカメラが実際に読むのは bits 8–11 のcounterで、その値によって後続の乱数消費数が変わります。<br>したがってこれは残さなければなりません。<br>
一方、`Main` が bits 12–31 に保持する turn count は `StepSearchState` の `forcedHeroAction` / `mode=-2` によるこの到達可能性判定では、ターン番号そのものによって戦闘規則を変化させていません。<br>毎ターン冒頭の `specialChargeTurn--` などはターン番号ではなく状態そのものに依存します。<br>したがって「撃破可能性だけ」を観測する商では、turn count を状態IDから除去できる余地があります。<br>これは近似ではなく、同じ残りターン数で未来の遷移が一致することを確認した上で行う exact quotient です。<br>
同様に `players[0].defence` はターン冒頭で必ず `1.0` へ戻され、`DEFENCE` のターンだけ `0.5` へ変化します。<br>よってフルターン終了後の次ターン開始状態を基準に商を作るなら、前ターン末尾の `defence` 値そのものは独立状態として保持する必要がありません。<br>
数学的には、残り $h$ ターンの探索だけを考えるなら
$$
S\sim_h S'
\iff
\forall w,\ |w|\le h:
\operatorname{Outcome}(S,w)
=
\operatorname{Outcome}(S',w)
$$
という同値関係を使えます。<br>必要なのは最小商を実際に完成させることではなく、**使う圧縮がこの条件を破らないことだけ**です。<br>ここを最小化問題にしてしまう必要はありません。<br>
各異なるexact状態へ
$$
\rho(S_i)=i,\qquad 0\le i<N
$$
という番号を付けます。<br>すると状態 $S_i$ は普通の数値ベクトルではなく、
$$
e_i=
(0,\ldots,0,\underbrace{1}_{i},0,\ldots,0)^\top
$$
というone-hot基底になります。<br>
ここで重要なのは、**64-bit hashを状態そのものだと思ってはいけない**という点です。<br>proof用途で衝突可能な64-bit hashを同一性判定に使えば、異なる二状態を誤って潰した瞬間に `false` 証明ではなくなります。<br>64-bit整数を使うなら、
$$
\boxed{\text{exact比較で一意化した状態に付与する64-bit ordinal ID}}
$$
あるいは範囲を数学的に証明したinjective packingでなければなりません。<br>
## 行動ごとの実際の遷移行列
8種類のhero commandそれぞれについて一枚ずつ行列を作る、と考えると最も分かりやすいです。<br>
行動 $a$ について
$$
T_a(S)=T(S,a)
$$
と定義します。<br>
すると
$$
\boxed{
(A_a)_{ji}
=
\begin{cases}
1,&a\in U(S_i)\land T_a(S_i)=S_j,\\
0,&\text{otherwise}.
\end{cases}
}
$$
です。<br>
さらに標準基底を使えば、行列そのものをもっと直接
$$
\boxed{
A_a=
\bigvee_{\substack{i\\a\in U(S_i)}}
e_{\rho(T_a(S_i))}e_i^\top
}
$$
と書けます。<br>
これが「実際にどういう行列か」へのかなり文字通りの答えです。<br>
例えばある状態 $S_{137}$ において
$$
U(S_{137})
=
\{\mathrm{ATTACK},\mathrm{DRAGON},\mathrm{DEFENCE},
\mathrm{HEAL}\}
$$
だったとします。<br>そしてexact emulatorを1ターン実行した結果
$$
\begin{aligned}
T_{\rm attack}(S_{137})&=S_{921},\\
T_{\rm dragon}(S_{137})&=S_{522},\\
T_{\rm defence}(S_{137})&=S_{301},\\
T_{\rm heal}(S_{137})&=S_{812}.
\end{aligned}
$$
なら、全行動をまとめた行列 $A$ の **137列目** は
$$
A_{\ast,137}
=
e_{921}\lor e_{522}\lor e_{301}\lor e_{812}.
$$
つまり列の見た目は
$$
\begin{pmatrix}
0\\
\vdots\\
1\quad(\text{row }301)\\
\vdots\\
1\quad(\text{row }522)\\
\vdots\\
1\quad(\text{row }812)\\
\vdots\\
1\quad(\text{row }921)\\
\vdots\\
0
\end{pmatrix}.
$$
これだけです。<br>
別の状態で薬草が0なら `MEDICINAL_HERBS` のedgeは存在しません。<br>MPが2未満なら `HEAL` はなく、MPが3未満なら `CRACK_ALLY` はなく、`ACROBATIC_STAR` は `specialCharge && specialChargeTurn != 0 && !acrobaticStar` のときだけedgeが存在します。<br>この合法性判定は既に `IsHeroCommandSelectable` にexactに書かれています。<br>
8枚をORすれば
$$
\boxed{
A=
A_{\rm ATTACK}
\lor
A_{\rm DRAGON}
\lor
A_{\rm DEFENCE}
\lor
A_{\rm FLEE}
\lor
A_{\rm HERB}
\lor
A_{\rm HEAL}
\lor
A_{\rm CRACK}
\lor
A_{\rm ACRO}
}
$$
です。<br>
決定的な $A_a$ は合法な各source状態について行き先が一つしかないので、**各列に高々1個しか1がありません**。<br>全行動をORした $A$ でも、各列の1の数は高々8です。<br>
したがって $N\times N$ のdense matrixを保存するのは数学の表記を誤って実装へ直訳しただけです。<br>実体は
$$
\boxed{
\mathrm{succ}[i,a]=\rho(T_a(S_i))
}
$$
という疎行列です。<br>
行列積も
$$
r' = A\odot r
$$
と書きますが、その成分は
$$
r'_j
=
\bigvee_i(A_{ji}\land r_i)
$$
なので、実処理は
$$
r_i=1
\quad\Longrightarrow\quad
r'_{\rho(T_a(S_i))}\gets1
$$
を全合法 $a$ について行うだけです。<br>
つまりGPU上で100万状態を同時に展開することと、このBoolean疎行列-vector積は**数学的に完全に同じ演算**です。<br>
## LCG部分の具体的な二行二列行列
`lcg.cpp` の生成式は
$$
x_{n+1}=ax_n+c\pmod{2^{64}}
$$
で
$$
a=\texttt{0x5d588b656c078965},
\qquad
c=\texttt{0x269ec3}.
$$
unsigned 64-bit積和なので、コード上のwraparoundがそのまま modulo $2^{64}$ です。<br>`lcg_advance` も affine map を二乗してjump-aheadしています。<br>
同次座標
$$
\tilde x_n=
\begin{pmatrix}x_n\\1\end{pmatrix}
$$
を使えば
$$
\boxed{
\tilde x_{n+1}
=
\underbrace{
\begin{pmatrix}
a&c\\
0&1
\end{pmatrix}}_L
\tilde x_n
\pmod{2^{64}}
}
$$
です。<br>
したがって $m$ 個進めると
$$
\boxed{
\tilde x_{n+m}=L^m\tilde x_n
}
$$
で、
$$
L^m=
\begin{pmatrix}
A_m&C_m\\
0&1
\end{pmatrix}
$$
と書けば
$$
\boxed{
x_{n+m}=A_mx_n+C_m\pmod{2^{64}}.
}
$$
`lcg.cpp` の `lcg_advance` が計算しているのがまさにこの $(A_m,C_m)$ です。<br>
固定初期seedを $x_0$ とした場合、現在のコードでは `position=p` の乱数は
$$
x_p=L^p x_0
$$
から得られる上位32bit
$$
u_p=\operatorname{top32}(x_p)
$$
です。<br>`getPercent` は正確に
$$
\boxed{
G_m(u_p)
=
\left\lfloor
\frac{m\,u_p}{2^{32}}
\right\rfloor
}
$$
を返しています。<br>
したがって例えば敵行動選択は
$$
G_{256}(u_p)+1
$$
を
$$
[1,43],\quad
[44,85],\quad
[86,128],\quad
[129,171],\quad
[172,213],\quad
[214,256]
$$
に分け、それぞれ `VICTIMISER`, `HP_HOOVER`, `CRACK_ENEMY`, `ATTACK_ENEMY`, `MANAZASHI`, `PUFF_PUFF` に写す有限写像です。<br>これは `ProcessEnemyRandomAction2B` のコードそのものです。<br>
つまりここでは32bit乱数値を状態として残す必要すらなく、その処理が終了した後に未来へ残るのは選択されたactionと、その結果更新されたbattle state、そして次のLCG位相だけです。<br>
さらに、このコードにはexactに消せる死んだ分岐もあります。<br>`mitoreP` は `-0.0330` なのに、
```cpp
if (lcg::getPercent(position, 100) < mitoreP)
```
となっています。<br>`getPercent(...,100)` は整数 $0,\ldots,99$ しか返さないため、この条件は数学的に常にfalseです。<br>したがってその内側の二度目の `getPercent` は現在のコードでは一度も消費されません。<br>これはheuristic pruningではなく恒等的なbranch eliminationです。<br>
問題は、一ターンの乱数消費数 $M$ が固定ではないことです。<br>そこで一ターン全体は
$$
\boxed{
(q,x)
\xrightarrow{a}
\left(
F_a(q,x),
L^{M_a(q,x)}
\tilde x
\right)
}
$$
と書きます。<br>
$M_a$ が $a$ だけで決まらないのは、例えば敵が `MANAZASHI` を選んだか、rage中か、heroがparalysisか、回避したか、acrobatic starが発動したか、camera counterが何だったか等で `position` の進み方が変わるからです。<br>BattleEmulator と camera の双方にその条件付き消費が明示されています。<br>
ただしこれは行列化を壊しません。<br>**$M$ が状態依存であることまで含めて $T_a$ を一つの有限写像として扱えばよい**からです。<br>
## LCGと戦闘を一枚の行列へ結合する形
戦闘状態を $q\in Q$、LCG位相を $p\in P$ として完全状態を
$$
s=(q,p)
$$
とします。<br>
固定seedなら $p$ だけで未来乱数列が決まるため、全体状態空間は
$$
\Omega=Q\times P
$$
です。<br>現在の `lcg.cpp` は `position` から固定seedのテープを参照する構造なので、この表現と直接一致します。<br>
完全状態に番号
$$
\eta:Q\times P\rightarrow\{0,\ldots,N-1\}
$$
を付ければ、一行動の行列は
$$
\boxed{
(\mathcal A_a)_{\eta(q',p'),\eta(q,p)}
=
1
}
$$
ただし
$$
(q',p')=T_a(q,p)
$$
のときだけです。<br>
つまり
$$
\boxed{
\mathcal A_a
=
\bigvee_{(q,p):a\in U(q)}
e_{\eta(T_a(q,p))}
e_{\eta(q,p)}^\top
}
$$
です。<br>
LCG state $x$ を直接持たせる形なら
$$
p'=p+M_a(q,p)
$$
の代わりに
$$
\begin{pmatrix}x'\\1\end{pmatrix}
=
L^{M_a(q,x)}
\begin{pmatrix}x\\1\end{pmatrix}
$$
とします。<br>
さらに「どうしてもblock matrixの形を見たい」なら、乱数の分岐結果をexactなsignature $\sigma$ に分けることができます。<br>例えばsignatureは
$$
\sigma=
(\text{initiative},
\text{enemy action},
\text{crit},
\text{evade},
\text{shield},
\text{damage},
\text{paralysis result},
\text{camera result},\ldots)
$$
です。<br>
各signatureが消費する乱数数を $m_\sigma$ として、そのsignatureになるsourceだけを選ぶ対角行列を
$$
D_{a,\sigma}
=
\operatorname{diag}
\left(
[\operatorname{sig}(S_i,a)=\sigma]
\right)
$$
とすれば
$$
\boxed{
A_a
=
\bigvee_\sigma
B_{a,\sigma}D_{a,\sigma}
}
$$
と分解できます。<br>
LCG位相を明示的な直積基底にした場合は概念的に
$$
\boxed{
\mathcal A_a
=
\bigvee_\sigma
\left(B_{a,\sigma}\otimes J_{m_\sigma}\right)
D_{a,\sigma}
}
$$
です。<br>
ここで $J_m$ は
$$
(J_m)_{p',p}=1
\iff
p'=p+m
$$
という位相shift行列です。<br>
これは「Battle行列」と「LCG行列」を数学的に分離して書いた形です。<br>ただし実装ではsignatureごとの巨大blockも $J_m$ も作る意味はありません。<br>**この式が表しているedgeをBattleEmulator相当のexact kernelが直接生成すればよい**からです。<br>
## 最適解より一つ短いことを否定する証明
現在の最適解を $d^\*$ とし、
$$
H=d^\*-1
$$
とします。<br>
命題
$$
D(H)=
\text{「初期状態から合法行動だけでHターン以内にenemy.hp=0へ到達できる」}
$$
を考えます。<br>
`Player::reduceHp` はHPを0未満へ行かないようclampし、`isPlayerAlive` は `hp != 0` なので、goal集合は正確に
$$
G=\{S:\operatorname{enemy.hp}(S)=0\}
$$
とできます。<br>
そのindicator vectorを
$$
g_i=
\begin{cases}
1&S_i\in G\\
0&\text{otherwise}
\end{cases}
$$
とします。<br>
初期状態
$$
r_0=e_{\rho(S_0)}
$$
から、一ターン後は
$$
r_1=A\odot r_0,
$$
二ターン後は
$$
r_2=A\odot r_1=A^2\odot r_0,
$$
したがってexactに$t$ターン後は
$$
\boxed{
r_t=A^t\odot r_0.
}
$$
「ちょうど$t$」ではなく「$H$以内」なら
$$
R_H
=
r_0\lor r_1\lor\cdots\lor r_H.
$$
Boolean semiringではこれは
$$
\boxed{
R_H=
(I\lor A)^H\odot r_0
}
$$
と一行で書けます。<br>
したがって
$$
\boxed{
D(H)
=
g^\top\odot R_H
}
$$
です。<br>
具体的には
$$
g^\top\odot R_H
=
\bigvee_{i=0}^{N-1}(g_i\land R_{H,i}).
$$
ここで値が0なら、goalに対応する成分はただの一つも立っていません。<br>
よって
$$
\boxed{
g^\top\odot(I\lor A)^Hr_0=0
\Longrightarrow
D(H)=\mathrm{false}.
}
$$
これは「たぶんない」ではありません。<br>
$$
\boxed{
\text{全合法経路がBoolean行列の全非零項として含まれているため、0なら存在しない}
}
$$
という有限到達可能性の証明です。<br>
さらに今回のコードには、最終層だけかなり重要なexact short-cutがあります。<br>`StepSearchState` は `stopBeforePresentationTail` を受け取り、`Main` はこれがtrueならaction後のHP更新まで終えたところでcamera処理前にreturnします。<br>ソース内コメントにも、最終horizonの $E(t)$ 判定にはpost-action HPだけが必要で、cameraはPlayerへアクセスせずRNG/presentation stateしか進めないため、最終ターンの撃破判定ではtailを省略できると明記されています。<br>
実際の `camera::Main` が受け取るのは `position`, `actions`, `NowState`, initiative等で、Playerへの参照は存在しません。<br>
したがって $H-1$ 層までは完全な
$$
A
$$
で進め、
$$
r_{H-1}=A^{H-1}r_0
$$
を得た後、最後だけ「presentation tail前まで」の作用素を $E$ とすれば、
$$
\boxed{
D(H)
=
g^\top\odot E\odot r_{H-1}
}
$$
で十分です。<br>
つまり最後の層では **次状態を完全に構築・dedupする必要すらなく、enemy.hp==0が一本でも出たかだけを調べればよい**。<br>逆に一本も出なければ、
$$
\boxed{D(H)=false}
$$
がその時点で確定します。<br>
これは5～10秒を狙う上で、quotientの最小化などより先に使う価値のある、コード自身が保証しているexact reductionです。<br>
## WebGPUで「行列演算」として実行する場合
ここで一番重要なのは、上の
$$
r_{t+1}=Ar_t
$$
を **WebGPUの通常の浮動小数点dense `matmul` と解釈しないこと**です。<br>
$A$ は各source列に高々8本しかedgeがない猛烈な疎行列なので、実際のSpMVは
$$
\boxed{
\text{reachable source}
\rightarrow
\text{最大8個のexact successor}
\rightarrow
\text{next reachable集合}
}
$$
というscatter演算です。<br>
WebGPUはcompute pass、storage buffer、compute dispatchを提供しているため、このfrontier型の並列計算そのものをGPUへ載せられます。<br>
bit-vectorでglobal state IDが直接引ける場合は、32状態を一つの`u32`にまとめ、
$$
word=\left\lfloor j/32\right\rfloor,\qquad
bit=1\ll(j\bmod32)
$$
として
$$
\mathrm{atomicOr}(next[word],bit)
$$
相当を行えば、これはまさにBoolean行列積のOR reductionです。<br>WGSLには `atomic<u32>` が存在します。<br>
ただし2026年9月時点のWGSL仕様では**具体的な64-bit整数型は存在しません**。<br>仕様自身が「WGSL does not have a concrete 64-bit integer type」としています。<br>
したがって `uint64_t` LCGをWebGPU shaderへexactに持ち込むなら
$$
x=x_{\rm lo}+2^{32}x_{\rm hi}
$$
として
$$
(x_{\rm lo},x_{\rm hi})
$$
の二つの`u32`で表し、mod $2^{64}$ の積和をlimb arithmeticで実装する必要があります。<br>CPU上の64-bit結果とbit-for-bit一致させれば、これは近似ではありません。<br>WGSLで「64-bit ID」を使いたい場合も同様に`vec2<u32>`等の二word表現になります。<br>
さらにもっと重要な罠があります。<br>現在のBattleEmulatorは `lcg::floatRand` がC++ `double` を返し、その値を先攻判定、ダメージ、会心補正等で使っています。<br>
WGSLのconcrete floating typesは現在 `f32` と、拡張を有効にした場合の `f16` であり、C++のbinary64 `double` をそのまま使うことはできません。<br>またWGSLの浮動小数点規則には実装上許容される差異もあります。<br>
したがってproof kernelで
```text
C++ double → WGSL f32
```
と移植するのは不可です。<br>そこで必要なのは、行列を浮動小数点化することではなく、各 `top32` に対してBattleEmulatorが最終的に観測する
$$
\text{branch result / integer damage / initiative}
$$
を**C++ binary64と同値な整数区間判定へ変換すること**です。<br>`getPercent` は既に完全な整数式ですし、`floatRand` 系も入力が32-bit有限集合なので、最終的な比較・`static_cast<int>`結果ごとにexactな区間へ分割できます。<br>
最終形はしたがって、
$$
\boxed{
\begin{array}{c}
\text{LCG: }L^m\text{ をexact 64-bit整数演算}\\[2mm]
+\quad
\text{Battle: }T_a(S)\text{ をexact finite transition}\\[2mm]
+\quad
\text{Search: }r_{t+1}=A\odot r_t\text{ をBoolean sparse SpMV}\\[2mm]
+\quad
g^\top r_H=0
\end{array}}
$$
です。<br>
`EnhancedHashCalculator.cpp` も `ActionOptimizer.cpp` もこの数学には一切必要ありません。<br>状態集合の削減をする場合も、必要なのは「hash値が同じ」ではなく **exact state equality または証明済みのfuture-equivalence** だけです。<br>
そして5～10秒という本来の目的に対して最も重要なのは、「行列 $A$ を作る」ことではありません。<br>
$$
\boxed{
A\text{ を保存せず、各active columnの最大8個の非零要素だけをGPU上で生成する}
}
$$
ことです。<br>
数学上は行列、実体はexact successor operatorです。<br>この形なら「全枝を調べた」という証明能力を一切失わず、巨大dense matrixのメモリ確保も、64-bit hash tableを探索の中心に据える必要もありません。<br>