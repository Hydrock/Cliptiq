#include "cliptiq/xml.hpp"
#include "db.hpp"
#include "tinyxml2.h"
#include <algorithm>
#include <cstring>
#include <set>
#include <string_view>
namespace cliptiq {
namespace {
using namespace detail;
using namespace tinyxml2;
constexpr std::size_t wire_limit = 64 * 1024 * 1024;
bool ws(char c) { return c == ' ' || c == '\t' || c == '\n' || c == '\r'; }
bool whitespace(const std::string &s) {
  return std::all_of(s.begin(), s.end(), ws);
}
bool scalar(unsigned c) {
  return c == 9 || c == 10 || c == 13 || (c >= 32 && c <= 0xd7ff) ||
         (c >= 0xe000 && c <= 0xfffd) || (c >= 0x10000 && c <= 0x10ffff);
}
void xml_text(const std::string &s) {
  for (std::size_t i = 0; i < s.size();) {
    unsigned c = static_cast<unsigned char>(s[i++]), n = 0, min = 0;
    if (c >= 128) {
      if (c >= 0xc2 && c <= 0xdf) {
        c &= 31;
        n = 1;
        min = 128;
      } else if (c >= 0xe0 && c <= 0xef) {
        c &= 15;
        n = 2;
        min = 2048;
      } else if (c >= 0xf0 && c <= 0xf4) {
        c &= 7;
        n = 3;
        min = 65536;
      } else
        invalid("Invalid XML UTF-8");
      if (i + n > s.size())
        invalid("Incomplete XML UTF-8");
      while (n--) {
        unsigned b = static_cast<unsigned char>(s[i++]);
        if ((b & 192) != 128)
          invalid("Invalid XML UTF-8 continuation");
        c = (c << 6) | (b & 63);
      }
      if (c < min)
        invalid("Overlong XML UTF-8");
    }
    if (!scalar(c))
      invalid("Forbidden XML1.0 character");
  }
}
void entities(const std::string &s, std::size_t a, std::size_t b) {
  for (auto i = a; i < b; ++i)
    if (s[i] == '&') {
      auto end = s.find(';', i + 1);
      if (end == std::string::npos || end >= b || end - i > 16)
        invalid("Invalid XML entity");
      auto name = s.substr(i + 1, end - i - 1);
      if (name != "amp" && name != "lt" && name != "gt" && name != "quot" &&
          name != "apos") {
        if (name.empty() || name[0] != '#')
          invalid("Unknown XML entity; DTD entities forbidden");
        unsigned value = 0, radix = 10;
        std::size_t n = 1;
        if (n < name.size() && (name[n] == 'x')) {
          radix = 16;
          ++n;
        }
        if (n == name.size())
          invalid("Empty numeric XML entity");
        for (; n < name.size(); ++n) {
          unsigned c = name[n], v = c >= '0' && c <= '9'   ? c - '0'
                                    : c >= 'a' && c <= 'f' ? c - 'a' + 10
                                    : c >= 'A' && c <= 'F' ? c - 'A' + 10
                                                           : 99;
          if (v >= radix || value > (0x10ffff - v) / radix)
            invalid("Invalid numeric XML entity");
          value = value * radix + v;
        }
        if (!scalar(value))
          invalid("Forbidden numeric XML character");
      }
      i = end;
    }
}
void declaration(const std::string &s) {
  std::size_t i = 0;
  if (s.compare(0, 3, "xml") != 0)
    invalid("Only XML declaration is allowed");
  i = 3;
  std::set<std::string> seen;
  while (i < s.size()) {
    if (!ws(s[i]))
      invalid("Invalid XML declaration");
    while (i < s.size() && ws(s[i]))
      ++i;
    if (i == s.size())
      break;
    auto a = i;
    while (i < s.size() && s[i] >= 'a' && s[i] <= 'z')
      ++i;
    auto name = s.substr(a, i - a);
    if (name.empty() || !seen.insert(name).second)
      invalid("Duplicate XML declaration field");
    while (i < s.size() && ws(s[i]))
      ++i;
    if (i == s.size() || s[i++] != '=')
      invalid("Invalid XML declaration assignment");
    while (i < s.size() && ws(s[i]))
      ++i;
    if (i == s.size() || (s[i] != '\'' && s[i] != '"'))
      invalid("Invalid XML declaration value");
    auto quote = s[i++];
    a = i;
    while (i < s.size() && s[i] != quote)
      ++i;
    if (i == s.size())
      invalid("Unterminated XML declaration");
    auto value = s.substr(a, i - a);
    ++i;
    if (name == "version") {
      if (seen.size() != 1 || value != "1.0")
        invalid("Only XML1.0 supported");
    } else if (name == "encoding") {
      for (auto &c : value)
        if (c >= 'a' && c <= 'z')
          c = char(c - 'a' + 'A');
      if (value != "UTF-8")
        invalid("Only UTF-8 XML supported");
    } else if (name == "standalone") {
      if (value != "yes" && value != "no")
        invalid("Invalid standalone declaration");
    } else
      invalid("Unknown XML declaration field");
  }
  if (!seen.count("version"))
    invalid("XML version required");
}
void preflight(const std::string &s) {
  xml_text(s);
  std::size_t depth = 0, nodes = 0, start = 0;
  if (s.compare(0, 3, "\xef\xbb\xbf") == 0)
    start = 3;
  for (auto i = start; i < s.size();) {
    if (s[i] != '<') {
      auto end = s.find('<', i);
      if (end == std::string::npos)
        end = s.size();
      if (std::string_view(s.data() + i, end - i).find("]]>") !=
          std::string_view::npos)
        invalid("CDATA terminator in plain text");
      entities(s, i, end);
      i = end;
      continue;
    }
    if (++nodes > 75000)
      throw Failure{{ErrorCode::TooLarge, "XML node budget exceeded"}};
    if (s.compare(i, 4, "<!--") == 0) {
      auto end = s.find("-->", i + 4);
      if (end == std::string::npos || s.find("--", i + 4) != end)
        invalid("Invalid XML comment");
      i = end + 3;
      continue;
    }
    if (s.compare(i, 9, "<![CDATA[") == 0) {
      auto end = s.find("]]>", i + 9);
      if (end == std::string::npos || depth == 0)
        invalid("Invalid CDATA");
      i = end + 3;
      continue;
    }
    if (s.compare(i, 2, "<!") == 0)
      invalid("DTD and entity declarations are forbidden");
    if (s.compare(i, 2, "<?") == 0) {
      auto end = s.find("?>", i + 2);
      if (i != start || end == std::string::npos)
        invalid("Only an initial XML declaration is allowed");
      declaration(s.substr(i + 2, end - i - 2));
      i = end + 2;
      continue;
    }
    auto end = i + 1;
    char quote = 0;
    for (; end < s.size(); ++end) {
      char c = s[end];
      if (quote) {
        if (c == quote)
          quote = 0;
      } else if (c == '\'' || c == '"')
        quote = c;
      else if (c == '>')
        break;
    }
    if (end == s.size())
      invalid("Unterminated XML tag");
    if (s[i + 1] == '/') {
      if (!depth)
        invalid("Unexpected closing XML tag");
      --depth;
    } else if (s[end - 1] != '/') {
      if (++depth > 5)
        throw Failure{{ErrorCode::TooLarge, "XML depth budget exceeded"}};
    }
    i = end + 1;
  }
  if (depth)
    invalid("Unclosed XML tag");
}
std::vector<const XMLElement *> children(const XMLNode &node) {
  std::vector<const XMLElement *> out;
  for (auto n = node.FirstChild(); n; n = n->NextSibling()) {
    if (auto e = n->ToElement()) {
      if (e->FirstAttribute())
        invalid("Clipy XML attributes unsupported");
      out.push_back(e);
    } else if (n->ToComment())
      continue;
    else if (auto t = n->ToText()) {
      if (!whitespace(t->Value()))
        invalid("Text outside Clipy XML field");
    } else
      invalid("Unexpected Clipy XML node");
  }
  return out;
}
std::string field(const XMLElement &e) {
  std::string out;
  for (auto n = e.FirstChild(); n; n = n->NextSibling()) {
    if (auto t = n->ToText())
      out += t->Value();
    else if (!n->ToComment())
      invalid("Nested XML field content");
  }
  xml_text(out);
  return out;
}
void limit(const std::string &s, std::size_t n) {
  if (s.size() > n)
    throw Failure{{ErrorCode::TooLarge, "Decoded XML field exceeds budget"}};
}
void add(std::string &out, const std::string &s) {
  if (s.size() > wire_limit - out.size())
    throw Failure{{ErrorCode::TooLarge, "Export XML exceeds64MiB budget"}};
  out += s;
}
void escaped(std::string &out, const std::string &s) {
  xml_text(s);
  for (char c : s) {
    switch (c) {
    case '&':
      add(out, "&amp;");
      break;
    case '<':
      add(out, "&lt;");
      break;
    case '>':
      add(out, "&gt;");
      break;
    case '"':
      add(out, "&quot;");
      break;
    case '\'':
      add(out, "&apos;");
      break;
    case '\r':
      add(out, "&#13;");
      break;
    default:
      if (out.size() == wire_limit)
        throw Failure{{ErrorCode::TooLarge, "Export XML exceeds budget"}};
      out += c;
    }
  }
}
} // namespace
Result<std::vector<ImportedFolder>> decode_clipy_xml(const Bytes &wire,
                                                     ImportLimits l) {
  return detail::guarded<std::vector<ImportedFolder>>(
      [&]() -> Result<std::vector<ImportedFolder>> {
        if (l.file_bytes > wire_limit || l.folders > 1000 ||
            l.snippets > 10000 || l.title_bytes > 1024 ||
            l.text_bytes > 1024 * 1024 || l.total_text_bytes > 16 * 1024 * 1024)
          invalid("XML limits exceed hard ceilings");
        if (wire.size() > l.file_bytes)
          return Error{ErrorCode::TooLarge, "XML file exceeds budget"};
        std::string raw(wire.begin(), wire.end());
        preflight(raw);
        // XML1.0 EOL normalization before TinyXML2: it otherwise also collapses
        // LF-CR.
        std::size_t written = 0;
        for (std::size_t i = 0; i < raw.size(); ++i) {
          char c = raw[i];
          if (c == '\r') {
            c = '\n';
            if (i + 1 < raw.size() && raw[i + 1] == '\n')
              ++i;
          }
          raw[written++] = c;
        }
        raw.resize(written);
        XMLDocument doc(true, PEDANTIC_WHITESPACE);
        if (doc.Parse(raw.data(), raw.size()) != XML_SUCCESS)
          return Error{ErrorCode::InvalidArgument,
                       std::string("Malformed XML: ") + doc.ErrorStr()};
        const XMLElement *root = nullptr;
        for (auto n = doc.FirstChild(); n; n = n->NextSibling()) {
          if (n->ToDeclaration() || n->ToComment())
            continue;
          if (n->ToText() && whitespace(n->Value()))
            continue;
          if (!n->ToElement() || root)
            invalid("One folders root required");
          root = n->ToElement();
        }
        if (!root || std::strcmp(root->Name(), "folders") ||
            root->FirstAttribute())
          invalid("Expected folders root without attributes");
        std::vector<ImportedFolder> out;
        std::size_t count = 0, total = 0;
        for (auto f : children(*root)) {
          if (std::strcmp(f->Name(), "folder"))
            invalid("Expected folder element");
          if (out.size() >= l.folders)
            return Error{ErrorCode::TooLarge, "XML folder count exceeded"};
          ImportedFolder folder;
          bool title = false, snippets = false;
          for (auto e : children(*f)) {
            if (!std::strcmp(e->Name(), "title") && !title) {
              title = true;
              folder.title = field(*e);
            } else if (!std::strcmp(e->Name(), "snippets") && !snippets) {
              snippets = true;
              for (auto sn : children(*e)) {
                if (std::strcmp(sn->Name(), "snippet"))
                  invalid("Expected snippet element");
                if (count++ >= l.snippets)
                  return Error{ErrorCode::TooLarge,
                               "XML snippet count exceeded"};
                ImportedSnippet item;
                bool st = false, sc = false;
                for (auto value : children(*sn)) {
                  if (!std::strcmp(value->Name(), "title") && !st) {
                    st = true;
                    item.title = field(*value);
                  } else if (!std::strcmp(value->Name(), "content") && !sc) {
                    sc = true;
                    item.content = field(*value);
                  } else
                    invalid("Unknown or duplicate snippet field");
                }
                if (item.title.empty())
                  item.title = "untitled snippet";
                limit(item.title, l.title_bytes);
                limit(item.content, l.text_bytes);
                if (item.content.size() > l.total_text_bytes - total)
                  return Error{ErrorCode::TooLarge,
                               "XML decoded text budget exceeded"};
                total += item.content.size();
                folder.snippets.push_back(std::move(item));
              }
            } else
              invalid("Unknown or duplicate folder field");
          }
          if (folder.title.empty())
            folder.title = "untitled folder";
          limit(folder.title, l.title_bytes);
          out.push_back(std::move(folder));
        }
        return out;
      });
}
Result<Bytes> encode_clipy_xml(Store &store) {
  return detail::guarded<Bytes>([&]() -> Result<Bytes> {
    auto db = store.connection();
    detail::exec(db, "BEGIN");
    struct ReadTx {
      sqlite3 *db;
      ~ReadTx() {
        if (db)
          sqlite3_exec(db, "ROLLBACK", nullptr, nullptr, nullptr);
      }
    } tx{db};
    auto fs = list_folders(store);
    if (!fs)
      return fs.error();
    auto hs = list_snippet_headers(store);
    if (!hs)
      return hs.error();
    std::set<std::string> ids;
    for (auto &f : fs.value())
      ids.insert(f.id);
    for (auto &h : hs.value())
      if (!ids.count(h.folder_id))
        return Error{
            ErrorCode::NotFound,
            "Move unfiled legacy snippets into a folder before export"};
    std::string out = "<?xml version=\"1.0\" encoding=\"UTF-8\"?>\n<folders>\n";
    std::size_t total = 0;
    for (auto &f : fs.value()) {
      add(out, "  <folder><title>");
      escaped(out, f.title);
      add(out, "</title><snippets>\n");
      for (auto &h : hs.value())
        if (h.folder_id == f.id) {
          auto s = get_snippet(store, h.id);
          if (!s)
            return s.error();
          if (s.value().content.size() > 16 * 1024 * 1024 - total)
            return Error{ErrorCode::TooLarge, "Export text budget exceeded"};
          total += s.value().content.size();
          add(out, "    <snippet><title>");
          escaped(out, s.value().title);
          add(out, "</title><content>");
          escaped(out, s.value().content);
          add(out, "</content></snippet>\n");
        }
      add(out, "  </snippets></folder>\n");
    }
    add(out, "</folders>\n");
    detail::exec(db, "COMMIT");
    tx.db = nullptr;
    return Bytes(out.begin(), out.end());
  });
}
} // namespace cliptiq
