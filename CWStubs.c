typedef int CWBool;
typedef int CWSocket;
typedef void* CWSecuritySession;
typedef void* CWSecurityContext;
typedef void* CWNetworkLev4Address;
typedef void* CWSafeList;

CWBool CWSecurityReceive(CWSecuritySession s, char *buf, int len, int *rb) { return 1; }
CWBool CWSecuritySend(CWSecuritySession s, const char *buf, int len) { return 1; }
void   CWSecurityDestroySession(CWSecuritySession s) {}
void   CWSecurityDestroyContext(void *ctx) {}
void   CWSslCleanUp(void) {}
CWBool CWSecurityInitContext(CWSecurityContext *ctx, const char *ca, const char *cert, const char *key, CWBool isClient, int (*cb)(void*)) { return 0; }
CWBool CWSecurityInitSessionClient(CWSocket sock, CWNetworkLev4Address addr, CWSafeList list, CWSecurityContext ctx, CWSecuritySession *sp, int *rb) { return 1; }
CWBool CWSecurityInitSessionServer(CWSocket sock, CWNetworkLev4Address addr, CWSecurityContext ctx, CWSecuritySession *sp) { return 1; }
CWBool CWSecurityInitSessionServerDataChannel(CWSocket sock, CWNetworkLev4Address addr, CWSecurityContext ctx, CWSecuritySession *sp) { return 1; }
CWBool CWSecurityInitGenericSessionServerDataChannel(void *list, CWSocket sock, CWSecurityContext ctx, CWSecuritySession *sp) { return 1; }
void   CWNetworkDeleteMHInterface(void *iface) {}
