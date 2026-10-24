#include "../LocaleNative.hpp"
#include "cliptiq/editor.hpp"
#ifdef CLIPTIQ_EDITOR_XML
#include "../SnippetXml.hpp"
#endif
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
// clang-format off
#include <windows.h>
#include <commctrl.h>
#ifdef CLIPTIQ_EDITOR_XML
#include <commdlg.h>
#endif
#include <windowsx.h>
#include <richedit.h>
#include <oleidl.h>
#include <shlobj.h>
// clang-format on
#include <algorithm>
#include <atomic>
#include <stdexcept>
namespace cliptiq {
namespace {
std::wstring wide(const std::string &s) {
  int n = MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, s.data(),
                              int(s.size()), nullptr, 0);
  std::wstring out(n, 0);
  if (n)
    MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, s.data(), int(s.size()),
                        out.data(), n);
  return out;
}
std::string text(HWND w, bool multiline = false) {
  int n = GetWindowTextLengthW(w);
  std::wstring raw(std::size_t(n) + 1, 0);
  GetWindowTextW(w, raw.data(), n + 1);
  int count = WideCharToMultiByte(CP_UTF8, WC_ERR_INVALID_CHARS, raw.data(), n,
                                  nullptr, 0, nullptr, nullptr);
  if (n && !count)
    throw std::runtime_error("Invalid Unicode in editor input");
  std::string out(count, 0);
  if (count)
    WideCharToMultiByte(CP_UTF8, WC_ERR_INVALID_CHARS, raw.data(), n,
                        out.data(), count, nullptr, nullptr);
  if (multiline) {
    std::string normalized;
    normalized.reserve(out.size());
    for (std::size_t i = 0; i < out.size(); ++i) {
      if (out[i] == '\r') {
        normalized += '\n';
        if (i + 1 < out.size() && out[i + 1] == '\n')
          ++i;
      } else
        normalized += out[i];
    }
    return normalized;
  }
  return out;
}
CLIPFORMAT move_format() {
  static auto id =
      CLIPFORMAT(RegisterClipboardFormatW(L"Cliptiq.SnippetMove.v1"));
  return id;
}
struct DragData final : IDataObject {
  std::atomic<ULONG> refs{1};
  std::string token;
  explicit DragData(std::uint64_t value) : token(std::to_string(value)) {}
  HRESULT STDMETHODCALLTYPE QueryInterface(REFIID id, void **out) override {
    if (!out)
      return E_POINTER;
    *out = nullptr;
    if (id == IID_IUnknown || id == IID_IDataObject) {
      *out = static_cast<IDataObject *>(this);
      AddRef();
      return S_OK;
    }
    return E_NOINTERFACE;
  }
  ULONG STDMETHODCALLTYPE AddRef() override { return ++refs; }
  ULONG STDMETHODCALLTYPE Release() override {
    auto n = --refs;
    if (!n)
      delete this;
    return n;
  }
  HRESULT STDMETHODCALLTYPE QueryGetData(FORMATETC *f) override {
    return f && f->cfFormat == move_format() && (f->tymed & TYMED_HGLOBAL) &&
                   f->dwAspect == DVASPECT_CONTENT && f->lindex == -1
               ? S_OK
               : DV_E_FORMATETC;
  }
  HRESULT STDMETHODCALLTYPE GetData(FORMATETC *f, STGMEDIUM *out) override {
    if (!out)
      return E_POINTER;
    *out = {};
    if (QueryGetData(f) != S_OK)
      return DV_E_FORMATETC;
    auto memory = GlobalAlloc(GMEM_MOVEABLE, token.size() + 1);
    if (!memory)
      return E_OUTOFMEMORY;
    auto bytes = GlobalLock(memory);
    if (!bytes) {
      GlobalFree(memory);
      return E_OUTOFMEMORY;
    }
    memcpy(bytes, token.c_str(), token.size() + 1);
    GlobalUnlock(memory);
    out->tymed = TYMED_HGLOBAL;
    out->hGlobal = memory;
    return S_OK;
  }
  HRESULT STDMETHODCALLTYPE GetDataHere(FORMATETC *, STGMEDIUM *) override {
    return E_NOTIMPL;
  }
  HRESULT STDMETHODCALLTYPE GetCanonicalFormatEtc(FORMATETC *,
                                                  FORMATETC *out) override {
    if (out)
      out->ptd = nullptr;
    return DATA_S_SAMEFORMATETC;
  }
  HRESULT STDMETHODCALLTYPE SetData(FORMATETC *, STGMEDIUM *, BOOL) override {
    return E_NOTIMPL;
  }
  HRESULT STDMETHODCALLTYPE EnumFormatEtc(DWORD direction,
                                          IEnumFORMATETC **out) override {
    if (direction != DATADIR_GET)
      return E_NOTIMPL;
    FORMATETC f{move_format(), nullptr, DVASPECT_CONTENT, -1, TYMED_HGLOBAL};
    return SHCreateStdEnumFmtEtc(1, &f, out);
  }
  HRESULT STDMETHODCALLTYPE DAdvise(FORMATETC *, DWORD, IAdviseSink *,
                                    DWORD *) override {
    return OLE_E_ADVISENOTSUPPORTED;
  }
  HRESULT STDMETHODCALLTYPE DUnadvise(DWORD) override {
    return OLE_E_ADVISENOTSUPPORTED;
  }
  HRESULT STDMETHODCALLTYPE EnumDAdvise(IEnumSTATDATA **) override {
    return OLE_E_ADVISENOTSUPPORTED;
  }
};
struct DragSource final : IDropSource {
  std::atomic<ULONG> refs{1};
  HRESULT STDMETHODCALLTYPE QueryInterface(REFIID id, void **out) override {
    if (!out)
      return E_POINTER;
    *out = nullptr;
    if (id == IID_IUnknown || id == IID_IDropSource) {
      *out = static_cast<IDropSource *>(this);
      AddRef();
      return S_OK;
    }
    return E_NOINTERFACE;
  }
  ULONG STDMETHODCALLTYPE AddRef() override { return ++refs; }
  ULONG STDMETHODCALLTYPE Release() override {
    auto n = --refs;
    if (!n)
      delete this;
    return n;
  }
  HRESULT STDMETHODCALLTYPE QueryContinueDrag(BOOL escape,
                                              DWORD keys) override {
    return escape                 ? DRAGDROP_S_CANCEL
           : !(keys & MK_LBUTTON) ? DRAGDROP_S_DROP
                                  : S_OK;
  }
  HRESULT STDMETHODCALLTYPE GiveFeedback(DWORD) override {
    return DRAGDROP_S_USEDEFAULTCURSORS;
  }
};
} // namespace
struct NativeSnippetEditor::State : std::enable_shared_from_this<State> {
  DWORD owner = GetCurrentThreadId();
  HWND window = nullptr, tree = nullptr, title = nullptr, body = nullptr,
       enabled = nullptr, shortcut = nullptr, bind = nullptr, message = nullptr,
       undo = nullptr, redo = nullptr, addSnippet = nullptr;
  HMODULE rich = nullptr;
  HRESULT ole = E_FAIL;
  std::shared_ptr<SnippetEditorSession> model;
#ifdef CLIPTIQ_EDITOR_XML
  SnippetXmlTransfer xml;
#endif
  bool updating = false;
  std::map<HTREEITEM, SnippetItemId> nodes;
  std::uint64_t drag = 0;
  std::optional<SnippetItemId> dragged;
  IDataObject *activeData = nullptr;
  struct DropTarget final : IDropTarget {
    std::atomic<ULONG> refs{1};
    std::weak_ptr<State> state;
    explicit DropTarget(std::weak_ptr<State> s) : state(std::move(s)) {}
    HRESULT STDMETHODCALLTYPE QueryInterface(REFIID id, void **out) override {
      if (!out)
        return E_POINTER;
      *out = nullptr;
      if (id == IID_IUnknown || id == IID_IDropTarget) {
        *out = static_cast<IDropTarget *>(this);
        AddRef();
        return S_OK;
      }
      return E_NOINTERFACE;
    }
    ULONG STDMETHODCALLTYPE AddRef() override { return ++refs; }
    ULONG STDMETHODCALLTYPE Release() override {
      auto n = --refs;
      if (!n)
        delete this;
      return n;
    }
    bool accepted = false;
    HRESULT STDMETHODCALLTYPE DragEnter(IDataObject *data, DWORD, POINTL point,
                                        DWORD *effect) override {
      auto s = state.lock();
      accepted = s && data == s->activeData && s->drag;
      return DragOver(0, point, effect);
    }
    HRESULT STDMETHODCALLTYPE DragOver(DWORD, POINTL point,
                                       DWORD *effect) override {
      if (!effect)
        return E_POINTER;
      auto s = state.lock();
      *effect = DROPEFFECT_NONE;
      if (!s || !accepted)
        return S_OK;
      auto where = s->location(point);
      if (where) {
        *effect = DROPEFFECT_MOVE;
        POINT p{point.x, point.y};
        ScreenToClient(s->tree, &p);
        RECT area{};
        GetClientRect(s->tree, &area);
        if (p.y < 16)
          SendMessageW(s->tree, WM_VSCROLL, SB_LINEUP, 0);
        else if (p.y > area.bottom - 16)
          SendMessageW(s->tree, WM_VSCROLL, SB_LINEDOWN, 0);
      }
      return S_OK;
    }
    HRESULT STDMETHODCALLTYPE DragLeave() override {
      accepted = false;
      if (auto s = state.lock()) {
        TreeView_SetInsertMark(s->tree, nullptr, FALSE);
        TreeView_SelectDropTarget(s->tree, nullptr);
      }
      return S_OK;
    }
    HRESULT STDMETHODCALLTYPE Drop(IDataObject *data, DWORD, POINTL point,
                                   DWORD *effect) override {
      if (!effect)
        return E_POINTER;
      *effect = DROPEFFECT_NONE;
      auto s = state.lock();
      if (!s || !accepted || data != s->activeData)
        return S_OK;
      auto where = s->location(point);
      if (!where)
        return S_OK;
      FORMATETC f{move_format(), nullptr, DVASPECT_CONTENT, -1, TYMED_HGLOBAL};
      STGMEDIUM memory{};
      if (FAILED(data->GetData(&f, &memory)))
        return S_OK;
      bool valid = false;
      if (memory.tymed == TYMED_HGLOBAL && GlobalSize(memory.hGlobal) <= 32) {
        auto bytes = static_cast<const char *>(GlobalLock(memory.hGlobal));
        auto expected = std::to_string(s->drag);
        if (bytes && GlobalSize(memory.hGlobal) >= expected.size() + 1)
          valid = memcmp(bytes, expected.c_str(), expected.size() + 1) == 0;
        if (bytes)
          GlobalUnlock(memory.hGlobal);
      }
      ReleaseStgMedium(&memory);
      if (!valid)
        return S_OK;
      auto r =
          s->model->finish_drag(s->drag, where->first, where->second, true);
      s->drag = 0;
      s->dragged.reset();
      s->status(r);
      if (r) {
        *effect = DROPEFFECT_MOVE;
        s->rebuild();
      }
      DragLeave();
      return S_OK;
    }
  } *target = nullptr;
  void close() {
    if (tree && target)
      RevokeDragDrop(tree);
    if (target) {
      target->Release();
      target = nullptr;
    }
    if (tree) {
      auto images = TreeView_GetImageList(tree, TVSIL_STATE);
      TreeView_SetImageList(tree, nullptr, TVSIL_STATE);
      if (images)
        ImageList_Destroy(images);
    }
    if (window)
      DestroyWindow(window);
    window = tree = nullptr;
  }
  ~State() {
    close();
    if (rich)
      FreeLibrary(rich);
    if (SUCCEEDED(ole))
      OleUninitialize();
  }
  bool transfer_locked() const {
#ifdef CLIPTIQ_EDITOR_XML
    return xml.busy();
#else
    return false;
#endif
  }
  void status(Result<void> r) {
    if (message)
      SetWindowTextW(
          message,
          r ? wide((model->saving()
                        ? std::string(lc("Saving locally..."))
                        : lc("Saved locally. ") + model->binding_status()))
                  .c_str()
            : wide(r.error().message).c_str());
  }
#ifdef CLIPTIQ_EDITOR_XML
  void xml_sensitive(bool value) { EnableWindow(window, value); }
  Result<void> start_xml(const std::string &path, bool importing) {
    if (!window || !model)
      return Error{ErrorCode::InvalidArgument, "Open the snippet editor first"};
    auto result = xml.start(*model, path, importing);
    if (result) {
      xml_sensitive(false);
      status(Error{ErrorCode::Busy, importing ? lc("Importing XML...")
                                              : lc("Exporting XML...")});
    }
    return result;
  }
  bool poll_xml() {
    if (!xml.busy())
      return false;
    auto result = xml.poll(*model);
    if (xml.busy())
      return true;
    xml_sensitive(true);
    if (result) {
      if (xml.importing())
        rebuild();
      else
        detail();
      SetWindowTextW(message, wide(xml.message()).c_str());
    } else {
      detail();
      status(result.error());
    }
    return true;
  }
  void xml_picker(bool importing) {
    auto keep = shared_from_this();
    if (xml.busy() || model->has_unsaved()) {
      status(Error{
          ErrorCode::Busy,
          "Save or Reload the draft and finish the current transfer first"});
      return;
    }
    std::vector<wchar_t> path(32768);
    if (!importing)
      wcscpy_s(path.data(), path.size(), L"snippets.xml");
    OPENFILENAMEW picker{};
    picker.lStructSize = sizeof(picker);
    picker.hwndOwner = window;
    picker.lpstrFile = path.data();
    picker.nMaxFile = DWORD(path.size());
    picker.lpstrFilter = L"XML files (*.xml)\0*.xml\0All files (*.*)\0*.*\0\0";
    picker.lpstrDefExt = L"xml";
    picker.Flags = OFN_EXPLORER | OFN_NOCHANGEDIR | OFN_PATHMUSTEXIST |
                   (importing ? OFN_FILEMUSTEXIST : OFN_OVERWRITEPROMPT);
    auto accepted =
        importing ? GetOpenFileNameW(&picker) : GetSaveFileNameW(&picker);
    if (accepted) {
      int count =
          WideCharToMultiByte(CP_UTF8, WC_ERR_INVALID_CHARS, path.data(), -1,
                              nullptr, 0, nullptr, nullptr);
      if (!count) {
        status(Error{ErrorCode::InvalidArgument, "Invalid Unicode XML path"});
        return;
      }
      std::string chosen(std::size_t(count), 0);
      WideCharToMultiByte(CP_UTF8, WC_ERR_INVALID_CHARS, path.data(), -1,
                          chosen.data(), count, nullptr, nullptr);
      chosen.resize(std::size_t(count) - 1);
      status(start_xml(chosen, importing));
    } else if (CommDlgExtendedError())
      status(Error{ErrorCode::Io, "Cannot show XML file picker"});
  }
#endif
  void poll() {
    if(!model || !window) return;
#ifdef CLIPTIQ_EDITOR_XML
    if (poll_xml())
      return;
#endif
    auto result=model->poll_saves();
    if(!result) status(result.error());
    else if(result.value()) {status({});selected_label();}
    history();
  }
  void history() {
    EnableWindow(undo, model->can_undo());
    EnableWindow(redo, model->can_redo());
  }
  bool confirm_close() {
#ifdef CLIPTIQ_EDITOR_XML
    if (xml.busy()) {
      status(
          Error{ErrorCode::Busy, "XML transfer pending; wait before closing"});
      return false;
    }
#endif
    if(model && model->saving()) {status(Error{ErrorCode::Busy,"Save pending; wait before closing"});return false;}

    if (!model || !model->has_unsaved())
      return true;
    if (MessageBoxW(
            window, L"Discard the unsaved draft? Choose No to keep editing.",
            L"Cliptiq", MB_YESNO | MB_ICONWARNING | MB_DEFBUTTON2) != IDYES)
      return false;
    auto loaded = model->reload();
    status(loaded);
    if (loaded)
      rebuild();
    return bool(loaded);
  }
  void detail() {
    if (transfer_locked())
      return;
    updating = true;
    auto selected = model->selected();
    bool folder = selected && selected->kind == SnippetItemKind::Folder;
    EnableWindow(title, bool(selected));
    EnableWindow(enabled, bool(selected));
    SetWindowTextW(title, wide(model->title()).c_str());
    SendMessageW(enabled, BM_SETCHECK,
                 model->enabled() ? BST_CHECKED : BST_UNCHECKED, 0);
    EnableWindow(body, selected && !folder);
    SetWindowTextW(body, wide(model->content()).c_str());
    SendMessageW(body, EM_EMPTYUNDOBUFFER, 0, 0);
    EnableWindow(shortcut, folder);
    EnableWindow(bind, folder);
    auto chord = model->folder_chord();
    SetWindowTextW(shortcut, chord ? wide(format_chord(*chord)).c_str() : L"");
    EnableWindow(addSnippet, bool(selected));
    history();
    updating = false;
  }
  HTREEITEM insert(HTREEITEM parent, const SnippetItemId &id,
                   const std::string &label, bool enabled) {
    auto text = wide(label.empty() ? "Untitled" : label);
    TVINSERTSTRUCTW data{};
    data.hParent = parent;
    data.hInsertAfter = TVI_LAST;
    data.item.mask = TVIF_TEXT | TVIF_STATE;
    data.item.pszText = text.data();
    data.item.stateMask = TVIS_STATEIMAGEMASK;
    data.item.state = INDEXTOSTATEIMAGEMASK(enabled ? 2 : 1);
    auto handle = reinterpret_cast<HTREEITEM>(SendMessageW(
        tree, TVM_INSERTITEMW, 0, reinterpret_cast<LPARAM>(&data)));
    if (handle)
      nodes[handle] = id;
    return handle;
  }
  void rebuild() {
    updating = true;
    TreeView_DeleteAllItems(tree);
    nodes.clear();
    std::map<std::string, HTREEITEM> parents;
    for (auto &f : model->folders())
      parents[f.id] =
          insert(TVI_ROOT, {SnippetItemKind::Folder, f.id}, f.title, f.enabled);
    for (auto &s : model->snippets()) {
      auto parent = parents.find(s.folder_id);
      if (parent == parents.end()) {
        parents[s.folder_id] =
            insert(TVI_ROOT, {SnippetItemKind::Folder, ""},
                   "Unfiled legacy items: move to a folder", false);
        parent = parents.find(s.folder_id);
      }
      insert(parent->second, {SnippetItemKind::Snippet, s.id}, s.title,
             s.enabled);
    }
    for (auto &p : parents)
      TreeView_Expand(tree, p.second, TVE_EXPAND);
    auto selected = model->selected();
    if (selected)
      for (auto &n : nodes)
        if (n.second.kind == selected->kind &&
            n.second.value == selected->value) {
          TreeView_SelectItem(tree, n.first);
          TreeView_EnsureVisible(tree, n.first);
          break;
        }
    updating = false;
    detail();
  }
  void selected_label() {
    auto selected = TreeView_GetSelection(tree);
    if (selected) {
      auto text = wide(model->title().empty() ? "Untitled" : model->title());
      TVITEMW item{};
      item.hItem = selected;
      item.mask = TVIF_TEXT | TVIF_STATE;
      item.pszText = text.data();
      item.stateMask = TVIS_STATEIMAGEMASK;
      item.state = INDEXTOSTATEIMAGEMASK(model->enabled() ? 2 : 1);
      updating = true;
      SendMessageW(tree, TVM_SETITEMW, 0, reinterpret_cast<LPARAM>(&item));
      updating = false;
    }
    history();
  }
  void restore_selection() {
    auto selected=model->selected();HTREEITEM item=nullptr;
    if(selected) for(auto& pair:nodes)
      if(pair.second.kind==selected->kind && pair.second.value==selected->value) {item=pair.first;break;}
    updating=true;TreeView_SelectItem(tree,item);updating=false;
  }
  void perform(int command) {
    if (transfer_locked()) {
      status(Error{ErrorCode::Busy, "XML transfer pending"});
      return;
    }
    auto keep = shared_from_this();
#ifdef CLIPTIQ_EDITOR_XML
    if (command == 513 || command == 514) {
      xml_picker(command == 513);
      return;
    }
#endif
    Result<void> r;
    if (command == 507 || command == 508) {
      auto added = command == 507 ? model->add_folder() : model->add_snippet();
      if (!added)
        r = added.error();
      else {
        rebuild();
        SetFocus(command == 507 ? title : body);
      }
    } else if (command == 509) {
      bool yes =
          MessageBoxW(
              window,
              lw(L"Delete selected item? A folder also deletes its snippets."),
              L"Cliptiq", MB_YESNO | MB_ICONQUESTION | MB_DEFBUTTON2) == IDYES;
      r = model->erase_selected(yes);
      if (r && yes)
        rebuild();
    } else if (command == 510 || command == 511) {
      r = command == 510 ? model->undo() : model->redo();
      if (r)
        detail();
    } else if (command == 512) {
      r = model->reload();
      if (r)
        rebuild();
    } else if (command == 503) {
      r = model->enable(SendMessageW(enabled, BM_GETCHECK, 0, 0) ==
                        BST_CHECKED);
      if (r)
        selected_label();
      else
        SendMessageW(enabled, BM_SETCHECK,
                     model->enabled() ? BST_CHECKED : BST_UNCHECKED, 0);
    } else if (command == 506) {
      auto value = text(shortcut);
      std::optional<KeyChord> chord;
      if (!value.empty()) {
        auto c = parse_chord(value);
        if (!c) {
          status(c.error());
          return;
        }
        chord = c.value();
      }
      r = model->bind_folder(chord);
    }
    status(r);
  }
  void layout() {
    if (!tree)
      return;
    RECT area{};
    GetClientRect(window, &area);
    int width = int(area.right), height = int(area.bottom),
        left = std::min(300, width / 3);
    MoveWindow(tree, 12, 58, left - 18, height - 120, TRUE);
    int x = left + 6, right = width - x - 12;
    MoveWindow(title, x, 58, right, 26, TRUE);
    MoveWindow(enabled, x, 92, right, 24, TRUE);
    MoveWindow(shortcut, x, 126, right - 124, 26, TRUE);
    MoveWindow(bind, width - 124, 126, 112, 26, TRUE);
    MoveWindow(body, x, 164, right, height - 226, TRUE);
    MoveWindow(message, 12, height - 54, width - 24, 44, TRUE);
  }
  std::optional<std::pair<FolderId, std::size_t>> location(POINTL point) {
    if (transfer_locked())
      return {};
    if (!drag || !dragged)
      return {};
    POINT p{point.x, point.y};
    ScreenToClient(tree, &p);
    TVHITTESTINFO hit{};
    hit.pt = p;
    auto node = TreeView_HitTest(tree, &hit);
    if (!node) {
      if (dragged->kind == SnippetItemKind::Folder)
        return std::make_pair(FolderId{}, snippet_append);
      return {};
    }
    auto found = nodes.find(node);
    if (found == nodes.end() || found->second.value.empty())
      return {};
    auto item = found->second;
    RECT rect{};
    TreeView_GetItemRect(tree, node, &rect, TRUE);
    bool after = p.y > (rect.top + rect.bottom) / 2;
    FolderId folder;
    std::size_t index = 0;
    if (dragged->kind == SnippetItemKind::Folder) {
      auto destination = item.value;
      if (item.kind == SnippetItemKind::Snippet)
        for (auto &h : model->snippets())
          if (h.id == item.value)
            destination = h.folder_id;
      std::size_t source = 0;
      for (std::size_t n = 0; n < model->folders().size(); ++n) {
        if (model->folders()[n].id == destination)
          index = n;
        if (model->folders()[n].id == dragged->value)
          source = n;
      }
      if (after)
        ++index;
      if (source < index)
        --index;
      TreeView_SetInsertMark(tree, node, after);
    } else if (item.kind == SnippetItemKind::Folder) {
      folder = item.value;
      index = snippet_append;
      TreeView_SelectDropTarget(tree, node);
    } else {
      for (auto &h : model->snippets())
        if (h.id == item.value)
          folder = h.folder_id;
      std::size_t source = snippet_append;
      for (auto &h : model->snippets())
        if (h.folder_id == folder) {
          if (h.id == dragged->value)
            source = index;
          if (h.id == item.value)
            break;
          ++index;
        }
      if (after)
        ++index;
      if (source < index)
        --index;
      TreeView_SetInsertMark(tree, node, after);
    }
    return std::make_pair(folder, index);
  }
  void begin(HTREEITEM node) {
    if (transfer_locked())
      return;
    auto keep = shared_from_this();
    auto item = nodes.find(node);
    if (!target || item == nodes.end() || item->second.value.empty())
      return;
    dragged = item->second;
    auto started = model->begin_drag(*dragged);
    if (!started) {
      status(started.error());
      return;
    }
    drag = started.value();
    auto data = new DragData(drag);
    auto source = new DragSource;
    activeData = data;
    DWORD effect = 0;
    DoDragDrop(data, source, DROPEFFECT_MOVE, &effect);
    activeData = nullptr;
    source->Release();
    data->Release();
    if (drag)
      model->finish_drag(drag, "", 0, false);
    drag = 0;
    dragged.reset();
    TreeView_SetInsertMark(tree, nullptr, FALSE);
    TreeView_SelectDropTarget(tree, nullptr);
  }
  static LRESULT CALLBACK edit_proc(HWND w, UINT msg, WPARAM key, LPARAM lp,
                                    UINT_PTR, DWORD_PTR data) {
    auto s = reinterpret_cast<State *>(data);
    if (msg == WM_KEYDOWN || msg == WM_SYSKEYDOWN) {
      bool control = (GetKeyState(VK_CONTROL) & 0x8000) != 0;
      if (w == s->body && control && (key == 'Z' || key == 'Y')) {
        s->perform(key == 'Y' || (GetKeyState(VK_SHIFT) & 0x8000) ? 511 : 510);
        return 0;
      }
      if (w == s->shortcut) {
        unsigned mods =
            ((GetKeyState(VK_CONTROL) & 0x8000) ? 1u : 0u) |
            ((GetKeyState(VK_SHIFT) & 0x8000) ? 2u : 0u) |
            ((GetKeyState(VK_MENU) & 0x8000) ? 4u : 0u) |
            (((GetKeyState(VK_LWIN) | GetKeyState(VK_RWIN)) & 0x8000) ? 8u
                                                                      : 0u);
        if (mods) {
          std::string name;
          if ((key >= 'A' && key <= 'Z') || (key >= '0' && key <= '9'))
            name = char(key);
          else if (key >= VK_F1 && key <= VK_F11)
            name = "F" + std::to_string(key - VK_F1 + 1);
          if (!name.empty()) {
            auto chord = format_chord({mods, name});
            if (parse_chord(chord)) {
              SetWindowTextW(w, wide(chord).c_str());
              return 0;
            }
          }
        }
      }
      if (w == s->tree) {
        if (key == VK_SPACE) {
          auto selected = TreeView_GetSelection(s->tree);
          if (selected) {
            s->updating = true;
            TreeView_SetCheckState(s->tree, selected,
                                   !TreeView_GetCheckState(s->tree, selected));
            s->updating = false;
            SendMessageW(s->window, WM_APP + 1, 0,
                         reinterpret_cast<LPARAM>(selected));
          }
          return 0;
        }
        if (key == VK_DELETE) {
          s->perform(509);
          return 0;
        }
        if (key == VK_F2) {
          SetFocus(s->title);
          return 0;
        }
        if (control && key == 'N') {
          s->perform(GetKeyState(VK_SHIFT) & 0x8000 ? 507 : 508);
          return 0;
        }
      }
    }
    if (w == s->body && msg == WM_CHAR && (GetKeyState(VK_CONTROL) & 0x8000) &&
        (key == 26 || key == 25))
      return 0;
    if (w == s->shortcut &&
        (msg == WM_SYSCHAR ||
         (msg == WM_CHAR && (GetKeyState(VK_CONTROL) & 0x8000))))
      return 0;
    return DefSubclassProc(w, msg, key, lp);
  }
  static LRESULT CALLBACK proc(HWND w, UINT msg, WPARAM wp, LPARAM lp) {
    auto s = reinterpret_cast<State *>(GetWindowLongPtrW(w, GWLP_USERDATA));
    if (msg == WM_NCCREATE) {
      s = static_cast<State *>(
          reinterpret_cast<CREATESTRUCTW *>(lp)->lpCreateParams);
      SetWindowLongPtrW(w, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(s));
    }
    if (!s)
      return DefWindowProcW(w, msg, wp, lp);
    try {
      if (msg == WM_SIZE) {
        s->layout();
        return 0;
      }
      if (msg == WM_GETMINMAXINFO) {
        auto info = reinterpret_cast<MINMAXINFO *>(lp);
        info->ptMinTrackSize = {700, 420};
        return 0;
      }
      if (msg == WM_CLOSE) {
        if (s->confirm_close())
          ShowWindow(w, SW_HIDE);
        return 0;
      }
      if (msg == WM_COMMAND || msg == WM_NOTIFY || msg == WM_APP + 1) {
        auto keep = s->weak_from_this().lock();
        if (!keep)
          return DefWindowProcW(w, msg, wp, lp);
        if (s->updating)
          return 0;
        if (s->transfer_locked()) {
          if (msg == WM_NOTIFY &&
              reinterpret_cast<NMHDR *>(lp)->code == TVN_SELCHANGINGW)
            return TRUE;
          return 0;
        }
        if (msg == WM_COMMAND) {
          int id = LOWORD(wp);
          if ((id == 502 || id == 504) && HIWORD(wp) == EN_CHANGE) {
            auto r = id == 502 ? s->model->queue_title(text(s->title))
                               : s->model->queue_content(text(s->body, true));
            s->status(r);
            if (r)
              s->selected_label();
            s->history();
            return 0;
          }
          if (id >= 503 && id <= 514 && id != 504 && id != 505) {
            s->perform(id);
            return 0;
          }
        }
        if (msg == WM_NOTIFY) {
          auto header = reinterpret_cast<NMHDR *>(lp);
          if (header->hwndFrom == s->tree) {
            if (header->code == TVN_SELCHANGINGW && s->model->has_unsaved()) {
              s->status(Error{
                  ErrorCode::Busy,
                  "Unsaved draft: correct it or Reload before navigating"});
              return TRUE;
            }
            if (header->code == TVN_SELCHANGEDW) {
              auto info = reinterpret_cast<NMTREEVIEWW *>(lp);
              auto found = s->nodes.find(info->itemNew.hItem);
              std::optional<SnippetItemId> item;
              if (found != s->nodes.end() && !found->second.value.empty())
                item = found->second;
              auto r = s->model->select(item);
              s->status(r);
              if (r)
                s->detail();
              else s->restore_selection();
              return 0;
            }
            if (header->code == TVN_BEGINDRAGW) {
              s->begin(reinterpret_cast<NMTREEVIEWW *>(lp)->itemNew.hItem);
              return 0;
            }
            if (header->code == NM_CLICK) {
              auto packed = GetMessagePos();
              POINT point{GET_X_LPARAM(packed), GET_Y_LPARAM(packed)};
              ScreenToClient(s->tree, &point);
              TVHITTESTINFO hit{};
              hit.pt = point;
              auto node = TreeView_HitTest(s->tree, &hit);
              if (node && (hit.flags & TVHT_ONITEMSTATEICON))
                PostMessageW(w, WM_APP + 1, 0, reinterpret_cast<LPARAM>(node));
            }
          }
        }
        if (msg == WM_APP + 1) {
          auto node = reinterpret_cast<HTREEITEM>(lp);
          auto found = s->nodes.find(node);
          if (found != s->nodes.end() && !found->second.value.empty()) {
            auto r = s->model->select(found->second);
            if (r)
              r = s->model->enable(TreeView_GetCheckState(s->tree, node) != 0);
            s->status(r);
            if (r) {
              s->updating = true;
              TreeView_SelectItem(s->tree, node);
              s->updating = false;
              s->detail();
            } else {
              bool active = false;
              auto item = found->second;
              if (item.kind == SnippetItemKind::Folder) {
                for (auto &f : s->model->folders())
                  if (f.id == item.value)
                    active = f.enabled;
              } else {
                for (auto &h : s->model->snippets())
                  if (h.id == item.value)
                    active = h.enabled;
              }
              s->updating = true;
              TreeView_SetCheckState(s->tree, node, active);
              s->updating = false;
            }
          }
          return 0;
        }
      }
    } catch (const std::exception &) {
      if (msg == WM_COMMAND && HIWORD(wp) == EN_CHANGE) {
        if (LOWORD(wp) == 502)
          s->model->queue_title(std::string("\xc0\x80", 2));
        if (LOWORD(wp) == 504)
          s->model->queue_content(std::string("\xc0\x80", 2));
      }
      s->status(Error{ErrorCode::Internal,
                      "Editor operation failed; reload to recover"});
      return 0;
    }
    return DefWindowProcW(w, msg, wp, lp);
  }
};
NativeSnippetEditor::NativeSnippetEditor()
    : state_(std::make_shared<State>()) {}
