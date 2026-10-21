#pragma once

#include "update/HttpTransport.h"

/// The transport of the Windows build. The requests go through WinHTTP: Schannel with TLS 1.2 and 1.3,
/// the system certificate store and the proxy settings of the machine, so the package needs no TLS
/// plugin of Qt. The certificate of a server is always checked.
/// Only https addresses are fetched, and a redirect to another address is followed ten times at most.
/// With allowLoopbackHttp a test may also fetch from http://127.0.0.1.
HttpTransport* createWinHttpTransport(QObject* parent, bool allowLoopbackHttp);
