"""Pair kerning from a font's OpenType GPOS table, in em.

msdf-atlas-gen only reads the legacy 'kern' table, which modern fonts (every Google font here)
no longer carry: their kerning lives in GPOS pair adjustment lookups. This reads those, for the
pairs of a charset, the way a shaper applies them to a horizontal, left-to-right run:

  - the lookups the 'kern' feature uses (in any script and language), in lookup order;
  - within a lookup, the first subtable that covers a pair decides it;
  - across lookups, adjustments add up;
  - the value is the first glyph's XAdvance (the space between the two glyphs).

Class kerning (PairPos format 2) is expanded to concrete pairs, so the result is a flat
{"AV": -0.071, ...} map with zero pairs left out.
"""
from fontTools.ttLib import TTFont


def _kern_lookup_indices(gpos) -> list[int]:
    table = gpos.table
    if not table.FeatureList or not table.LookupList:
        return []
    indices = set()
    for record in table.FeatureList.FeatureRecord:
        if record.FeatureTag == "kern":
            indices.update(record.Feature.LookupListIndex)
    return sorted(indices)


def _pair_subtables(lookup):
    for sub in lookup.SubTable:
        if lookup.LookupType == 9:  # extension: the real subtable is inside
            if sub.ExtensionLookupType != 2:
                continue
            sub = sub.ExtSubTable
        elif lookup.LookupType != 2:
            continue
        yield sub


def _x_advance(value) -> int:
    if value is None:
        return 0
    return getattr(value, "XAdvance", 0) or 0


def _subtable_value(sub, first: str, second: str):
    """The subtable's adjustment for the pair, or None if it does not cover the pair."""
    coverage = sub.Coverage.glyphs
    if first not in coverage:
        return None
    if sub.Format == 1:
        pair_set = sub.PairSet[coverage.index(first)]
        for record in pair_set.PairValueRecord:
            if record.SecondGlyph == second:
                return _x_advance(record.Value1)
        return None
    if sub.Format == 2:
        class1 = sub.ClassDef1.classDefs.get(first, 0)
        class2 = sub.ClassDef2.classDefs.get(second, 0)
        return _x_advance(sub.Class1Record[class1].Class2Record[class2].Value1)
    return None


def gpos_kerning(ttf_path, charset: str) -> dict[str, float]:
    font = TTFont(str(ttf_path))
    upem = font["head"].unitsPerEm
    cmap = font.getBestCmap()
    chars = [c for c in charset if ord(c) in cmap]
    pairs: dict[str, int] = {}

    if "GPOS" in font:
        lookups = font["GPOS"].table.LookupList.Lookup
        for index in _kern_lookup_indices(font["GPOS"]):
            subtables = list(_pair_subtables(lookups[index]))
            if not subtables:
                continue
            for a in chars:
                glyph_a = cmap[ord(a)]
                for b in chars:
                    glyph_b = cmap[ord(b)]
                    for sub in subtables:
                        value = _subtable_value(sub, glyph_a, glyph_b)
                        if value is not None:
                            if value:
                                pairs[a + b] = pairs.get(a + b, 0) + value
                            break

    if "kern" in font:  # a font with both: the legacy table adds on, as old shapers would
        for table in font["kern"].kernTables:
            for (glyph_a, glyph_b), value in table.kernTable.items():
                for a in chars:
                    if cmap[ord(a)] != glyph_a:
                        continue
                    for b in chars:
                        if cmap[ord(b)] == glyph_b and value:
                            pairs[a + b] = pairs.get(a + b, 0) + value

    return {pair: round(value / upem, 5) for pair, value in sorted(pairs.items()) if value}
