#!/usr/bin/env python3
"""Sizes of candidate layouts for a compact Bomb, from the counts of colorless_scan.

    python3 storage.py results/scan-24.data [results/scan-16.data ...]

For each Bomb: what the REVs take under each way of writing them, what the
index takes, and what the combinations come to.  Then, if several files are
given, how the counts move with the length.

Everything here is arithmetic on the numbers the scan wrote: the number of
strings of each node count, the number of different canonical topologies
among them, and the number of strings whose stored coloring is not the first
one the recoloring search finds.  No Bomb is read.
"""
import math
import sys

SMALL = 5  # REVs of up to 5 nodes stay as the compact format has them (see REPORT.md)


def read(path):
    d = {"class": {}}
    for line in open(path):
        w = line.split()
        if w[0] == "class":
            c = d["class"].setdefault(int(w[1]), {})
            for k, v in zip(w[2::2], w[3::2]):
                c[k] = float(v) if "." in v else int(v)
        else:
            d[w[0]] = w[1]
    d["bits"] = int(d["bits"])
    return d


def link_bits(n):
    w = 0
    while (1 << w) < n:
        w += 1
    return w


def packed(count, bits):
    """count records of `bits` bits, packed without gaps, in bytes."""
    return (count * bits + 7) // 8


def fields_bytes(n):
    return (n * (1 + 2 * link_bits(n)) + 7) // 8


def base_n_bits(n):
    """colors as bits, links as one number in base n"""
    return n + (math.ceil(2 * n * math.log2(n) - 1e-9) if n > 1 else 0)


