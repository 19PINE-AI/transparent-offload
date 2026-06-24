#include <openssl/ssl.h>
#include <openssl/err.h>
#include <openssl/bio.h>
#include <stdio.h>
#include <string.h>
/* Does TLS1.2 + NO_TICKET + SSL_SESS_CACHE_SERVER actually populate the internal
   session cache with a BIO-pair in-memory handshake? (audit of the round-1 claim) */
static void pump(SSL*a){ (void)a; }
int main(){
  SSL_library_init(); OpenSSL_add_all_algorithms(); SSL_load_error_strings();
  SSL_CTX *sc=SSL_CTX_new(TLS_server_method()), *cc=SSL_CTX_new(TLS_client_method());
  SSL_CTX_use_certificate_file(sc,"eccert.pem",SSL_FILETYPE_PEM);
  SSL_CTX_use_PrivateKey_file(sc,"eckey.pem",SSL_FILETYPE_PEM);
  SSL_CTX_set_verify(cc,SSL_VERIFY_NONE,0);
  SSL_CTX_set_session_cache_mode(sc, SSL_SESS_CACHE_SERVER);
  SSL_CTX_set_min_proto_version(sc,TLS1_2_VERSION); SSL_CTX_set_max_proto_version(sc,TLS1_2_VERSION);
  SSL_CTX_set_options(sc, SSL_OP_NO_TICKET);
  for(int n=0;n<200;n++){
    SSL *cli=SSL_new(cc), *srv=SSL_new(sc);
    SSL_set_connect_state(cli); SSL_set_accept_state(srv);
    BIO *cw=BIO_new(BIO_s_mem()), *sw=BIO_new(BIO_s_mem()); BIO_up_ref(cw); BIO_up_ref(sw);
    SSL_set_bio(cli,sw,cw); SSL_set_bio(srv,cw,sw);
    for(int i=0;i<64;i++){ SSL_do_handshake(cli); SSL_do_handshake(srv);
      if(SSL_is_init_finished(cli)&&SSL_is_init_finished(srv)) break; }
    if(n==0) printf("negotiated version=%s cipher=%s resumable_session=%d\n",
       SSL_get_version(srv), SSL_get_cipher(srv), SSL_SESSION_is_resumable(SSL_get_session(srv)));
    SSL_free(cli); SSL_free(srv);
  }
  printf("after 200 handshakes: SSL_CTX_sess_number=%ld sess_connect=%ld sess_accept=%ld\n",
     SSL_CTX_sess_number(sc), SSL_CTX_sess_connect(sc), SSL_CTX_sess_accept(sc));
  return 0;
}
