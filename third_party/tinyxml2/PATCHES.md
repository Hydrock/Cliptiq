# Local alteration

Pinned upstream11.0.0/9148bdf. `upstream_files` records original SHA256, `files` records compiled files. One Identify branch extends PEDANTIC_WHITESPACE to retain whitespace before comments/CDATA/elements, rather than only an immediate closing tag. This is needed for Clipy shouldTrimWhitespace=false. Local regression covers space-only fields and comment/CDATA concatenation. License is unchanged; source alteration is marked. No entity/network feature added.

Header line endings normalized from CRLF to LF for reproducible Git checkout/CMake verification; semantic header content unchanged.
