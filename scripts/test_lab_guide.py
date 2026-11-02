#!/usr/bin/env python3
"""Check that docs/LAB.md keeps up: everything the Lab offers is named, and its pictures and links work."""
from pathlib import Path
import re

root = Path(__file__).resolve().parents[1]
guide = (root / 'docs/LAB.md').read_text()
lab = (root / 'bots/CardputerADV/src/lab.h').read_text()
menu = (root / 'bots/CardputerADV/src/main_parts/004_main.cpp.inc').read_text()
pictures = root / 'docs/assets/lab'


def table(start):
    return lab[lab.index(start):lab.index('};', lab.index(start))]


# What the firmware offers, by the names on its screens.
names = re.findall(r'^\s*\{"[a-z0-9_]+", "([^"]+)"', table('constexpr SigInfo kSigs') + '\n' + table('constexpr OutInfo kOuts'), re.M)
names += re.findall(r'"([^"]+)"', lab[lab.index('kDevNames[kDevCount] = {'):lab.index('};', lab.index('kDevNames[kDevCount] = {'))])
names += re.findall(r'^\s*\{"([^"]+)", "', table('constexpr Recipe kRecipes'), re.M)
names += re.findall(r'\{"([^"]+)", UiMode::Lab', menu)
assert len(names) >= 12 + 14 + 17 + 10 + 3, names
missing = [name for name in names if name not in guide]
assert not missing, f'docs/LAB.md never mentions {missing}'

# Every picture exists, and none is left behind by a screen that went away.
used = set(re.findall(r'assets/lab/([a-z0-9-]+\.png)', guide))
have = {path.name for path in pictures.glob('*.png')}
assert used <= have, f'docs/LAB.md shows pictures that are missing: {sorted(used - have)}'
assert have <= used, f'pictures docs/LAB.md no longer shows: {sorted(have - used)}'

# Every link inside the page lands on a heading, the way GitHub names them.
def slug(title):
    return re.sub(r'[^a-z0-9 -]', '', title.lower()).replace(' ', '-')


headings = {slug(title) for title in re.findall(r'^#+ (.+)$', guide, re.M)}
links = re.findall(r'\(#([a-z0-9-]+)\)', guide) + re.findall(r'href="#([a-z0-9-]+)"', guide)
broken = sorted(set(links) - headings)
assert not broken, f'docs/LAB.md links to headings it does not have: {broken}'
print(f'PASS Lab guide: names all {len(names)} parts, sensors, outputs, recipes and rows, shows {len(have)} pictures, and {len(links)} links land')
