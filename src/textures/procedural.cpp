#include "textures/procedural.h"

#include <algorithm>
#include <cmath>

namespace sp {

namespace {

// PCG32: small, fast, good quality, stable across platforms.
class Pcg32 {
public:
    explicit Pcg32(uint64_t seed) {
        state_ = 0;
        next();
        state_ += seed;
        next();
    }
    uint32_t next() {
        const uint64_t old = state_;
        state_ = old * 6364136223846793005ULL + 1442695040888963407ULL;
        const uint32_t xorshifted = static_cast<uint32_t>(((old >> 18u) ^ old) >> 27u);
        const uint32_t rot = static_cast<uint32_t>(old >> 59u);
        return (xorshifted >> rot) | (xorshifted << ((32u - rot) & 31u));
    }
    uint8_t byte() { return static_cast<uint8_t>(next() >> 24); }
    // Uniform in [0, n).
    uint32_t below(uint32_t n) { return static_cast<uint32_t>((static_cast<uint64_t>(next()) * n) >> 32); }

private:
    uint64_t state_;
};

ImageData blank(int w, int h, int d, int channels) {
    ImageData img;
    img.width = w;
    img.height = h;
    img.depth = d;
    img.channels = channels;
    img.pixels.assign(static_cast<size_t>(w) * h * d * channels, 0);
    return img;
}

// Void-and-cluster (Ulichney 1993) on an n x n torus. Returns a rank in
// [0, n*n) for every pixel; low ranks are spread out as evenly as possible.
std::vector<int> voidAndCluster(int n, uint32_t seed) {
    const int count = n * n;
    const float sigma = 1.5f;
    const int radius = std::min(n / 2, 7);
    std::vector<float> kernel;
    for (int dy = -radius; dy <= radius; ++dy)
        for (int dx = -radius; dx <= radius; ++dx)
            kernel.push_back(std::exp(-(dx * dx + dy * dy) / (2.0f * sigma * sigma)));

    // Membership lists (swap-remove) so each search scans only candidates.
    std::vector<uint8_t> bits(static_cast<size_t>(count), 0);
    std::vector<float> energy(static_cast<size_t>(count), 0.0f);
    std::vector<int> ones, zeros(static_cast<size_t>(count));
    std::vector<int> slot(static_cast<size_t>(count));  // position in ones or zeros
    for (int i = 0; i < count; ++i) zeros[static_cast<size_t>(i)] = slot[static_cast<size_t>(i)] = i;

    auto moveBetween = [&](int idx, std::vector<int>& from, std::vector<int>& to) {
        const int pos = slot[static_cast<size_t>(idx)];
        const int last = from.back();
        from[static_cast<size_t>(pos)] = last;
        slot[static_cast<size_t>(last)] = pos;
        from.pop_back();
        slot[static_cast<size_t>(idx)] = static_cast<int>(to.size());
        to.push_back(idx);
    };
    auto splat = [&](int idx, float sign) {
        const int x0 = idx % n, y0 = idx / n;
        const float* k = kernel.data();
        for (int dy = -radius; dy <= radius; ++dy) {
            float* row = &energy[static_cast<size_t>(((y0 + dy + n) % n) * n)];
            for (int dx = -radius; dx <= radius; ++dx) row[(x0 + dx + n) % n] += sign * *k++;
        }
    };
    auto set = [&](int idx) {
        bits[static_cast<size_t>(idx)] = 1;
        moveBetween(idx, zeros, ones);
        splat(idx, 1.0f);
    };
    auto unset = [&](int idx) {
        bits[static_cast<size_t>(idx)] = 0;
        moveBetween(idx, ones, zeros);
        splat(idx, -1.0f);
    };
    // Tightest cluster: the set pixel with the highest energy.
    auto tightestCluster = [&]() {
        int best = ones[0];
        float bestE = energy[static_cast<size_t>(best)];
        for (int i : ones)
            if (energy[static_cast<size_t>(i)] > bestE) bestE = energy[static_cast<size_t>(best = i)];
        return best;
    };
    // Largest void: the empty pixel with the lowest energy.
    auto largestVoid = [&]() {
        int best = zeros[0];
        float bestE = energy[static_cast<size_t>(best)];
        for (int i : zeros)
            if (energy[static_cast<size_t>(i)] < bestE) bestE = energy[static_cast<size_t>(best = i)];
        return best;
    };

    // Initial pattern: ~10% random points, then relax until stable.
    Pcg32 rng(seed);
    const int initial = std::max(1, count / 10);
    while (static_cast<int>(ones.size()) < initial) {
        const int i = static_cast<int>(rng.below(static_cast<uint32_t>(count)));
        if (!bits[static_cast<size_t>(i)]) set(i);
    }
    for (int iter = 0; iter < count; ++iter) {
        const int cluster = tightestCluster();
        unset(cluster);
        const int hole = largestVoid();
        set(hole);
        if (hole == cluster) break;
    }

    std::vector<int> rank(static_cast<size_t>(count), -1);
    const std::vector<uint8_t> protoBits = bits;
    const std::vector<float> protoEnergy = energy;
    const std::vector<int> protoOnes = ones, protoZeros = zeros, protoSlot = slot;

    // Phase 1: remove points from the prototype, highest ranks first.
    for (int r = initial - 1; r >= 0; --r) {
        const int cluster = tightestCluster();
        unset(cluster);
        rank[static_cast<size_t>(cluster)] = r;
    }
    // Phase 2+3 (simplified): fill the largest voids until the torus is full.
    bits = protoBits;
    energy = protoEnergy;
    ones = protoOnes;
    zeros = protoZeros;
    slot = protoSlot;
    for (int r = initial; r < count; ++r) {
        const int hole = largestVoid();
        set(hole);
        rank[static_cast<size_t>(hole)] = r;
    }
    return rank;
}

}  // namespace

ImageData makeRgbaNoise(int size, uint32_t seed) {
    ImageData img = blank(size, size, 1, 4);
    Pcg32 rng(seed);
    for (int y = 0; y < size; ++y)
        for (int x = 0; x < size; ++x) {
            uint8_t* p = &img.pixels[(static_cast<size_t>(y) * size + x) * 4];
            p[0] = rng.byte();
            p[2] = rng.byte();
        }
    for (int y = 0; y < size; ++y)
        for (int x = 0; x < size; ++x) {
            const size_t dst = (static_cast<size_t>(y) * size + x) * 4;
            const size_t src = (static_cast<size_t>((y + 17) % size) * size + (x + 37) % size) * 4;
            img.pixels[dst + 1] = img.pixels[src + 0];
            img.pixels[dst + 3] = img.pixels[src + 2];
        }
    return img;
}

ImageData makeGrayNoise(int size, uint32_t seed) {
    ImageData img = blank(size, size, 1, 1);
    Pcg32 rng(seed);
    for (auto& v : img.pixels) v = rng.byte();
    return img;
}

ImageData makeBayer8() {
    ImageData img = blank(8, 8, 1, 1);
    for (int y = 0; y < 8; ++y)
        for (int x = 0; x < 8; ++x) {
            // Bit-interleave of (x ^ y, y), reversed: the classic recursive Bayer index.
            int v = 0;
            const int xy = x ^ y;
            for (int bit = 0; bit < 3; ++bit) {
                v = (v << 1) | ((xy >> bit) & 1);
                v = (v << 1) | ((y >> bit) & 1);
            }
            img.pixels[static_cast<size_t>(y * 8 + x)] = static_cast<uint8_t>(v * 255 / 63);
        }
    return img;
}

ImageData makeBlueNoise(int size, int tile, int channels, uint32_t seed) {
    ImageData img = blank(size, size, 1, channels);
    const int count = tile * tile;
    for (int c = 0; c < channels; ++c) {
        const std::vector<int> rank = voidAndCluster(tile, seed + static_cast<uint32_t>(c) * 7919u);
        for (int y = 0; y < size; ++y)
            for (int x = 0; x < size; ++x) {
                const int r = rank[static_cast<size_t>((y % tile) * tile + (x % tile))];
                img.pixels[(static_cast<size_t>(y) * size + x) * channels + c] =
                    static_cast<uint8_t>((static_cast<int64_t>(r) * 256) / count);
            }
    }
    return img;
}

ImageData makeNoiseVolume(int size, int channels, uint32_t seed) {
    ImageData img = blank(size, size, size, channels);
    Pcg32 rng(seed);
    for (auto& v : img.pixels) v = rng.byte();
    return img;
}

void flipRows(ImageData& img) {
    const size_t row = static_cast<size_t>(img.width) * img.channels;
    for (int z = 0; z < img.depth; ++z) {
        uint8_t* slice = img.pixels.data() + static_cast<size_t>(z) * img.height * row;
        for (int y = 0; y < img.height / 2; ++y)
            std::swap_ranges(slice + y * row, slice + (y + 1) * row, slice + (img.height - 1 - y) * row);
    }
}

}  // namespace sp
