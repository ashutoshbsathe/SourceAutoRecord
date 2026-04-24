// Compatibility stubs for linking gRPC against SAR's OpenSSL 1.1 and
// systemd-free build environment. These symbols are referenced by libgrpc.a
// but never called at runtime (we use InsecureServerCredentials).
//
// Also contains Renderer stubs to avoid pulling in the FFMPEG dependency
// that the rest of SAR uses for video recording.

namespace Renderer {
int segmentEndTick = -1;
bool isDemoLoading = false;
void Frame() {}
void Init(void** videomode) {}
void Cleanup() {}
bool IsRunning() { return false; }
}  // namespace Renderer

extern "C" {

// OpenSSL 3.x symbols referenced by gRPC but absent from OpenSSL 1.1
void* EVP_MAC_fetch(void*, const char*, const char*) { return nullptr; }
void EVP_MAC_free(void*) {}
void* EVP_MAC_CTX_new(void*) { return nullptr; }
void EVP_MAC_CTX_free(void*) {}
int EVP_MAC_init(void*, const unsigned char*, unsigned long, void*) {
  return 0;
}
int EVP_MAC_update(void*, const unsigned char*, unsigned long) { return 0; }
int EVP_MAC_final(void*, unsigned char*, unsigned long*, unsigned long) {
  return 0;
}
int EVP_Q_digest(void*, const char*, const char*, const void*, unsigned long,
                 unsigned char*, unsigned long*) {
  return 0;
}
int EVP_DigestSignUpdate(void*, const void*, unsigned long) { return 0; }
void* OSSL_PARAM_construct_utf8_string(const char*, char*, unsigned long) {
  return nullptr;
}
void* OSSL_PARAM_construct_end(void) { return nullptr; }
void* SSL_get1_peer_certificate(const void*) { return nullptr; }

// systemd socket-activation symbols referenced by gRPC
int sd_listen_fds(int) { return 0; }
int sd_is_socket_inet(int, int, int, int, unsigned short) { return 0; }
int sd_is_socket_unix(int, int, int, const char*, unsigned int) { return 0; }
int sd_is_socket_sockaddr(int, int, const void*, unsigned int, int) {
  return 0;
}

}  // extern "C"
