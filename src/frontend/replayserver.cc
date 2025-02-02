/* -*-mode:c++; tab-width: 4; indent-tabs-mode: nil; c-basic-offset: 4 -*- */
#include "timestamp.hh"
#include <fstream>
#include <unordered_map>
#include <sstream>
#include <algorithm>

#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <unistd.h>

#include <iostream>
#include <vector>
#include <limits>

#include "util.hh"
#include "http_record.pb.h"
#include "http_header.hh"
#include "exception.hh"
#include "http_request.hh"
#include "http_response.hh"
#include "file_descriptor.hh"

using namespace std;

//string log = "";

string safe_getenv( const string & key )
{
    const char * const value = getenv( key.c_str() );
    if ( not value ) {
        throw runtime_error( "missing environment variable: " + key );
    }
    return value;
}

/* does the actual HTTP header match this stored request? */
bool header_match( const string & env_var_name,
                   const string & header_name,
                   const HTTPRequest & saved_request )
{
    const char * const env_value = getenv( env_var_name.c_str() );

    /* case 1: neither header exists (OK) */
    if ( (not env_value) and (not saved_request.has_header( header_name )) ) {
        return true;
    }

    /* case 2: headers both exist (OK if values match) */
    if ( env_value and saved_request.has_header( header_name ) ) {
        return saved_request.get_header_value( header_name ) == string( env_value );
    }

    /* case 3: one exists but the other doesn't (failure) */
    return false;
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

/* compare request_line and certain headers of incoming request and stored request */
int match_score( const MahimahiProtobufs::RequestResponse & saved_record,
                          const string & request_line,
                          const bool is_https )
{
    const HTTPRequest saved_request( saved_record.request() );

    /* match HTTP/HTTPS */
    if ( is_https and (saved_record.scheme() != MahimahiProtobufs::RequestResponse_Scheme_HTTPS) ) {
        return -5;
    }

    if ( (not is_https) and (saved_record.scheme() != MahimahiProtobufs::RequestResponse_Scheme_HTTP) ) {
        return -4;
    }

    /* match host header */
    if ( not header_match( "HTTP_HOST", "Host", saved_request ) ) {
        return -3;
    }

    /* match user agent */
    if ( not header_match( "HTTP_USER_AGENT", "User-Agent", saved_request ) ) {
        return -2;
    }

    /* must match first line up to "?" at least */
    if ( strip_query( request_line ) != strip_query( saved_request.first_line() ) ) {
        //log += strip_query( request_line ) + " vs. " + strip_query( saved_request.first_line() ) + " | ";
        return -1;
    }

    //if (request_line.find("zone-manager") != string::npos) return 0;
    /* success! return size of common prefix */
    const auto max_match = min( request_line.size(), saved_request.first_line().size() );
    for ( unsigned int i = 0; i < max_match; i++ ) {
        if ( request_line.at( i ) != saved_request.first_line().at( i ) ) {
            return i;
        }
    }

    return max_match;
}

/*static float find_delays(const string &host, const string &recording_dir, const string &uri)
{
    float delay = 0;
    ifstream in(recording_dir + "/" + host);
    if (in.is_open()){
        string line;
        string curr_uri;
        float tmp_delay;
        while(getline(in, line)){
            istringstream is(line);
            is >> curr_uri >> tmp_delay;
            if (curr_uri == uri) {
                delay = tmp_delay;
                break;
            }
        }
    }
    return delay;
}*/

int main( void )
{
    try {
       //unsigned duration = (argc > 1)?atoi(argv[1]):0;
    
        assert_not_root();
        const string working_directory = safe_getenv( "MAHIMAHI_CHDIR" );
        const string recording_directory = safe_getenv( "MAHIMAHI_RECORD_PATH" );
        const string request_line = safe_getenv( "REQUEST_METHOD" )
            + " " + safe_getenv( "REQUEST_URI" )
            + " " + safe_getenv( "SERVER_PROTOCOL" );
        const bool is_https = getenv( "HTTPS" );
        const string host = safe_getenv( "HTTP_HOST" );
        const string uri = safe_getenv( "REQUEST_URI" );
        //const unsigned rtt_delay = stoi(safe_getenv("RTT_DELAY"));
        SystemCall( "chdir", chdir( working_directory.c_str() ) );

        const vector< string > files = list_directory_contents( recording_directory );
        int best_score = -5;
        MahimahiProtobufs::RequestResponse best_match;

        for ( const auto & filename : files ) {
            size_t point = filename.find_last_of('.');
            if (filename.substr(point-4, 4) != "save") continue;

            FileDescriptor fd( SystemCall( "open", open( filename.c_str(), O_RDONLY ) ) );
            MahimahiProtobufs::RequestResponse current_record;
            if ( not current_record.ParseFromFileDescriptor( fd.fd_num() ) ) {
                throw runtime_error( filename + ": invalid HTTP request/response" );
            }

            int score = match_score( current_record, request_line, is_https );
            if ( score > 0 and score >  best_score ) {
                best_match = current_record;
                best_score = score;
            } else if (score < 0 and score > best_score)
                best_score = score;
        }

        //if (duration) delay(duration);
        if ( best_score > 0 ) { /* give client the best match */
            //delay(max(0, int(find_delays(host, recording_directory, strip_query(uri))) - int(rtt_delay)) );
            cout << HTTPResponse( best_match.response() ).str();
            return EXIT_SUCCESS;
        } else {                /* no acceptable matches for request */
            cout << "HTTP/1.1 404 Not Found" << CRLF;
            cout << "Content-Type: text/plain" << CRLF;
            cout << "Access-Control-Allow-Origin: *" << CRLF;
            cout << "replayserver: could not find a match for " << request_line <<CRLF;
            cout <<"Best_score: " << best_score << CRLF << CRLF;
            //cout << "Log: " << log <<CRLF << CRLF;
            return EXIT_FAILURE;
        }
    } catch ( const exception & e ) {
        cout << "HTTP/1.1 500 Internal Server Error" << CRLF;
        cout << "Content-Type: text/plain" << CRLF << CRLF;
        cout << "mahimahi mm-webreplay received an exception:" << CRLF << CRLF;
        print_exception( e, cout );
        return EXIT_FAILURE;
    }
}
