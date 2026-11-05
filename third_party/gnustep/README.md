# GNUstep

- `libobjc2/` is vendored, MIT licensed, at tag `v2.3`.
- `tools-make` is listed under GPL-3.0 in the inventory.
- `libs-base` and `libs-corebase` distinguish LGPL library code from GPL
  tools/tests. They must not be classified as uniformly GPL libraries.
  Exact versions and exceptions follow the pinned per-file notices and
  `COPYING.LIB`/`COPYING` files.
- These three projects are fetched only and are not linked into CIRRUS.
  Their pins are in `../manifest.tsv`.

The library/tool split is documented in the pinned upstream READMEs:
[Base](https://github.com/gnustep/libs-base/blob/818b4b8ddd1b97dd7269136425cbd63da9e73622/README.md)
and [CoreBase](https://github.com/gnustep/libs-corebase/blob/67a6f90601e7b143f173d1403f9a381a6d968fc2/README).
