#pragma once
#include "cliptiq/storage.hpp"
#include "sqlite3.h"
#include <stdexcept>
namespace cliptiq::detail {
struct Failure { Error error; };
inline Error db_error(sqlite3* db,int rc) {
 ErrorCode c=ErrorCode::Io;
 switch(rc&255){case SQLITE_BUSY:case SQLITE_LOCKED:c=ErrorCode::Busy;break;case SQLITE_READONLY:c=ErrorCode::ReadOnly;break;case SQLITE_FULL:c=ErrorCode::Full;break;case SQLITE_CORRUPT:case SQLITE_NOTADB:c=ErrorCode::Corrupt;break;case SQLITE_TOOBIG:c=ErrorCode::TooLarge;break;}
 return {c,db?sqlite3_errmsg(db):sqlite3_errstr(rc)};
}
inline void check(sqlite3* db,int rc){if(rc!=SQLITE_OK)throw Failure{db_error(db,rc)};}
inline void exec(sqlite3* db,const char* sql){check(db,sqlite3_exec(db,sql,nullptr,nullptr,nullptr));}
class Statement {
 sqlite3* db_; sqlite3_stmt* p_=nullptr;
public:
 Statement(sqlite3* db,const char* sql):db_(db){check(db,sqlite3_prepare_v2(db,sql,-1,&p_,nullptr));}
 ~Statement(){sqlite3_finalize(p_);}
 Statement(const Statement&)=delete;
 void bind(int i,const std::string& s){check(db_,sqlite3_bind_text64(p_,i,s.data(),s.size(),SQLITE_TRANSIENT,SQLITE_UTF8));}
 void bind(int i,std::int64_t n){check(db_,sqlite3_bind_int64(p_,i,n));}
 void bind(int i,const Bytes& b){static const unsigned char empty=0;check(db_,sqlite3_bind_blob64(p_,i,b.empty()?&empty:b.data(),b.size(),SQLITE_TRANSIENT));}
 void null(int i){check(db_,sqlite3_bind_null(p_,i));}
 bool row(){int rc=sqlite3_step(p_);if(rc==SQLITE_ROW)return true;if(rc==SQLITE_DONE)return false;throw Failure{db_error(db_,rc)};}
 void done(){if(row())throw Failure{{ErrorCode::Internal,"unexpected SQL result"}};}
 std::int64_t number(int i)const{return sqlite3_column_int64(p_,i);}
 bool is_null(int i)const{return sqlite3_column_type(p_,i)==SQLITE_NULL;}
 std::string text(int i)const{auto p=sqlite3_column_text(p_,i);auto n=sqlite3_column_bytes(p_,i);return p?std::string(reinterpret_cast<const char*>(p),n):std::string();}
 Bytes bytes(int i)const{auto p=static_cast<const std::uint8_t*>(sqlite3_column_blob(p_,i));auto n=sqlite3_column_bytes(p_,i);return n?Bytes(p,p+n):Bytes();}
};
class Transaction {
 sqlite3* db_;bool committed_=false;
public:
 explicit Transaction(sqlite3* db):db_(db){exec(db_,"BEGIN IMMEDIATE");}
 ~Transaction(){if(!committed_)sqlite3_exec(db_,"ROLLBACK",nullptr,nullptr,nullptr);}
 void commit(){exec(db_,"COMMIT");committed_=true;}
};
inline void revision(sqlite3* db){exec(db,"UPDATE store_state SET data_revision=data_revision+1 WHERE id=1");}
template<class T,class F> Result<T> guarded(F f){try{return f();}catch(const Failure& e){return e.error;}catch(const std::bad_alloc&){return Error{ErrorCode::TooLarge,"allocation failed"};}catch(const std::exception& e){return Error{ErrorCode::Internal,e.what()};}}
inline void invalid(const char* msg){throw Failure{{ErrorCode::InvalidArgument,msg}};}
}
