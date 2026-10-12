#!/usr/bin/env python3
"""mincecraft_gen.py - world.c's generator, integer for integer (v0.60.192).
   python3 scripts/mincecraft_gen.py SEED   -> prints the FNV-1a hash and spawn top"""
import sys
WX, WZ, WY, SEA = 128, 128, 64, 26
AIR, CRUST, CRUMB, PARM, IRON, BROTH, SUGAR, STALK, FLORET, PEPPER, BUTTER = range(11)
M = 0xFFFFFFFF
def hash2(x, z, seed):
    h = (seed ^ ((x * 0x27d4eb2d) & M) ^ ((z * 0x165667b1) & M)) & M
    h ^= h >> 15; h = (h * 0x2c1b3c6d) & M
    h ^= h >> 12; h = (h * 0x297a2d39) & M
    h ^= h >> 15
    return h
def noise(x, z, s, seed):
    x0, z0, fx, fz = x // s, z // s, x % s, z % s
    v00 = hash2(x0, z0, seed) & 255; v10 = hash2(x0 + 1, z0, seed) & 255
    v01 = hash2(x0, z0 + 1, seed) & 255; v11 = hash2(x0 + 1, z0 + 1, seed) & 255
    a = v00 * (s - fx) + v10 * fx; b = v01 * (s - fx) + v11 * fx
    return (a * (s - fz) + b * fz) // (s * s)
def cdiv(a, b):                                   # C's division: toward zero
    q = abs(a) // abs(b)
    return q if (a >= 0) == (b > 0) else -q
def height(x, z, seed):
    n = (noise(x, z, 32, seed) * 3 + noise(x, z, 16, seed ^ 0x85ebca6b) * 2 + noise(x, z, 8, seed ^ 0x9e3779b9) * 1) // 6
    d = n - 128
    h = 29 + cdiv(d * abs(d), 640) + cdiv(d, 16)
    return 5 if h < 5 else 50 if h > 50 else h
def gen(seed):
    w = bytearray(WX * WZ * WY)
    for z in range(WZ):
        for x in range(WX):
            edge = x == 0 or z == 0 or x == WX - 1 or z == WZ - 1
            h = height(x, z, seed)
            top = SUGAR if h <= SEA else CRUST
            for y in range(WY):
                if edge or y == 0: b = IRON
                elif y < h - 4:
                    r = hash2(x + z * WX, y, seed ^ 0x51ed27)
                    b = PEPPER if r % 97 == 0 else (BUTTER if y < 12 and r % 211 == 1 else PARM)
                elif y < h - 1: b = SUGAR if top == SUGAR else CRUMB
                elif y == h - 1: b = top
                elif y < SEA: b = BROTH
                else: b = AIR
                w[(y * WZ + z) * WX + x] = b
    def get(x, y, z):
        if x < 0 or x >= WX or z < 0 or z >= WZ or y < 0: return IRON
        if y >= WY: return AIR
        return w[(y * WZ + z) * WX + x]
    for z in range(3, WZ - 3):
        for x in range(3, WX - 3):
            h = height(x, z, seed)
            if h <= SEA: continue
            r = hash2(x, z, seed ^ 0x7f4a7c15)
            if r % 60: continue
            t = 4 + (r >> 8) % 3
            for y in range(h, h + t): w[(y * WZ + z) * WX + x] = STALK
            cy = h + t
            for dy in range(-2, 3):
                for dz in range(-2, 3):
                    for dx in range(-2, 3):
                        if dx * dx + dy * dy + dz * dz <= 5 and get(x + dx, cy + dy, z + dz) == AIR:
                            w[((cy + dy) * WZ + z + dz) * WX + x + dx] = FLORET
    return w
def fnv(w):
    h = 2166136261
    for b in w: h = ((h ^ b) * 16777619) & M
    return h
if __name__ == '__main__':
    seed = int(sys.argv[1]) & M
    w = gen(seed)
    top = max(y for y in range(WY) if w[(y * WZ + 64) * WX + 64] not in (AIR, BROTH))
    print('seed=%d hash=%d top=%d' % (seed, fnv(w), top))
