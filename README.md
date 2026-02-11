## DQIX Solo Travel Battle Emulator

- Highly optimized, algorithmic combinatorial optimization
- Written in C++
- 100% open source
- Individual management with git branches

## What is our goal?
Our goal is to create a fully debugged battle emulator with many story bosses, and we will continue to move forward with this goal.  
Our team dedicates significant time to debugging to ensure the battle emulator perfectly matches actual gameplay on the hardware.　　

## Try Online!

|               emu               |      Bosses      |                                   url                                    | example                                                                                                                                  |
|:-------------------------------:|:----------------:|:------------------------------------------------------------------------:|------------------------------------------------------------------------------------------------------------------------------------------|
|    reokonn_lv8_new_arugo_v2     |   Wight Knight   |      [link](https://dqix.github.io/BattleEmulator/?emu=reokonn_v6)       | [example](https://dqix.github.io/BattleEmulator/?emu=reokonn_v6&offset=15&range=4&input=0+1+33+b+7+20+a14+16+h34+14+a58)                 |
|      yo2_lv5_algorithm_v4       |      Morag       |     [link](https://dqix.github.io/BattleEmulator/?emu=isilyudaru_v6)     | [example](https://dqix.github.io/BattleEmulator/?emu=isilyudaru_v6&offset=15&range=2&input=0+2+23+a16+10+a16+11+a16+11+a16+h32+y+11+a16) |
|       bilyouma_new_arugo        | Ragin' Contagion |      [link](https://dqix.github.io/BattleEmulator/?emu=bilyouma_v6)      |                                                                                                                                          |
|  zilyadama_new_arugo_tamahane   | Master of Nu'un  | [link](https://dqix.github.io/BattleEmulator/?emu=zilyadama_v6_tamahane) |                                                                                                                                          |
|   zilyadama_new_arugo_hagane    | Master of Nu'un  |  [link](https://dqix.github.io/BattleEmulator/?emu=zilyadama_v6_hagane)  |                                                                                                                                          |
| nusisama1_v2_new_arugo_tamahane |    Lleviathan    | [link](https://dqix.github.io/BattleEmulator/?emu=nusisama1_v6_tamahane) |                                                                                                                                          |
|  nusisama1_v2_new_arugo_hagane  |    Lleviathan    | [link](https://dqix.github.io/BattleEmulator/?emu=nusisama1_v6_hagane )  |                                                                                                                                          |


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

|    ver    |       used        | description                                                                                        |
|:---------:|:-----------------:|----------------------------------------------------------------------------------------------------|
|   v2 🦍   | Best-first search | Used by Corvus for compatibility with older battle emulators                                       |
|   v4 🔍   |   A* algorithm    | Much better than v2. Maintenance costs are quite high when porting. Maximum 2 million turns/second |
|  v6 🔍⚡   |   A* algorithm+   | A* algorithm with reduced maintenance costs                                                        |
|  v7 💥🐎  | Brute force+beam  | 5-turn brute force-based + beam search algorithm. Discontinued because it lost to v6.              |


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