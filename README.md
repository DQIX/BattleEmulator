## DQIX Solo Travel Battle Emulator

- Highly optimized, algorithmic combinatorial optimization
- Written in C++
- 100% open source
- Individual management with git branches

## What is our goal?
Our goal is to create a fully debugged battle emulator with many story bosses, and we will continue to move forward with this goal.  
Our team dedicates significant time to debugging to ensure the battle emulator perfectly matches actual gameplay on the hardware.　　


## branches
This repository manages the battle emulator in branches.　　


|                                            Branch                                             |      Bosses      | Target                                                                                                               　    |
|:---------------------------------------------------------------------------------------------:|:----------------:|---------------------------------------------------------------------------------------------------------------------------|
|  [reokonn_lv8_new_arugo](https://github.com/DQIX/BattleEmulator/tree/reokonn_lv8_new_arugo)   |   Wight Knight   | [Minstrel lv8](https://github.com/DQIX/BattleEmulator/blob/reokonn_lv8_new_arugo/image/reokonn_lv8_v3.png)                |
|   [yo2_lv5_algorithm_v4](https://github.com/DQIX/BattleEmulator/tree/yo2_lv5_algorithm_v4)    |      Morag       | [Minstrel lv10](https://github.com/DQIX/BattleEmulator/blob/yo2_lv5_algorithm_v4/image/isilyudaru_lv10.png)               |
|     [bilyouma_new_arugo](https://github.com/DQIX/BattleEmulator/tree/bilyouma_new_arugo)      | Ragin' Contagion | [Minstrel lv15 sp22](https://github.com/DQIX/BattleEmulator/blob/bilyouma_new_arugo/image/bilyouma_metaru1_lv15_sp22.png) |
|    [zilyadama_new_arugo](https://github.com/DQIX/BattleEmulator/tree/zilyadama_new_arugo)     | Master of Nu'un  | lv16_sp22_tamahagane_atk123_def86 or lv16_sp22_tamahagane_atk123_def86                                                    |
| [nusisama1_v2_new_arugo](https://github.com/DQIX/BattleEmulator/tree/nusisama1_v2_new_arugo)] |    Lleviathan    | lv17_sp22_tamahane_atk125_def93 or lv17_sp22_hagane_atk108_def93                                                          |
|                 [zuo_v2](https://github.com/DQIX/BattleEmulator/tree/zuo_v2)                  |    Tyrantula     | n/a                                                                                                                       |
|               [erugiosu](https://github.com/DQIX/BattleEmulator/tree/erugiosu)                |      Corvus      | n/a                                                                                                                       |


## Optimization Algorithms

|   ver   |       used        | description                                                                                        |
|:-------:|:-----------------:|----------------------------------------------------------------------------------------------------|
|  v2 🦍  | Best-first search | Used by Corvus for compatibility with older battle emulators                                       |
|  v4 🔍  |   A* algorithm    | Much better than v2. Maintenance costs are quite high when porting. Maximum 2 million turns/second |
| v6 🔍⚡  |   A* algorithm+   | A* algorithm with reduced maintenance costs                                                        |
| v7 💥🐎 | Brute force+beam  | 5-turn brute force-based + beam search algorithm. Discontinued because it lost to v6.              |


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
