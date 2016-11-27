/* -*-mode:c++; tab-width: 4; indent-tabs-mode: nil; c-basic-offset: 4 -*- */

#include <cassert>
#include <vector>
#include <thread>
#include <mutex>
#include <algorithm>

#include "secure_socket.hh"
#include "certificate.hh"
#include "exception.hh"

using namespace std;

/* error category for OpenSSL */
class ssl_error_category : public error_category
{
public:
    const char * name( void ) const noexcept override { return "SSL"; }
    string message( const int ssl_error ) const noexcept override
    {
        return ERR_error_string( ssl_error, nullptr );
    }
};

class ssl_error : public tagged_error
{
public:
    ssl_error( const string & s_attempt,
               const int error_code = ERR_get_error() )
        : tagged_error( ssl_error_category(), s_attempt, error_code )
    {}
};

class OpenSSL
{
private:
    vector<mutex> locks_;

    static void locking_function( int mode, int n, const char *, int )
    {
        if ( mode & CRYPTO_LOCK ) {
            OpenSSL::global_context().locks_.at( n ).lock();
        } else {
            OpenSSL::global_context().locks_.at( n ).unlock();
        }
    }

    static unsigned long id_function( void )
    {
        return pthread_self();
    }

public:
    OpenSSL()
        : locks_( CRYPTO_num_locks() )
    {
        /* SSL initialization: Needs to be done exactly once */
        /* load algorithms/ciphers */
        SSL_library_init();
        OpenSSL_add_all_algorithms();

        /* load error messages */
        SSL_load_error_strings();

        /* set thread-safe callbacks */
        CRYPTO_set_locking_callback( locking_function );
        CRYPTO_set_id_callback( id_function );
    }

    static OpenSSL & global_context( void )
    {
        static OpenSSL os;
        return os;
    }
};


const std::string H1_1_ALPN = std::string("\x8http/1.1");
const std::string H1_1 = std::string("http/1.1");


bool select_proto(const unsigned char **out, unsigned char *outlen,
                  const unsigned char *in, unsigned int inlen,
                  const std::string &key) {
  for (auto p = in, end = in + inlen; p + key.size() <= end; p += *p + 1) {
    if (std::equal(std::begin(key), std::end(key), p)) {
      *out = p + 1;
      *outlen = *p;
      //std::cout << "NPN SELECTED: " << *out << std::endl;
      return true;
    }
  }
  return false;
}

std::vector<unsigned char> get_default_alpn() {
  auto res = std::vector<unsigned char>(H1_1_ALPN.size());
  auto p = std::begin(res);

  p = std::copy_n(std::begin(H1_1_ALPN), H1_1_ALPN.size(), p);

  return res;
}

bool select_http1(const unsigned char **out, unsigned char *outlen,
               const unsigned char *in, unsigned int inlen) {
  return select_proto(out, outlen, in, inlen, H1_1);
}

int client_select_next_proto_cb(SSL *, unsigned char **out,
                                unsigned char *outlen, const unsigned char *in,
                                unsigned int inlen, void *) {
    //std::cout << "[NPN] server offers:" << std::endl;
  //in is the size of the current str.
  /*for (unsigned int i = 0; i < inlen; i += in [i] + 1) {
      std::cout << "          * ";
      std::cout.write(reinterpret_cast<const char *>(&in[i + 1]), in[i]);
      std::cout << std::endl;
  }*/
  if (!select_http1(const_cast<const unsigned char **>(out), outlen, in, inlen)) {
    //std::cout << "NO NPN HTTP1 SELECTED" << std::endl;
    return SSL_TLSEXT_ERR_NOACK;
  }
  return SSL_TLSEXT_ERR_OK;
}

SSL_CTX * initialize_new_context( const SSL_MODE type )
{
    OpenSSL::global_context();
    SSL_CTX * ret = SSL_CTX_new( type == CLIENT ? SSLv23_client_method() : SSLv23_server_method() );
    
    if ( not ret ) {
        throw ssl_error( "SSL_CTL_new" );
    }
    
    if(type == CLIENT)
    {
        //Stolen from nghttp2
        const char *const CIPHER_LIST =
        "ECDHE-RSA-AES128-GCM-SHA256:ECDHE-ECDSA-AES128-GCM-SHA256:ECDHE-RSA-"
        "AES256-GCM-SHA384:ECDHE-ECDSA-AES256-GCM-SHA384:DHE-RSA-AES128-GCM-SHA256:"
        "DHE-DSS-AES128-GCM-SHA256:kEDH+AESGCM:ECDHE-RSA-AES128-SHA256:ECDHE-ECDSA-"
        "AES128-SHA256:ECDHE-RSA-AES128-SHA:ECDHE-ECDSA-AES128-SHA:ECDHE-RSA-"
        "AES256-SHA384:ECDHE-ECDSA-AES256-SHA384:ECDHE-RSA-AES256-SHA:ECDHE-ECDSA-"
        "AES256-SHA:DHE-RSA-AES128-SHA256:DHE-RSA-AES128-SHA:DHE-DSS-AES128-SHA256:"
        "DHE-RSA-AES256-SHA256:DHE-DSS-AES256-SHA:DHE-RSA-AES256-SHA:AES128-GCM-"
        "SHA256:AES256-GCM-SHA384:AES128-SHA256:AES256-SHA256:AES128-SHA:AES256-"
        "SHA:AES:CAMELLIA:DES-CBC3-SHA:!aNULL:!eNULL:!EXPORT:!DES:!RC4:!MD5:!PSK:!"
        "aECDH:!EDH-DSS-DES-CBC3-SHA:!EDH-RSA-DES-CBC3-SHA:!KRB5-DES-CBC3-SHA";
        auto ssl_opts = (SSL_OP_ALL & ~SSL_OP_DONT_INSERT_EMPTY_FRAGMENTS) |
                SSL_OP_NO_SSLv2 | SSL_OP_NO_SSLv3 | SSL_OP_NO_COMPRESSION |
                SSL_OP_NO_SESSION_RESUMPTION_ON_RENEGOTIATION;

        SSL_CTX_set_options(ret, ssl_opts);
        SSL_CTX_set_mode(ret, SSL_MODE_AUTO_RETRY);
        SSL_CTX_set_mode(ret, SSL_MODE_RELEASE_BUFFERS);
        if (SSL_CTX_set_cipher_list(ret, CIPHER_LIST) == 0) {
            throw ssl_error( "SSL_CTX_set_cipher_list" );
        }

        auto proto_list = get_default_alpn();
        SSL_CTX_set_next_proto_select_cb(ret, client_select_next_proto_cb,nullptr);
        SSL_CTX_set_alpn_protos(ret, proto_list.data(), proto_list.size());
    }

    return ret;
}