def sizes(d):
    L = d["bits"]
    classes = d["class"]
    count = 1 << L
    present = len(classes)
    out = {}
    # --- the REVs
    fields = sum(c["strings"] * fields_bytes(n) for n, c in classes.items())
    small = sum(c["strings"] * fields_bytes(n) for n, c in classes.items() if n <= SMALL)
    base_n = sum(packed(c["strings"], base_n_bits(n)) for n, c in classes.items())
    plain = sum(packed(c["strings"], 2 * n * link_bits(n)) for n, c in classes.items())
    rank_colors = small + sum(packed(c["strings"], math.ceil(c["any_bits"]) + n) for n, c in classes.items() if n > SMALL)
    rank = small + sum(packed(c["strings"], math.ceil(c["any_bits"])) for n, c in classes.items() if n > SMALL)
    listed = sum(c["strings"] - c["first0_original"] for n, c in classes.items() if n > SMALL)
    # a dictionary of the topologies in use, and for each string which one
    dictionary = small
    dictionary_entropy = small
    for n, c in classes.items():
        if n <= SMALL:
            continue
        name = math.ceil(math.log2(c["topologies"])) if c["topologies"] > 1 else 0
        table = packed(c["topologies"], math.ceil(c["any_bits"]))
        dictionary += packed(c["strings"], name) + table
        dictionary_entropy += math.ceil(c["strings"] * c["entropy"] / 8) + table
    out["pool"] = [
        ("bit fields: color and two links per node (as built)", fields),
        ("colors as bits, links as one number in base N", base_n),
        ("links only, as bit fields (colors found by search)", plain),
        ("rank of the canonical topology, and colors", rank_colors),
        ("rank of the canonical topology (colors found by search)", rank + 5 * listed),
        ("dictionary of topologies in use + which one, no colors", dictionary + 5 * listed),
        ("  the same if naming one cost its entropy", dictionary_entropy + 5 * listed),
    ]
    # --- the index
    ordinal_bytes = (L + 7) // 8
    ordinals = ordinal_bytes * (2 * count - 2)
    a = (count + 7) // 8
    b = count // 2 + ((count >> 8) + 1) * present * 4
    out["index"] = [
        ("an ordinal for every string of 1 to L bits (as built)", ordinals),
        ("A: one bit for every shorter string", a),
        ("B: node count of every string in 4 bits, checkpoint every 256", b),
    ]
    half_b = count // 4 + ((count >> 9) + 1) * present * 4
    fixed = 64 + (19 if L <= 16 else 34 if L <= 24 else 44) * count
    out["fixed"] = fixed
    out["layouts"] = [
        ("V  as built: ordinals, bit fields", ordinals + fields, None),
        ("W  A + B, bit fields", a + b + fields, a + half_b + fields // 2),
        ("   A + B, base N", a + b + base_n, a + half_b + base_n // 2),
        ("Y  A + B, rank and colors", a + b + rank_colors, a + half_b + rank_colors // 2),
        ("X  A + B, rank, colors by search", a + b + rank + 5 * listed, a + half_b + (rank + 5 * listed) // 2),
        ("   A + B, dictionary, colors by search", a + b + dictionary + 5 * listed, None),
    ]
    out["listed"] = listed
    return out


def show(d):
    L = d["bits"]
    s = sizes(d)
    fixed = s["fixed"]
    print(f"FixedRunBomb({L}), table CRC-32 {d['crc']}: fixed-width file {fixed:,} bytes\n")
    print("  Per REV, in bits            " + "".join(f"{n:>8}" for n in sorted(d["class"])) + "   nodes")
    rows = [
        ("strings", lambda n, c: f"{c['strings']:,}"),
        ("bit fields (as built)", lambda n, c: f"{8 * fields_bytes(n)}"),
        ("colors + links in base N", lambda n, c: f"{base_n_bits(n)}"),
        ("rank + colors", lambda n, c: f"{math.ceil(c['any_bits']) + n}"),
        ("rank", lambda n, c: f"{math.ceil(c['any_bits'])}"),
        ("topologies in use", lambda n, c: f"{c['topologies']:,}"),
        ("bits to name one in use", lambda n, c: f"{math.ceil(math.log2(c['topologies'])) if c['topologies'] > 1 else 0}"),
        ("dictionary, per string", lambda n, c: f"{(math.ceil(math.log2(c['topologies'])) if c['topologies'] > 1 else 0) + c['topologies'] * math.ceil(c['any_bits']) / c['strings']:.1f}"),
    ]
    for name, f in rows:
        cells = [f(n, d["class"][n]) for n in sorted(d["class"])]
        width = max(8, max(len(x) for x in cells) + 2)
        print(f"  {name:<28}" + "".join(f"{x:>{width}}" for x in cells))
    print("\n  The REVs                                                          bytes")
    for name, b in s["pool"]:
        print(f"  {name:<58} {b:>14,}")
    print(f"  ({s['listed']:,} strings whose stored coloring is not the first found are listed, 5 bytes each)")
    print("\n  The index                                                         bytes")
    for name, b in s["index"]:
        print(f"  {name:<58} {b:>14,}")
    print("\n  Together                                         bytes   of fixed   only strings that start with 0")
    for name, b, half in s["layouts"]:
        extra = f"{half:>14,}  {100 * half / fixed:5.1f}%" if half is not None else ""
        print(f"  {name:<40} {b:>14,}   {100 * b / fixed:5.1f}%   {extra}")
    print()


def trend(all_d):
    print("By length\n")
    print("  Bits      Strings   Topologies  Strings per  More than one   Next bit    Stream     First coloring")
    print("                                    topology      coloring        open       open     not the stored one")
    for d in sorted(all_d, key=lambda x: x["bits"]):
        c = d["class"].values()
        strings = sum(x["strings"] for x in c)
        topo = sum(x["topologies"] for x in c)
        many = sum(x["many"] for x in c)
        nxt = sum(x["next_open"] for x in c)
        stream = sum(x["stream_open"] for x in c)
        other = sum(x["strings"] - x["first0_original"] for x in c)
        print(f"  {d['bits']:>4} {strings:>12,} {topo:>12,} {strings / topo:>10.3f} {many:>9,} ({100 * many / strings:.4f}%)"
              f" {nxt:>7,} {stream:>9,} {other:>11,} ({100 * other / strings:.4f}%)")
    print()


def main():
    if len(sys.argv) < 2:
        print(__doc__)
        return 2
    all_d = [read(p) for p in sys.argv[1:]]
    show(all_d[0])
    if len(all_d) > 1:
        trend(all_d)
    return 0


if __name__ == "__main__":
    sys.exit(main())
