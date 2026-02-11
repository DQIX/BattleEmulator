## DQIX Solo Travel Battle Emulator

- Highly optimized, algorithmic combinatorial optimization
- Written in C++
- 100% open source
- Individual management with git branches

## What is our goal?
Our goal is to create a fully debugged battle emulator with many story bosses, and we will continue to move forward with this goal.  
Our team dedicates significant time to debugging to ensure the battle emulator perfectly matches actual gameplay on the hardware.　　


## Known Issues
### The random number scaling in the Battle Emulator is not mathematically exact.

The Battle Emulator scales the random value using the following integer-based formula for performance reasons  
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
a small quantization error occurs. The maximum deviation is less than `0.99999999976`