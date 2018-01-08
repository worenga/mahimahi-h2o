#include <string>

#include "config.h"


#ifndef H2O_CONFIGURATION_HH
#define H2O_CONFIGURATION_HH


const std::string h2o_main_cfg = R"END(
### Global Base
send-server-name: OFF
max-connections: 5000
max-delegations: 1000
num-threads: 2
tcp-fastopen: 0

### Global HTTP/1 Settings
http1-upgrade-to-http2: OFF
### Global TLS Settings
#Disable TLS Session Resumption
ssl-session-resumption:
    mode: off
num-ocsp-updaters: 1
### Global HTTP/2 Settings
http2-push-preload: ON
http2-reprioritize-blocking-assets: OFF
http2-casper: OFF
http2-idle-timeout: 30
http2-max-concurrent-requests-per-connection: 255
### Compression
#compress: [ gzip ]
compress: OFF
compress-minimum-size: 100
gzip: OFF
# access-log: /dev/null
# error-log: /dev/stdout
)END";
//pid-file: status/pid-file

const std::string h2o_ssl_config_certfile =std::string( MOD_SSL_CERTIFICATE_FILE );
const std::string h2o_ssl_config_keyfile =std::string( MOD_SSL_KEY );

#endif
