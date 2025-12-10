# Description
This is a delta compression algorithem developed by Haoliang Tan, the first author of [Exploring the Potential of Fast Delta Encoding: Marching to a Higher Compression Ratio](https://ieeexplore.ieee.org/abstract/document/9229609/) .

As written in the abstract section of the paper above, Gdelta is better than Xdelta:
>Our evaluation results driven by six real-world datasets suggest that Gdelta achieves encoding/decoding speedups of 2X∼4X over the classic Xdelta and Zdelta approaches while increasing the compression ratio by about 10%∼120%.

# How to Use
```
mkdir -p build && cd build
cmake ..
make -j
```

# Author
It's not written by myself, It was written by my upperclassman HaoLiang Tan.

His Github homepage is [here](https://github.com/AnsonHooL).

He has another more complex version of gdelta using zstd, you can see [here](https://github.com/AnsonHooL/gdelta).

# Community Implementations

The following are third-party implementations of the GDelta algorithm. These are maintained independently and we have not verified their correctness or performance claims.

## Rust

- **[gdelta-rs](https://github.com/ImGajeed76/gdelta)** by [@ImGajeed76](https://github.com/ImGajeed76) - A pure Rust implementation with SIMD optimization. Claims ~1 GB/s encoding speed and memory-safe implementation.

*If you've created an implementation in another language, feel free to open a PR to add it here.*

