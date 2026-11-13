#include "CryptoRuntime.hpp"
#include <mbedtls/threading.h>
#include <psa/crypto.h>
#include <mutex>
namespace cliptiq::lan {
namespace {
#ifdef _WIN32
void mutex_init(mbedtls_threading_mutex_t *m){InitializeCriticalSection(&m->native);m->valid=1;}
void mutex_free(mbedtls_threading_mutex_t *m){if(m->valid){DeleteCriticalSection(&m->native);m->valid=0;}}
int mutex_lock(mbedtls_threading_mutex_t *m){if(!m->valid)return MBEDTLS_ERR_THREADING_BAD_INPUT_DATA;EnterCriticalSection(&m->native);return 0;}
int mutex_unlock(mbedtls_threading_mutex_t *m){if(!m->valid)return MBEDTLS_ERR_THREADING_BAD_INPUT_DATA;LeaveCriticalSection(&m->native);return 0;}
#endif
}
Result<void> initialize_crypto_runtime(){
 static std::once_flag once;static bool ready=false;
 std::call_once(once,[]{
#ifdef _WIN32
  mbedtls_threading_set_alt(mutex_init,mutex_free,mutex_lock,mutex_unlock);
#endif
  ready=psa_crypto_init()==PSA_SUCCESS;
 });
 if(!ready)return Error{ErrorCode::Io,"Secure crypto initialization failed"};return {};
}
}
