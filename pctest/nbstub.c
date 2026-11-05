// The PC engine tests run TCP-only and in plaintext: the non-blocking µTP
// transport (engine/utp_nb.cpp, libutp) and MSE encryption (engine/mse_peer.c,
// mbedtls bignum) are not built here. These stand-ins make every µTP dial fail
// at once and MSE never engage -- the paths the engine already handles (MSE is
// off by default on the console too).
#include <stddef.h>
#include <string.h>

#include "mse_peer.h"
#include "utp_nb.h"

int  utp_nb_init(void) { return -1; }
void utp_nb_exit(void) {}
int  utp_nb_fd(void) { return -1; }
void utp_nb_service(void) {}
utp_nb_sess *utp_nb_connect(uint32_t ip_net, uint16_t port_host) {
    (void)ip_net; (void)port_host;
    return NULL;
}
void utp_nb_close(utp_nb_sess *s) { (void)s; }
int  utp_nb_read(utp_nb_sess *s, void *buf, int len) {
    (void)s; (void)buf; (void)len;
    return -1;
}
int  utp_nb_write(utp_nb_sess *s, const void *buf, int len) {
    (void)s; (void)buf; (void)len;
    return -1;
}
int  utp_nb_state(const utp_nb_sess *s) { (void)s; return -1; }

void mse_rc4_init(mse_rc4 *r, const uint8_t *key, size_t key_len) {
    (void)key; (void)key_len;
    memset(r, 0, sizeof(*r));
}
void mse_rc4_crypt(mse_rc4 *r, const uint8_t *in, uint8_t *out, size_t len) {
    (void)r;
    if (in != out) memmove(out, in, len);
}
void mse_rc4_skip(mse_rc4 *r, size_t n) { (void)r; (void)n; }
void mse_peer_start(mse_peer *m, const uint8_t info_hash[20],
                    const uint8_t *ia, size_t ia_len,
                    uint8_t *out, size_t *out_len) {
    (void)info_hash; (void)ia; (void)ia_len; (void)out;
    memset(m, 0, sizeof(*m));
    if (out_len) *out_len = 0;
}
int mse_peer_feed(mse_peer *m, const uint8_t *in, size_t in_len,
                  size_t *consumed, uint8_t *out, size_t out_cap,
                  size_t *produced) {
    (void)m; (void)in; (void)in_len; (void)out; (void)out_cap;
    if (consumed) *consumed = 0;
    if (produced) *produced = 0;
    return MSE_FAIL;
}
void mse_peer_decrypt(mse_peer *m, uint8_t *buf, size_t len) {
    (void)m; (void)buf; (void)len;
}
