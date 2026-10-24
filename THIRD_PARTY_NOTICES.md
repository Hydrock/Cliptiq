# Third-party notices

Cliptiq uses pinned SQLite (public domain), libgrapheme (ISC), and TinyXML2 (zlib); their source/license files remain under third_party/ or the pinned dependency provenance in cmake/. System AppKit/ImageIO/ServiceManagement, Win32/WIC and GTK/GdkPixbuf remain OS/runtime dependencies, not bundled replacements.

## Clipy translations

Exact matching translations in resources/locales/{de,fa,it,ja,pt-BR,uk,zh-Hans}.json derive from Clipy commit71b0e82623d5c73b4b717d81233eb78db8bbe258. Product references are adapted to Cliptiq. EN/RU additions are Cliptiq-authored. source-inventory.json records original xcstrings/lproj hashes, locale coverage and copied counts. Unmatched strings fall back to English. No Clipy donation destination is carried over.

The MIT License (MIT)

Copyright (c) 2015-2018 Clipy Project

Permission is hereby granted, free of charge, to any person obtaining a copy
of this software and associated documentation files (the "Software"), to deal
in the Software without restriction, including without limitation the rights
to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
copies of the Software, and to permit persons to whom the Software is
furnished to do so, subject to the following conditions:

The above copyright notice and this permission notice shall be included in all
copies or substantial portions of the Software.

THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
SOFTWARE.


## ClipMenu legacy translation provenance

The Clipy snippet lproj strings also inherit this notice:

The MIT License (MIT)

Copyright (c) 2008-2014 Naotaka Morimoto

Permission is hereby granted, free of charge, to any person obtaining a copy
of this software and associated documentation files (the "Software"), to deal
in the Software without restriction, including without limitation the rights
to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
copies of the Software, and to permit persons to whom the Software is
furnished to do so, subject to the following conditions:

The above copyright notice and this permission notice shall be included in
all copies or substantial portions of the Software.

THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN
THE SOFTWARE.

Icons are copyrighted by their respective authors.

T14 optional macOS migration helper uses RealmSwift10.54.6 and RealmCore14.14.0 (Apache-2.0), pinned in tools/import-clipy/Package.resolved. Their complete license and upstream third-party notices are in tools/import-clipy/licenses/ and accompany the separate migration package. Realm is not linked or bundled into the main Cliptiq application. SDK analytics is explicitly disabled in the offline helper.
