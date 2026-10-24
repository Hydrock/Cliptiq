# T12 measurement evidence

Exact runtime 132adc53f3bf6ddd9cc009dc97151de84d888269; CI https://github.com/Hydrock/Cliptiq/actions/runs/40077744955. Four clean-source reports and eleven GREEN suite logs; source-verification.json checks hosted hashes against Git blobs and local hashes with explicit CRLF/LF normalization. Linux paired executable hashes equal hosted CI. Historical t02-size.json now includes a media section. It measures review host executables/one-file ZIPs, not packages/installations/updates.

Local logs include initial core linker RED and all three review behavior regressions RED→GREEN. The original core RED command did not link the existing core archive, so it only establishes that media symbols were absent; it is not a full isolated failing behavioral test. Native tests also caught the Windows normal-stop error, with its green local run retained. No native desktop acceptance is inferred from these logs.

Raw compiler flags/imported libraries/executable hashes are in each size report. Existing sanitizer limitations are retained. sha256.txt covers every evidence file except itself.
