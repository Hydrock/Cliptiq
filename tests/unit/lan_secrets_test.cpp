#include "platform/lan/Secrets.hpp"
#include <cstdlib>
#include <iostream>
#include <stdexcept>
using namespace cliptiq;
#define CHECK(x) do { if(!(x))throw std::runtime_error(#x); } while(false)
int main(){try {
 auto key=lan::generate_key();CHECK(key);CHECK(key.value().size()==64);
 auto another=lan::generate_key();CHECK(another);CHECK(key.value()!=another.value());
 for(const auto& ref:{std::string(),std::string("bad/ref"),std::string("bad\0ref",7),std::string(129,'a')}) {
  auto read=lan::read_secret(ref);CHECK(!read);CHECK(read.error().code==ErrorCode::InvalidArgument);
  auto write=lan::write_secret(ref,key.value());CHECK(!write);CHECK(write.error().code==ErrorCode::InvalidArgument);
  auto erase=lan::erase_secret(ref);CHECK(!erase);CHECK(erase.error().code==ErrorCode::InvalidArgument);
 }
 for(const auto& invalid:{std::string(),std::string(63,'a'),std::string(64,'g'),std::string(65,'a')}) {
  auto write=lan::write_secret("unit-test",invalid);CHECK(!write);CHECK(write.error().code==ErrorCode::InvalidArgument);
 }
 // Opt in to real OS keyring writes; never overwrite a fixed user credential.
 if(std::getenv("CLIPTIQ_TEST_NATIVE_SECRETS")) {
  const auto ref="test-"+key.value();
  CHECK(lan::erase_secret(ref));
  auto missing=lan::read_secret(ref);CHECK(!missing);CHECK(missing.error().code==ErrorCode::NotFound);
  CHECK(lan::write_secret(ref,key.value()));auto first=lan::read_secret(ref);CHECK(first);CHECK(first.value()==key.value());
  auto replaced=lan::write_secret(ref,another.value());
  if(!replaced){lan::erase_secret(ref);throw std::runtime_error(replaced.error().message);}
  auto second=lan::read_secret(ref);auto erased=lan::erase_secret(ref);CHECK(erased);CHECK(second);CHECK(second.value()==another.value());
  CHECK(lan::erase_secret(ref));missing=lan::read_secret(ref);CHECK(!missing);CHECK(missing.error().code==ErrorCode::NotFound);
 }
 std::cout<<"PASS LAN native secret validation\n";return 0;
}catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}}
