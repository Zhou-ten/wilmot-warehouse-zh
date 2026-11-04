# Third-party notices

- MinHook v1.3.4, commit `c3fcafdc10146beb5919319d0683e44e3c30d537`:
  https://github.com/TsudaKageyu/minhook/tree/v1.3.4
  Vendored in `third_party/minhook`, with its complete BSD-style license
  (including Hacker Disassembler Engine notices) in `LICENSE.txt`.
- [LodePNG](https://github.com/lvandeve/lodepng) (zlib license), vendored in
  `third_party/lodepng` with its license and copyright notices.
- [Noto Sans SC](https://github.com/google/fonts/tree/main/ofl/notosanssc)
  (SIL Open Font License 1.1) supplies the added Chinese outlines and bitmap
  glyphs in `assets/wilmzh-font.wzh`. The complete license is in
  `third_party/noto-sans-sc/OFL.txt`. The recipe contains a derived glyph subset
  and instructions referencing the player's local font, not a standalone font.
- The developer-only font recipe generator uses fontTools and Pillow.
- Wilmot's Warehouse, its executables, runtime libraries, artwork, original
  text and fonts belong to their respective rights holders. This is an
  unofficial fan translation mod and grants no rights to the game.
- The English keys in the translation dictionary are matching strings needed
  by the mod. The MIT license covers our original code and original translation
  contributions only; it does not relicense the underlying game text or assets.
- The original DIN glyphs are loaded from the player's local copy at runtime.
  The recipe references the original glyph table by offsets and lengths. Keep
  generated merged fonts and atlases in the local cache and out of releases.
