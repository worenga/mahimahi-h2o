/* -*-mode:c++; tab-width: 4; indent-tabs-mode: nil; c-basic-offset: 4 -*- */

#include <iostream>
#include <string>

#include "backing_store.hh"
#include "http_record.pb.h"
#include "temp_file.hh"

using namespace std;

uint32_t hash32(const uint8_t *x, size_t n)
{
    uint32_t v = 2166136261;
    for (size_t i = 0; i < n; i++) {
        v ^= x[i];
        v *= 16777619;
    }
    return v;
}

string strip_query( const string & request_line )
{
    const auto index = request_line.find( "?" );
    if ( index == string::npos ) {
        return request_line;
    } else {
        return request_line.substr( 0, index );
    }
}


HTTPDiskStore::HTTPDiskStore( const string & record_folder )
    : record_folder_( record_folder ),
      mutex_()
{}

void HTTPDiskStore::save( const HTTPResponse & response, const Address & server_address )
{
    unique_lock<mutex> ul( mutex_ );

    /* output file to write current request/response pair protobuf (user has all permissions) */

    auto stripped = strip_query(response.request().first_line());
    string hash = to_string(hash32(reinterpret_cast<const uint8_t*>(stripped.c_str()),stripped.size()-1));
    UniqueFile file( record_folder_ + hash );


    /* construct protocol buffer */
    MahimahiProtobufs::RequestResponse output;

    output.set_ip( server_address.ip() );
    output.set_port( server_address.port() );
    output.set_scheme( server_address.port() == 443
                       ? MahimahiProtobufs::RequestResponse_Scheme_HTTPS
                       : MahimahiProtobufs::RequestResponse_Scheme_HTTP );
    output.mutable_request()->CopyFrom( response.request().toprotobuf() );
    output.mutable_response()->CopyFrom( response.toprotobuf() );

    if ( not output.SerializeToFileDescriptor( file.fd().fd_num() ) ) {
        throw runtime_error( "save_to_disk: failure to serialize HTTP request/response pair" );
    }

}
