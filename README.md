## DQIX Solo Travel Battle Emulator

- Highly optimized, algorithmic combinatorial optimization
- Written in C++
- 100% open source
- MIT License
- Individual management with git branches
- For RTA Players

## Disclaimer
This project does not distribute or include any copyrighted game data.<br>
Our battle emulator is designed to avoid including any copyrighted data.<br>
The terms and conditions for avoiding conflicts on the Battle Emulator can be [viewed here](https://www.dropbox.com/scl/fi/ljf4ij06fux9s2527dnyp/DQ9_BattleEmu_Readme.txt?rlkey=o5xmzegriye8krmni3ma7zg82&st=1x0aowuh&dl=0) (Japanese)


## What is our goal?
Our goal is to create a fully debugged battle emulator with many story bosses, and we will continue to move forward with this goal.  
Our team dedicates significant time to debugging to ensure the battle emulator perfectly matches actual gameplay on the hardware.　　

## RTA chart and input method for argument parser
https://note.com/zeppeki0711/n/neac461916cc8

# Quick Start
## Try Online!

Run the battle emulator on your CPU in your browser!<br>

|               emu               |      Bosses      |                                   url                                    |                                                                 example                                                                  |
|:-------------------------------:|:----------------:|:------------------------------------------------------------------------:|:----------------------------------------------------------------------------------------------------------------------------------------:|
|    reokonn_lv8_new_arugo_v2     |   Wight Knight   |      [link](https://dqix.github.io/BattleEmulator/?emu=reokonn_v6)       |         [example](https://dqix.github.io/BattleEmulator/?emu=reokonn_v6&offset=15&range=4&input=0+1+33+b+7+20+a14+16+h34+14+a58)         |
|      yo2_lv5_algorithm_v4       |      Morag       |     [link](https://dqix.github.io/BattleEmulator/?emu=isilyudaru_v6)     | [example](https://dqix.github.io/BattleEmulator/?emu=isilyudaru_v6&offset=15&range=2&input=0+2+23+a16+10+a16+11+a16+11+a16+h32+y+11+a16) |
|       bilyouma_new_arugo        | Ragin' Contagion |      [link](https://dqix.github.io/BattleEmulator/?emu=bilyouma_v6)      |      [example](https://dqix.github.io/BattleEmulator/?emu=bilyouma_v6&offset=15&range=2&input=0+5+45+a24+b+11+a25+12+13+a23+10+11)       |
|  zilyadama_new_arugo_tamahane   | Master of Nu'un  | [link](https://dqix.github.io/BattleEmulator/?emu=zilyadama_v6_tamahane) | [example](https://dqix.github.io/BattleEmulator/?emu=zilyadama_v6_tamahane&offset=15&range=4&input=0+2+19+a42+s+18+a31+29+a30+m+17+12+h) |
|   zilyadama_new_arugo_hagane    | Master of Nu'un  |  [link](https://dqix.github.io/BattleEmulator/?emu=zilyadama_v6_hagane)  |                                                                   n/a                                                                    |
| nusisama1_v2_new_arugo_tamahane |    Lleviathan    | [link](https://dqix.github.io/BattleEmulator/?emu=nusisama1_v6_tamahane) |     [example](https://dqix.github.io/BattleEmulator/?emu=nusisama1_v6_tamahane&offset=15&range=4&input=0+15+27+a40+t33+a40+t30+h+0)      |
|  nusisama1_v2_new_arugo_hagane  |    Lleviathan    | [link](https://dqix.github.io/BattleEmulator/?emu=nusisama1_v6_hagane )  |                                                                   n/a                                                                    |

## Clion with Windows11

1. Clone the project
2. Open the project in Clion
3. switch branch
4. Run the project with Arguments
5. Enjoy!

## Contribution
### What you need
- JetBrains Clion(Free!) or virtual studio code 2026 c++ mode
- DeSmuME Nightly with Lua scripting
- [lua51.dll](https://sourceforge.net/projects/luabinaries/files/5.1.5/Tools%20Executables/lua-5.1.5_Win64_bin.zip/download)
- git
- [Ghidra](https://github.com/DQIX/dqix-functions/issues/2)
- [Ctable_jp.lua](https://github.com/DQIX/desmume-scripts/blob/main/jpn/Ctable_jp.lua)
- DQ9 Japanese ROM
- Boss Save Data

Interested in contributing? Hit us up on Twitter!  

https://x.com/Daisuke76897125

## branches
This repository manages the battle emulator in branches<br>
Note that v6 🔍⚡ is better than v7 💥🐎 and abandons    
  
|                                              Branch                                              |              Bosses               | Target                                                                                                               　       | Optimizer |
|:------------------------------------------------------------------------------------------------:|:---------------------------------:|------------------------------------------------------------------------------------------------------------------------------|-----------|
|    [reokonn_lv8_new_arugo](https://github.com/DQIX/BattleEmulator/tree/reokonn_lv8_new_arugo)    |           Wight Knight            | [Minstrel lv8](https://github.com/DQIX/BattleEmulator/blob/reokonn_lv8_new_arugo/image/reokonn_lv8_v3.png)                   | v6 🔍⚡    |
| [reokonn_lv8_new_arugo_v2](https://github.com/DQIX/BattleEmulator/tree/reokonn_lv8_new_arugo_v2) |           Wight Knight            | [Minstrel lv8](https://github.com/DQIX/BattleEmulator/blob/reokonn_lv8_new_arugo_v2/image/reokonn_lv8_v3.png)                | v7 💥🐎   |
|     [yo2_lv5_algorithm_v4](https://github.com/DQIX/BattleEmulator/tree/yo2_lv5_algorithm_v4)     |               Morag               | [Minstrel lv10](https://github.com/DQIX/BattleEmulator/blob/yo2_lv5_algorithm_v4/image/isilyudaru_lv10.png)                  | v6 🔍⚡    |
|     [yo2_lv5_algorithm_v2](https://github.com/DQIX/BattleEmulator/tree/yo2_lv5_new_arugo_v2)     |               Morag               | [Minstrel lv10](https://github.com/DQIX/BattleEmulator/blob/yo2_lv5_new_arugo_v2/image/isilyudaru_lv10.png)                  | v7 💥🐎   |
|       [bilyouma_new_arugo](https://github.com/DQIX/BattleEmulator/tree/bilyouma_new_arugo)       |         Ragin' Contagion          | [Minstrel lv15 sp22](https://github.com/DQIX/BattleEmulator/blob/bilyouma_new_arugo/image/bilyouma_metaru1_lv15_sp22.png)    | v6 🔍⚡    |
|    [bilyouma_new_arugo_v2](https://github.com/DQIX/BattleEmulator/tree/bilyouma_new_arugo_v2)    |         Ragin' Contagion          | [Minstrel lv15 sp22](https://github.com/DQIX/BattleEmulator/blob/bilyouma_new_arugo_v2/image/bilyouma_metaru1_lv15_sp22.png) | v7 💥🐎   |
|      [zilyadama_new_arugo](https://github.com/DQIX/BattleEmulator/tree/zilyadama_new_arugo)      |          Master of Nu'un          | lv16_sp22_tamahagane_atk123_def86 or lv16_sp22_tamahagane_atk123_def86                                                       | v6 🔍⚡    |
|   [nusisama1_v2_new_arugo](https://github.com/DQIX/BattleEmulator/tree/nusisama1_v2_new_arugo)   |            Lleviathan             | lv17_sp22_tamahane_atk125_def93 or lv17_sp22_hagane_atk108_def93                                                             | v6 🔍⚡    |
|                   [zuo_v2](https://github.com/DQIX/BattleEmulator/tree/zuo_v2)                   |             Tyrantula             | n/a                                                                                                                          | v2 🦍     |
|                    [anonn](https://github.com/DQIX/BattleEmulator/tree/anonn)                    |           Grand Lizzier           | n/a                                                                                                                          | v2 🦍     |
|                 [erugiosu](https://github.com/DQIX/BattleEmulator/tree/erugiosu)                 |              Corvus               | n/a                                                                                                                          | v2 🦍     |


## Optimization Algorithms

|    ver    |       used        | description                                                                           |
|:---------:|:-----------------:|---------------------------------------------------------------------------------------|
|   v2 🦍   | Best-first search | Used by Corvus for compatibility with older battle emulators                          |
|   v4 🔍   |   A* algorithm    | Much better than v2. Maintenance costs are quite high when porting. Maximum 2 million |
|  v6 🔍⚡   |   A* algorithm+   | A* algorithm with reduced maintenance costs                                           |
|  v7 💥🐎  | Brute force+beam  | 5-turn brute force-based + beam search algorithm. Discontinued because it lost to v6. |

## Benchmark

* x86_64: i7 14700F 4.5Ghz, windows11 25h2, with msbuild -O3
* webassembly: Brave -O3

### Turns per Second

|         Branch         |     Bosses      | BruteForcer x86_64 | searcher x86_64 | BruteForcer Webassembly | searcher Webassembly |
|:----------------------:|:---------------:|:------------------:|:---------------:|:-----------------------:|:--------------------:|
| reokonn_lv8_new_arugo  |  Wight Knight   |     17,074,700     |    2,544,600    |       16,350,000        |      1,830,000       |
|  yo2_lv5_algorithm_v4  |      Morag      |     19,800,800     |    1,187,300    |       15,310,000        |       840,000        |
|   bilyouma_new_arugo   | Ragin' Contagio |     14,676,800     |    1,963,200    |       13,340,000        |      1,710,000       |
|  zilyadama_new_arugo   | Master of Nu'un |     24,663,100     |    1,407,700    |       15,180,000        |      1,150,000       |
| nusisama1_v2_new_arugo |   Lleviathan    |     23,277,000     |    1,462,900    |       15,400,000        |      1,170,000       |

### Cycles per Turn

|         Branch         |     Bosses      | BruteForcer x86_64 | searcher x86_64 | BruteForcer Webassembly | searcher Webassembly |
|:----------------------:|:---------------:|:------------------:|:---------------:|:-----------------------:|:--------------------:|
| reokonn_lv8_new_arugo  |  Wight Knight   |       263.55       |    1,768.45     |         275.23          |       2,459.02       |
|  yo2_lv5_algorithm_v4  |      Morag      |       227.26       |    3,790.11     |         293.93          |       5,357.14       |
|   bilyouma_new_arugo   | Ragin' Contagio |       306.61       |    2,292.18     |         337.33          |       2,631.58       |
|  zilyadama_new_arugo   | Master of Nu'un |       182.46       |    3,196.70     |         296.44          |       3,913.04       |
| nusisama1_v2_new_arugo |   Lleviathan    |       193.32       |    3,076.08     |         292.21          |       3,846.15       |

## Known Issues
### The random number scaling in the Battle Emulator is not mathematically exact.

The Battle Emulator scales the random value using the following integer-based formula for performance reasons:
```math
$$
((\text{seed} \gg 32) \cdot \text{max}) \gg 32
$$

```
The mathematically ideal form would be:  
```math
$$
((\text{seed} \gg 32) / 4294967295) \cdot \text{max}
$$
```
Because the implementation uses integer shifting instead of exact division,  
a small quantization error occurs. The maximum deviation is less than $`1 / 2^{32}`$.  
This is because the implementation effectively divides by $`2^{32}`$ instead of $`2^{32} - 1`$.  
   
For consistency reasons, the constant $`2^{32}`$ is also used in other random number calculations.  

### There is no standard for version matching between battle emulators

Battle emulators are effectively snapshots of the latest implementation at the time of development, and there is no mechanism to automatically synchronize versions.  

For example, [erugiosu](https://github.com/DQIX/BattleEmulator/tree/erugiosu) is significantly outdated and does not support the latest algorithms. Even within the _new_arugo series, multiple internal versions exist.  

In general, newer versions tend to have improved processing speed and more refined algorithms. When the gap between versions becomes too large, a reimplementation is sometimes performed to bridge the differences between versions.  

## help wanted
- [ ] An algorithm that always outputs the optimal solution without using heuristics
- [ ] Accurate implementation of more story boss battle emulators
- [ ] Those who can spare a lot of time and manpower for debugging

## Q&A
### What regions does Battle Emulator target?
Targeted and tested only in JP

### Why is this free and open source?
It's available for free thanks to volunteers who have dedicated significant amounts of their personal time and money to making the Battle Emulator accurate

### Why c++?
C++ was chosen because it is the fastest language and allows for highly optimized algorithms<br>
It is thanks to C++ that the brute force can be completed in 1 seconds<br>

### How can you manage a 48-bit brute force in 1 second?
The initial seed of the C table in DQ9 is based on a timer that starts when the game launches.<br>  
This results in $`2^{48}`$ possible combinations, and it increments approximately 520,000 times per second.<br>
The 48-bit counter is structured as follows:<br>
- The lower 16 bits come from CPU Timer 1.<br>
- The upper 32 bits come from a software timer.<br>
The upper 32-bit software timer increases about 7.920 times per second in practice.<br>
However, for simplicity, this can be approximated as exactly 8.0000 increments per second:<br>
<br>

```math
\frac{1}{8.0000} = 0.125
````

<br>
The measured value 7.920 can be interpreted as the real-world effective frequency when accounting for human timing error and practical measurement conditions.<br>
<br>
Since the full 48-bit value combines the upper 32 bits and lower 16 bits, the total increment rate becomes:<br>
<br>
Using the idealized value:<br>
<br>

```math
8.0000 \times 2^{16} = 524{,}288
```
<br>
Using the observed value:<br>
<br>

```math
7.920 \times 2^{16} = 519{,}045.12
```
<br>
Both results are close to the previously mentioned figure of roughly 520,000 increments per second.<br>
<br>
Under the simplified 8.0000 assumption, the approximate current seed can therefore be written as:<br>
<br>

```math
\left\lfloor \text{totalSeconds} \times (8.0000 \times 2^{16}) \right\rfloor
```
<br>
or equivalently:<br>
<br>

```math
\left\lfloor \text{totalSeconds} \times 524{,}288 \right\rfloor
```
<br>
This approximation makes the constant 0.125 a clean reciprocal representation of the upper timer frequency, while 7.920 represents the empirically observed effective rate in real conditions.<br>

### Why is the brute force algorithm so slow?
The Battle Emulator can execute 13 to 17 million times per second, but the v4 and v6 brute force algorithms are based on a very slow priority queue.<br>
Even with optimizations such as malloc and fixed memory allocation using LinearIdPool.h, which is present in some of the source code, the extremely slow speed cannot be overcome.<br>
In v7, the search algorithm switched to brute force, allowing it to achieve 17 million turns per second, but since it used heuristics it could not exceed A*(v6), so it was abandoned.<br>

### Why does the API for each battle emulator differ?
The Battle Emulator started out in an incomplete state, gradually incorporating various techniques and ergonomic APIs, and since it's not possible to deploy ideas to each branch at the same time, differences in implementation arise.<br>

### Why one boss per branch?
Although many battle mechanics are shared and could technically be combined into a single executable, doing so would require externalizing a large number of battle-dependent parameters. A fully generalized and complete decompilation of the battle system would significantly increase code size and blur the separation between the emulator and the original game logic.<br>
By isolating one boss per branch, each boss can maintain its own constexpr values, constants, action selection logic, and argument parser independently. Each branch effectively becomes a self-contained black box.<br>
This separation greatly simplifies maintenance, reduces unintended cross-effects between bosses, and allows boss-specific optimizations without increasing overall structural complexity.<br>
The emulator focuses strictly on precise RNG position tracking and damage calculation. By ignoring unrelated game systems, it preserves both compactness and execution speed.<br>

## Official image rules for this repository
> このページで利用している株式会社スクウェア・エニックスを代表とする共同著作者が権利を所有する画像の転載・配布は禁止いたします。  
> © 2009 ARMOR PROJECT/BIRD STUDIO/LEVEL-5/SQUARE ENIX All Rights Reserved.  