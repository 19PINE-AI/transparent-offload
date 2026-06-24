#include <openssl/ssl.h>
#include <openssl/err.h>
#include <openssl/bio.h>
#include <stdio.h>
#include <string.h>
int main(){
    SSL_library_init(); OpenSSL_add_all_algorithms(); SSL_load_error_strings();
    SSL_CTX *sc=SSL_CTX_new(TLS_server_method()), *cc=SSL_CTX_new(TLS_client_method());
    SSL_CTX_use_certificate_file(sc,"cert.pem",SSL_FILETYPE_PEM);
    SSL_CTX_use_PrivateKey_file(sc,"key.pem",SSL_FILETYPE_PEM);
    SSL_CTX_set_verify(cc,SSL_VERIFY_NONE,0);
    SSL *cli=SSL_new(cc), *srv=SSL_new(sc);
    SSL_set_connect_state(cli); SSL_set_accept_state(srv);
    BIO *cw=BIO_new(BIO_s_mem()), *sw=BIO_new(BIO_s_mem());
    BIO_up_ref(cw); BIO_up_ref(sw);
    SSL_set_bio(cli, sw, cw);
    SSL_set_bio(srv, cw, sw);
    for(int i=0;i<64;i++){
        int rc=SSL_do_handshake(cli); int rs=SSL_do_handshake(srv);
        int ec=SSL_get_error(cli,rc), es=SSL_get_error(srv,rs);
        printf("i=%d cli rc=%d err=%d  srv rc=%d err=%d  cliFin=%d srvFin=%d\n",
            i,rc,ec,rs,es,SSL_is_init_finished(cli),SSL_is_init_finished(srv));
        if(SSL_is_init_finished(cli)&&SSL_is_init_finished(srv)){printf("HANDSHAKE OK ver=%s cipher=%s\n",SSL_get_version(srv),SSL_get_cipher(srv));break;}
        if(ec==SSL_ERROR_SSL||es==SSL_ERROR_SSL){printf("SSL ERROR\n");ERR_print_errors_fp(stdout);break;}
        if(i>8) {printf("not converging\n"); break;}
    }
    return 0;
}