NativeSnippetEditor::~NativeSnippetEditor() = default;
Result<void>
NativeSnippetEditor::show(std::shared_ptr<SnippetEditorSession> model) {
  auto s = state_;
  if (GetCurrentThreadId() != s->owner)
    return Error{ErrorCode::InvalidArgument, "Editor requires owner thread"};
#ifdef CLIPTIQ_EDITOR_XML
  if (s->xml.busy())
    return Error{ErrorCode::Busy, "XML transfer pending in existing editor"};
#endif
  if (!model)
    return Error{ErrorCode::InvalidArgument, "No editor session"};
  if (s->window && s->model && s->model->has_unsaved())
    return Error{ErrorCode::Busy, "Unsaved draft retained in existing editor"};
  s->close();
  s->model = std::move(model);
  auto background=s->model->background_saves();if(!background) return background;
  if (!s->rich)
    s->rich =
        LoadLibraryExW(L"Msftedit.dll", nullptr, LOAD_LIBRARY_SEARCH_SYSTEM32);
  if (!s->rich)
    return Error{ErrorCode::Unsupported,
                 "System plain-text control unavailable"};
  if (s->ole == E_FAIL)
    s->ole = OleInitialize(nullptr);
  INITCOMMONCONTROLSEX init{sizeof(init), ICC_TREEVIEW_CLASSES};
  InitCommonControlsEx(&init);
  WNDCLASSW wc{};
  wc.hInstance = GetModuleHandleW(nullptr);
  wc.lpfnWndProc = State::proc;
  wc.lpszClassName = L"Cliptiq.SnippetEditor.v1";
  wc.hCursor = LoadCursor(nullptr, IDC_ARROW);
  wc.hbrBackground = reinterpret_cast<HBRUSH>(COLOR_WINDOW + 1);
  RegisterClassW(&wc);
  s->window = CreateWindowExW(WS_EX_CONTROLPARENT, wc.lpszClassName,
                              lw(L"Cliptiq Snippets"), WS_OVERLAPPEDWINDOW,
                              CW_USEDEFAULT, CW_USEDEFAULT, 920, 700, nullptr,
                              nullptr, wc.hInstance, s.get());
  if (!s->window)
    return Error{ErrorCode::Io, "Cannot create editor window"};
  auto font = GetStockObject(DEFAULT_GUI_FONT);
  auto control = [&](const wchar_t *type, const wchar_t *text, int id,
                     DWORD style, DWORD extra = 0) -> HWND {
    auto w = CreateWindowExW(
        extra, type, text, WS_CHILD | WS_VISIBLE | WS_TABSTOP | style, 0, 0,
        100, 26, s->window, reinterpret_cast<HMENU>(std::uintptr_t(id)),
        wc.hInstance, nullptr);
    SendMessageW(w, WM_SETFONT, reinterpret_cast<WPARAM>(font), TRUE);
    return w;
  };
  const wchar_t *names[] = {lw(L"Add Folder"),
                            lw(L"Add Snippet"),
                            lw(L"Delete…"),
                            lw(L"Undo"),
                            lw(L"Redo"),
                            lw(L"Reload")
#ifdef CLIPTIQ_EDITOR_XML
                                ,
                            lw(L"Import XML…"),
                            lw(L"Export XML…")
#endif
  };
  for (int n = 0; n < int(sizeof(names) / sizeof(names[0])); ++n) {
    auto button = control(L"BUTTON", names[n], 507 + n, BS_PUSHBUTTON);
    MoveWindow(button, 12 + n * 84, 12, 80, 30, TRUE);
    if (n == 1)
      s->addSnippet = button;
    if (n == 3)
      s->undo = button;
    if (n == 4)
      s->redo = button;
  }
  s->tree = control(WC_TREEVIEWW, L"", 501,
                    TVS_HASBUTTONS | TVS_HASLINES | TVS_LINESATROOT |
                        TVS_SHOWSELALWAYS | TVS_CHECKBOXES,
                    WS_EX_CLIENTEDGE);
  s->title = control(L"EDIT", L"", 502, ES_AUTOHSCROLL, WS_EX_CLIENTEDGE);
  SendMessageW(s->title, EM_SETLIMITTEXT, 4096, 0);
  s->enabled = control(L"BUTTON", lw(L"Enabled in menu"), 503, BS_AUTOCHECKBOX);
  s->shortcut = control(L"EDIT", L"", 505, ES_AUTOHSCROLL, WS_EX_CLIENTEDGE);
  SendMessageW(s->shortcut, EM_SETLIMITTEXT, 128, 0);
  s->bind = control(L"BUTTON", L"Bind / Clear", 506, BS_PUSHBUTTON);
  s->body = control(MSFTEDIT_CLASS, L"", 504,
                    ES_MULTILINE | ES_AUTOVSCROLL | ES_WANTRETURN | WS_VSCROLL,
                    WS_EX_CLIENTEDGE);
  if (!s->tree || !s->title || !s->body)
    return Error{ErrorCode::Io, "Cannot create editor controls"};
  SendMessageW(s->body, EM_SETTEXTMODE, TM_PLAINTEXT, 0);
  SendMessageW(s->body, EM_SETUNDOLIMIT, 0, 0);
  SendMessageW(s->body, EM_EXLIMITTEXT, 0, 16 * 1024 * 1024);
  SendMessageW(s->body, EM_SETEVENTMASK, 0, ENM_CHANGE);
  s->message = control(L"STATIC",
                       L"Changes save locally. Undo history resets when you "
                       L"change selected item.",
                       513, 0);
  SetWindowSubclass(s->tree, State::edit_proc, 1,
                    reinterpret_cast<DWORD_PTR>(s.get()));
  SetWindowSubclass(s->body, State::edit_proc, 1,
                    reinterpret_cast<DWORD_PTR>(s.get()));
  SetWindowSubclass(s->shortcut, State::edit_proc, 1,
                    reinterpret_cast<DWORD_PTR>(s.get()));
  if (SUCCEEDED(s->ole)) {
    s->target = new State::DropTarget(s);
    if (FAILED(RegisterDragDrop(s->tree, s->target))) {
      s->target->Release();
      s->target = nullptr;
    }
  }
  s->layout();
  s->rebuild();
  if (!s->target)
    s->status(
        Error{ErrorCode::Unsupported,
              "OLE drag unavailable; restart editor on a normal desktop"});
  ShowWindow(s->window, SW_SHOW);
  return {};
}
Result<void> NativeSnippetEditor::process_events() {
  auto s = state_;
  if (GetCurrentThreadId() != s->owner)
    return Error{ErrorCode::InvalidArgument, "Editor requires owner thread"};
  MSG msg{};
  for (unsigned n = 0;
       n < 64 && s->window && PeekMessageW(&msg, s->window, 0, 0, PM_REMOVE);
       ++n) {
    if (!IsDialogMessageW(s->window, &msg)) {
      TranslateMessage(&msg);
      DispatchMessageW(&msg);
    }
  }
  s->poll();
  return {};
}
Result<bool> NativeSnippetEditor::request_close() {
  auto s = state_;
  if (!s->window)
    return true;
  if (!s->confirm_close())
    return false;
  ShowWindow(s->window, SW_HIDE);
  return true;
}
std::uintptr_t NativeSnippetEditor::native_handle() const {
  return reinterpret_cast<std::uintptr_t>(state_->window);
}
bool NativeSnippetEditor::record_chord(const KeyChord &chord) {
  auto s = state_;
  if (s->transfer_locked())
    return false;
  if (!s->window || !IsWindowVisible(s->window) ||
      GetForegroundWindow() != s->window || GetFocus() != s->shortcut ||
      !IsWindowEnabled(s->shortcut))
    return false;
  SetWindowTextW(s->shortcut, wide(format_chord(chord)).c_str());
  return true;
}
Result<void> NativeSnippetEditor::import_xml_path(const std::string &path) {
#ifdef CLIPTIQ_EDITOR_XML
  auto s = state_;
  if (GetCurrentThreadId() != s->owner)
    return Error{ErrorCode::InvalidArgument, "Editor requires owner thread"};
  return s->start_xml(path, true);
#else
  (void)path;
  return Error{ErrorCode::Unsupported, "XML disabled in baseline probe"};
#endif
}
Result<void> NativeSnippetEditor::export_xml_path(const std::string &path) {
#ifdef CLIPTIQ_EDITOR_XML
  auto s = state_;
  if (GetCurrentThreadId() != s->owner)
    return Error{ErrorCode::InvalidArgument, "Editor requires owner thread"};
  return s->start_xml(path, false);
#else
  (void)path;
  return Error{ErrorCode::Unsupported, "XML disabled in baseline probe"};
#endif
}
bool NativeSnippetEditor::xml_transfer_busy() const {
#ifdef CLIPTIQ_EDITOR_XML
  return state_->xml.busy();
#else
  return false;
#endif
}
} // namespace cliptiq
