# Slime.dst / suraimu2.dst の row+4 — 実測・初差・残課題

2026-09-29。今回の managed ROM lane は `slime-rom-0929-b` (Slime.dst) と
`slime-rom-0929-c` (suraimu2.dst)。乱数の正は常に
`../../battle_harness/Ctable_jp.js` の consumed-checkpoint である。

## 結論と、まだ結論にしてはいけないこと

`021E1958` の作業行 `row+4` は初期化されない物理スタックの残りであり、
モンスター種・行動ID・presentation type の固有フラグではない。
この戦闘では、その領域を直前に書いた命令を実際に捕捉すると
`FUN_020b7224` 内の描画処理だった。旧4人戦闘のHUD文字描画
`0204F2F8` が残す `1111` を5人へ拡大した `11111` は正しくない。

描画側の呼び出しフレームと入力行列まで変動するため、今回得たビット列を
行動別・シード別テーブルにしても一般解にはならない。直前writerの確定と、
その入力をC++だけで再構成できることは別である。後者は未完了。

## どのメモリを読んでいるか

`021E1A10` で `r10=0x027e333c`、行stride12、raw ID順は
`[0,1,192,193,194]`。実際の5個の `row+4` は:

`027e3340, 027e334c, 027e3358, 027e3364, 027e3370`

同じフックには `r10=0x027e3338` の別の一時表も来る。この4byte違いを
混ぜると、packed ID等をrow+4と読み、全非ゼロに見える。
`021E08BC` が使う表・その呼び出し時点と対応させて比較すること。

初回の本物の表は `[027e3488,2,0,0,027e35b4]`、非ゼロマスク `11001`。
これは両DST、複数シードのターン入口で観測した。後続は一定ではない。

## 何が外部挙動を変えるか

`FUN_overlay_d_25__021e08bc` は、非actor/non-targetの配置処理で
row+4のゼロ/非ゼロを参照する。非ゼロ側では
`FUN_overlay_d_25__021e2664` の退避経路を呼ぶ。
ゼロ側は前のauxiliary node等を利用できる。
したがって「未初期化だから読まれない/無視してよい」ではない。
余計な退避は次の移動経路、freecam param5、次ターン乱数列を変える。

## 固定22bitシードでの最初の比較

`0x18e0e1`, 未変更Slime.dst, `defend`。戦闘計算は#130まで一致。
最初のCruelcumber行動後の配置 `[58,61,47,41,34]` も一致。
次のSlime A行動前にROM row4は `11100`、C++は `11111`。
ROMはCruelcumberを `41->50`、Slime Bを34に保持する一方、
C++はCruelcumberを `41->31`、Slime Bを `34->33` に退避させる。
最後のguest行動もROM route0、C++ route4になっていた。
それでもこのseedのターン末は両方#138なので、最終消費だけの一致は不十分。

同seedの実際のactor route (count/start/goal/aux):

| action順 | raw actor | ROM |
|---|---|---|
| 0 | 193 | 3 / 23 / 41 / 50 |
| 1 | 192 | 0 / 47 / 47 / 56 |
| 2 | 0 defend | freecam呼び出しなし |
| 3 | 194 | 4 / 34 / 41 / 40 |
| 4 | 1 | 0 / 61 / 61 / 60 |

`0x0822b3`, 未変更suraimu2.dst, `attack:1` は戦闘計算#136まで一致した
修正版C++でも、ROM camera末#145に対しC++#146になる。
ROM最終guest routeは2/start59/goal49/aux48。
このseedでは余計な1消費が次ターンの全乱数列へ伝播する。

## 直前writerをどう確定したか

常時watchではなく、前行動完了後 `021DBC84` の指定action indexから、
次の `02161FFC` までだけ、上記5物理wordへのwriteを観測した。
write PC/value/SP/LRを保持し、endpointでpauseして確認した。
この方法は重い。終了後は必ずscriptを再ロード/停止してwatchを外す。

### 0x18e0e1、次action index=1

直前freecam actor193/target0、param5=1、roll100=82、camera選択0。
writer SPは `027e3330`。

| 物理slot | 最終PC | 最終word |
|---|---|---|
| 0 | 020b7248 | 2 |
| 1 | 020b73e0 | 856064 |
| 2 | 020b7340 | -1 |
| 3 | 020b7358 | 0 |
| 4 | 020b7384 | 0 |

各PCは同じ `FUN_020b7224` 内。行列を読んで固定小数点の積和を書き、
一部slotには64bit積の上位word/符号拡張が残る。
この捕捉時の行列入力 `0210b438` の9要素は
`[418,-746,4004,-620,3967,802,-4026,-689,293]`、weight=2048。
描画context先頭にはリソース構造を指すpointerと値17があった。
値17を即「骨数」と意味確定しないこと (構造定義の実証は未完)。

### 同seed、次action index=4

writer PC/SPは上と同じ。wordは `[2,-1200128,0,0,0]`、mask11000。
行列は `[-586,974,-3936,202,3976,954,4049,-59,-617]`。
直前freecamはactor194/target0、param5=0、roll100=89、reset roll=1。
同じPCでも入力行列の符号が変わり、残るビット列が変わっている。

### 0x0822b3、次action index=1

直前freecam actor193/target1、param5=1、roll100=59、camera選択3。
writer SPは `027e32d0` (上の捕捉より96byte深い)。contextの対応値は29。
最終PCは `020b7784,020b77b0,020b77ec,020b7818,020b7854`、
wordは `[843,-1,1212,0,-1419]`、mask11101。
同じ関数の違う局所配列が、同じE08BC用物理wordへ重なる。

## Ghidraの使い方と落とし穴

program `dq9_new - コピー.nds`。batch_decompileで確認済み:
`FUN_020b7224`, `FUN_overlay_d_25__021e08bc`,
`FUN_overlay_d_25__021e1958`, `FUN_overlay_d_25__021e2664`,
`FUN_overlay_d_00__0216fda4`。
ARM overlayはアドレスだけで関数を選ぶと別overlayを引く。
実測PCとロード中overlayを合わせること。

## 現在の実装と再開点

`freecam_fast_runtime.hpp` の5人用 `ApplyBattleHudRendererResidueCompatibility`
は前AIの `11111` のまま。この調査では推測マスクへ置き換えていない。
`main.cpp` のcamera境界はauxと全actor routeを出すように拡張済み。

`slime_runtime_probe.js` は `CAPTURE_CAMERA=false` が通常設定。
guest AIの7フックとオンデマンドのCtable読取りだけを動かす。
row4 writerは明示的arm時のみ。user issue.txt v13に従い、
高頻度row4/renderer観測は停止して戦闘実装へ戻った。

必要なのは、判明したwriterへ至る**描画入力/フレーム選択**を限定的に
再現するか、同じ外部挙動になる簡潔な表現を実測で確立すること。
シード別の辻褄合わせ、Ctableの消費補填、乱数を変更した観測、
全描画エンジンの漫然とした移植は行わない。
