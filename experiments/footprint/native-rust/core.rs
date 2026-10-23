//! Throwaway single-threaded core; exact same ABI and SQLite as C++ variant.
use std::ffi::{c_char, c_int, c_void, CStr, CString};
use std::ptr;
static mut DB: *mut c_void = ptr::null_mut();
extern "C" {
    fn sqlite3_open(path: *const c_char, db: *mut *mut c_void) -> c_int;
    fn sqlite3_close(db: *mut c_void) -> c_int;
    fn sqlite3_exec(db: *mut c_void, sql: *const c_char, callback: *mut c_void, arg: *mut c_void, error: *mut *mut c_char) -> c_int;
    fn sqlite3_prepare_v2(db: *mut c_void, sql: *const c_char, len: c_int, stmt: *mut *mut c_void, tail: *mut *const c_char) -> c_int;
    fn sqlite3_bind_int(stmt: *mut c_void, index: c_int, value: c_int) -> c_int;
    fn sqlite3_bind_text(stmt: *mut c_void, index: c_int, value: *const c_char, len: c_int, destroy: Option<unsafe extern "C" fn(*mut c_void)>) -> c_int;
    fn sqlite3_bind_blob(stmt: *mut c_void, index: c_int, value: *const c_void, len: c_int, destroy: Option<unsafe extern "C" fn(*mut c_void)>) -> c_int;
    fn sqlite3_step(stmt: *mut c_void) -> c_int;
    fn sqlite3_column_int(stmt: *mut c_void, column: c_int) -> c_int;
    fn sqlite3_column_bytes(stmt: *mut c_void, column: c_int) -> c_int;
    fn sqlite3_column_blob(stmt: *mut c_void, column: c_int) -> *const c_void;
    fn sqlite3_column_text(stmt: *mut c_void, column: c_int) -> *const c_char;
    fn sqlite3_finalize(stmt: *mut c_void) -> c_int;
}
unsafe fn prepare(sql: &str) -> *mut c_void {
    let mut statement = ptr::null_mut();
    let query = CString::new(sql).unwrap();
    if sqlite3_prepare_v2(DB, query.as_ptr(), -1, &mut statement, ptr::null_mut()) != 0 { return ptr::null_mut(); }
    statement
}
unsafe fn scalar(sql: &str) -> c_int {
    let statement = prepare(sql);
    if statement.is_null() { return -1; }
    let value = if sqlite3_step(statement) == 100 { sqlite3_column_int(statement, 0) } else { -1 };
    sqlite3_finalize(statement); value
}
#[no_mangle] pub unsafe extern "C" fn probe_open(path: *const c_char) -> c_int {
    let mut handle = ptr::null_mut();
    let result = sqlite3_open(path, &mut handle);
    DB = handle;
    if result != 0 { return -1; }
    let schema = CString::new("CREATE TABLE IF NOT EXISTS assets(id INTEGER,format TEXT,data BLOB,PRIMARY KEY(id,format));CREATE TABLE IF NOT EXISTS snippets(title TEXT,content TEXT);DELETE FROM assets;DELETE FROM snippets;INSERT INTO snippets VALUES('Greeting','Hello & welcome'),('Unicode','Привет <мир>');").unwrap();
    sqlite3_exec(DB, schema.as_ptr(), ptr::null_mut(), ptr::null_mut(), ptr::null_mut())
}
#[no_mangle] pub unsafe extern "C" fn probe_add(id: c_int, format: *const c_char, data: *const u8, len: usize) -> c_int {
    if len > 32 * 1024 * 1024 { return -1; }
    let statement = prepare("INSERT OR REPLACE INTO assets VALUES(?,?,?)");
    if statement.is_null() { return -1; }
    // SQLITE_STATIC is safe here: input pointers remain live through step/finalize.
    sqlite3_bind_int(statement, 1, id);
    sqlite3_bind_text(statement, 2, format, -1, None);
    sqlite3_bind_blob(statement, 3, data.cast(), len as c_int, None);
    let result = if sqlite3_step(statement) == 101 { 0 } else { -1 };
    sqlite3_finalize(statement); result
}
#[no_mangle] pub unsafe extern "C" fn probe_count() -> c_int { scalar("SELECT count(DISTINCT id) FROM assets") }
#[no_mangle] pub unsafe extern "C" fn probe_representations() -> c_int { scalar("SELECT count(*) FROM assets") }
#[no_mangle] pub unsafe extern "C" fn probe_get(id: c_int, format: *const c_char, output: *mut u8, capacity: usize) -> c_int {
    let statement = prepare("SELECT data FROM assets WHERE id=? AND format=?");
    if statement.is_null() { return -1; }
    sqlite3_bind_int(statement, 1, id); sqlite3_bind_text(statement, 2, format, -1, None);
    let mut len = -1;
    if sqlite3_step(statement) == 100 {
        len = sqlite3_column_bytes(statement, 0);
        if len as usize > capacity { len = -1; }
        else if len > 0 { ptr::copy_nonoverlapping(sqlite3_column_blob(statement, 0).cast::<u8>(), output, len as usize); }
    }
    sqlite3_finalize(statement); len
}
fn escape(value: &str) -> String { value.replace('&', "&amp;").replace('<', "&lt;").replace('>', "&gt;") }
#[no_mangle] pub unsafe extern "C" fn probe_xml(output: *mut c_char, capacity: usize) -> c_int {
    let statement = prepare("SELECT title,content FROM snippets ORDER BY rowid");
    if statement.is_null() { return -1; }
    let mut xml = String::from("<folders><folder><title>Examples</title><snippets>");
    while sqlite3_step(statement) == 100 {
        let title = CStr::from_ptr(sqlite3_column_text(statement, 0)).to_string_lossy();
        let content = CStr::from_ptr(sqlite3_column_text(statement, 1)).to_string_lossy();
        xml.push_str(&format!("<snippet><title>{}</title><content>{}</content></snippet>", escape(&title), escape(&content)));
    }
    sqlite3_finalize(statement); xml.push_str("</snippets></folder></folders>");
    if xml.len() + 1 > capacity { return -1; }
    ptr::copy_nonoverlapping(xml.as_ptr(), output.cast(), xml.len()); *output.add(xml.len()) = 0; xml.len() as c_int
}
#[no_mangle] pub unsafe extern "C" fn probe_close() { if !DB.is_null() { sqlite3_close(DB); DB = ptr::null_mut(); } }