SSLContext::SSLContext( const SSL_MODE type )
    : ctx_( initialize_new_context( type ) )
{
    if ( type == SERVER ) {
        if ( not SSL_CTX_use_certificate_ASN1( ctx_.get(), 678, certificate ) ) {
            throw ssl_error( "SSL_CTX_use_certificate_ASN1" );
        }

        if ( not SSL_CTX_use_RSAPrivateKey_ASN1( ctx_.get(), private_key, 1191 ) ) {
            throw ssl_error( "SSL_CTX_use_RSAPrivateKey_ASN1" );
        }

        /* check consistency of private key with loaded certificate */
        if ( not SSL_CTX_check_private_key( ctx_.get() ) ) {
            throw ssl_error( "SSL_CTX_check_private_key" );
        }
    }
}

SecureSocket::SecureSocket( TCPSocket && sock, SSL * ssl )
    : TCPSocket( move( sock ) ),
      ssl_( ssl )
{
    if ( not ssl_ ) {
        throw runtime_error( "SecureSocket: constructor must be passed valid SSL structure" );
    }

    if ( not SSL_set_fd( ssl_.get(), fd_num() ) ) {
        throw ssl_error( "SSL_set_fd" );
    }
    

    SSL_set_mode( ssl_.get(), SSL_MODE_AUTO_RETRY );
}

SecureSocket SSLContext::new_secure_socket( TCPSocket && sock )
{
    return SecureSocket( move( sock ),
                         SSL_new( ctx_.get() ) );
}

void SecureSocket::connect( void )
{
    if ( not SSL_connect( ssl_.get() ) ) {
        ERR_print_errors_fp(stderr);
        throw ssl_error( "SSL_connect" );
    }
}

static int ssl_servername_cb(SSL *s, int *, void *arg)
{
    SSLContext *ctxarg = (SSLContext *) arg;

    const char *servername = SSL_get_servername(s, TLSEXT_NAMETYPE_host_name);
    if (servername && ctxarg)
    {
        ctxarg->set_requested_servername(std::string(servername));
    }

    return SSL_TLSEXT_ERR_OK;
}


void SSLContext::set_requested_servername( const std::string &servername )
{
    requested_servername_ = servername;
}

std::string SSLContext::get_requested_servername( void )
{
    return requested_servername_;
}



void SecureSocket::set_sni_servername_to_request( const std::string &servername )
{
    servername_to_request_ = servername;
    SSL_set_tlsext_host_name(ssl_.get(), servername_to_request_.c_str());
}


void SSLContext::register_server_sni_callback( void )
{
    SSL_CTX_set_tlsext_servername_callback(ctx_.get(), ssl_servername_cb);
    SSL_CTX_set_tlsext_servername_arg(ctx_.get(), this);
}

void SecureSocket::accept( void )
{
    const auto ret = SSL_accept( ssl_.get() );
    if ( ret == 1 ) {
        return;
    } else {
        throw ssl_error( "SSL_accept" );
    }

    register_read();
}

string SecureSocket::read( void )
{
    /* SSL record max size is 16kB */
    const size_t SSL_max_record_length = 16384;

    char buffer[ SSL_max_record_length ];

    ssize_t bytes_read = SSL_read( ssl_.get(), buffer, SSL_max_record_length );

    /* Make sure that we really are reading from the underlying fd */
    assert( 0 == SSL_pending( ssl_.get() ) );

    if ( bytes_read == 0 ) {
        int error_return = SSL_get_error( ssl_.get(), bytes_read );
        if ( SSL_ERROR_ZERO_RETURN == error_return ) { /* Clean SSL close */
            set_eof();
        } else if ( SSL_ERROR_SYSCALL == error_return ) { /* Underlying TCP connection close */
            /* Verify error queue is empty so we can conclude it is EOF */
            assert( ERR_get_error() == 0 );
            set_eof();
        }
        register_read();
        return string(); /* EOF */
    } else if ( bytes_read < 0 ) {
        throw ssl_error( "SSL_read" );
    } else {
        /* success */
        register_read();
        return string( buffer, bytes_read );
    }
}

void SecureSocket::write(const string & message )
{
    /* SSL_write returns with success if complete contents of message are written */
    ssize_t bytes_written = SSL_write( ssl_.get(), message.data(), message.length() );

    if ( bytes_written < 0 ) {
        throw ssl_error( "SSL_write" );
    }

    register_write();
}
