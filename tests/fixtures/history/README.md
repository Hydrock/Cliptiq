# History fixtures

Unit tests use explicit byte literals for text, RTF, HTML and PNG signatures,
including NUL/non-UTF8 bytes and multiple item indices. These test storage
fidelity, not decoding; they are deliberately not real image-decoder fixtures.
SHA256 is checked against empty/abc/million-a standard vectors. Faults use real
SQLite triggers, query-only mode, max_page_count and process interruption.
No user clipboard content or personal data is used.
