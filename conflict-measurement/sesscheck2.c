#include <openssl/ssl.h>
#include <openssl/bio.h>
#include <stdio.h>
static long added=0;
static int newcb(SSL*s, SSL_SESSION*sess){ added++; return 0; }
int main(){
  SSL_library_init();
  SSL_CTX *sc=SSL_CTX_new(TLS_server_method()), *cc=SSL_CTX_new(TLS_client_method());
  SSL_CTX_use_certificate_file(sc,"eccert.pem",SSL_FILETYPE_PEM);
  SSL_CTX_use_PrivateKey_file(sc,"eckey.pem",SSL_FILETYPE_PEM);
  SSL_CTX_set_verify(cc,SSL_VERIFY_NONE,0);
  SSL_CTX_set_session_cache_mode(sc, SSL_SESS_CACHE_SERVER);
  SSL_CTX_set_min_proto_version(sc,TLS1_2_VERSION); SSL_CTX_set_max_proto_version(sc,TLS1_2_VERSION);
  SSL_CTX_set_options(sc, SSL_OP_NO_TICKET);
  SSL_CTX_sess_set_new_cb(sc, newcb);
  for(int n=0;n<50;n++){
    SSL *cli=SSL_new(cc), *srv=SSL_new(sc);
    SSL_set_connect_state(cli); SSL_set_accept_state(srv);
    BIO *cw=BIO_new(BIO_s_mem()), *sw=BIO_new(BIO_s_mem()); BIO_up_ref(cw); BIO_up_ref(sw);
    SSL_set_bio(cli,sw,cw); SSL_set_bio(srv,cw,sw);
    for(int i=0;i<64;i++){ SSL_do_handshake(cli); SSL_do_handshake(srv);
      if(SSL_is_init_finished(cli)&&SSL_is_init_finished(srv)) break; }
    /* drive post-handshake app data both ways (this is what a real server does) */
    unsigned char b[16]="hello"; char r[64];
    SSL_write(cli,b,5); SSL_read(srv,r,sizeof r);
    SSL_write(srv,b,5); SSL_read(cli,r,sizeof r);
    SSL_free(cli); SSL_free(srv);
  }
  printf("sess_number=%ld new_cb_calls=%ld sess_accept=%ld\n",
     SSL_CTX_sess_number(sc), added, SSL_CTX_sess_accept(sc));
  return 0;
}
