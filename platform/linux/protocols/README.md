# Vendored protocol definitions

Only XML definitions and generated protocol tables are used; no compositor implementation is bundled. Versions, upstream URLs and SHA-256 are in pins.json, verified by CMake. Generated sources live in the build tree. Copyright notices and permissive license grants remain verbatim inside each XML, and must be retained when redistributing definitions or generated code.

ext-data-control-v1: wayland-protocols 1.45, staging/ext-data-control (Simon Ser and contributors as credited in XML). wlr-data-control-unstable-v1: swaywm/wlr-protocols commit b010a03648b88d143236de193bddbfea0c08bc84. ext is preferred; wlr is a compatibility fallback when ext is absent.
