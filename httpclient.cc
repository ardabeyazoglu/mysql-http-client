// define mysql component name
#define LOG_COMPONENT_TAG "httpclient"
#define NO_SIGNATURE_CHANGE 0
#define SIGNATURE_CHANGE 1

#include <components/httpclient/httpclient.h>

using json = nlohmann::json;

// declare which services are required for this component
REQUIRES_SERVICE_PLACEHOLDER(log_builtins);
REQUIRES_SERVICE_PLACEHOLDER(log_builtins_string);
REQUIRES_SERVICE_PLACEHOLDER(dynamic_privilege_register);
REQUIRES_SERVICE_PLACEHOLDER(udf_registration);
REQUIRES_SERVICE_PLACEHOLDER(mysql_udf_metadata);
REQUIRES_SERVICE_PLACEHOLDER(mysql_thd_security_context);
REQUIRES_SERVICE_PLACEHOLDER(mysql_thd_store);
REQUIRES_SERVICE_PLACEHOLDER(global_grants_check);
REQUIRES_SERVICE_PLACEHOLDER(mysql_current_thread_reader);
REQUIRES_SERVICE_PLACEHOLDER(mysql_runtime_error);
REQUIRES_SERVICE_PLACEHOLDER(status_variable_registration);

// declare log builtins to allow logging to error log
SERVICE_TYPE(log_builtins) * log_bi;
SERVICE_TYPE(log_builtins_string) * log_bs;

// define a custom privilege name required to call UDFs defined in this component
static const char *HTTPCLIENT_PRIVILEGE_NAME = "HTTP_CLIENT";

// define global status variables
static unsigned long long time_spent_ms = 0;
static unsigned long number_of_requests = 0;

static SHOW_VAR httpclient_status_variables[] = {
  {"httpclient.time_spent_ms", (char *)&time_spent_ms, SHOW_LONGLONG, SHOW_SCOPE_GLOBAL},
  {"httpclient.number_of_requests", (char *)&number_of_requests, SHOW_LONG, SHOW_SCOPE_GLOBAL}
};

// define all supported curl options that are int, long and string types
static std::map<std::string, std::tuple<CURLoption, long>> curl_options_available = {
  {"CURLOPT_ACCEPTTIMEOUT_MS", std::make_tuple(CURLOPT_ACCEPTTIMEOUT_MS, CURLOPTTYPE_LONG)},
  {"CURLOPT_ACCEPT_ENCODING", std::make_tuple(CURLOPT_ACCEPT_ENCODING, CURLOPTTYPE_STRINGPOINT)},
  {"CURLOPT_ADDRESS_SCOPE", std::make_tuple(CURLOPT_ADDRESS_SCOPE, CURLOPTTYPE_LONG)},
  {"CURLOPT_APPEND", std::make_tuple(CURLOPT_APPEND, CURLOPTTYPE_LONG)},
  {"CURLOPT_AUTOREFERER", std::make_tuple(CURLOPT_AUTOREFERER, CURLOPTTYPE_LONG)},
  {"CURLOPT_BUFFERSIZE", std::make_tuple(CURLOPT_BUFFERSIZE, CURLOPTTYPE_LONG)},
  {"CURLOPT_CAINFO", std::make_tuple(CURLOPT_CAINFO, CURLOPTTYPE_STRINGPOINT)},
  {"CURLOPT_CAPATH", std::make_tuple(CURLOPT_CAPATH, CURLOPTTYPE_STRINGPOINT)},
  {"CURLOPT_CERTINFO", std::make_tuple(CURLOPT_CERTINFO, CURLOPTTYPE_LONG)},
  {"CURLOPT_CONNECTTIMEOUT", std::make_tuple(CURLOPT_CONNECTTIMEOUT, CURLOPTTYPE_LONG)},
  {"CURLOPT_CONNECTTIMEOUT_MS", std::make_tuple(CURLOPT_CONNECTTIMEOUT_MS, CURLOPTTYPE_LONG)},
  {"CURLOPT_CONNECT_ONLY", std::make_tuple(CURLOPT_CONNECT_ONLY, CURLOPTTYPE_LONG)},
  {"CURLOPT_COOKIE", std::make_tuple(CURLOPT_COOKIE, CURLOPTTYPE_STRINGPOINT)},
  {"CURLOPT_COOKIEFILE", std::make_tuple(CURLOPT_COOKIEFILE, CURLOPTTYPE_STRINGPOINT)},
  {"CURLOPT_COOKIEJAR", std::make_tuple(CURLOPT_COOKIEJAR, CURLOPTTYPE_STRINGPOINT)},
  {"CURLOPT_COOKIELIST", std::make_tuple(CURLOPT_COOKIELIST, CURLOPTTYPE_STRINGPOINT)},
  {"CURLOPT_COOKIESESSION", std::make_tuple(CURLOPT_COOKIESESSION, CURLOPTTYPE_LONG)},
  {"CURLOPT_COPYPOSTFIELDS", std::make_tuple(CURLOPT_COPYPOSTFIELDS, CURLOPTTYPE_STRINGPOINT)},
  {"CURLOPT_CRLF", std::make_tuple(CURLOPT_CRLF, CURLOPTTYPE_LONG)},
  {"CURLOPT_CRLFILE", std::make_tuple(CURLOPT_CRLFILE, CURLOPTTYPE_STRINGPOINT)},
  {"CURLOPT_CUSTOMREQUEST", std::make_tuple(CURLOPT_CUSTOMREQUEST, CURLOPTTYPE_STRINGPOINT)},
  {"CURLOPT_DIRLISTONLY", std::make_tuple(CURLOPT_DIRLISTONLY, CURLOPTTYPE_LONG)},
  {"CURLOPT_DNS_CACHE_TIMEOUT", std::make_tuple(CURLOPT_DNS_CACHE_TIMEOUT, CURLOPTTYPE_LONG)},
  {"CURLOPT_DNS_INTERFACE", std::make_tuple(CURLOPT_DNS_INTERFACE, CURLOPTTYPE_STRINGPOINT)},
  {"CURLOPT_DNS_LOCAL_IP4", std::make_tuple(CURLOPT_DNS_LOCAL_IP4, CURLOPTTYPE_STRINGPOINT)},
  {"CURLOPT_DNS_LOCAL_IP6", std::make_tuple(CURLOPT_DNS_LOCAL_IP6, CURLOPTTYPE_STRINGPOINT)},
  {"CURLOPT_DNS_SERVERS", std::make_tuple(CURLOPT_DNS_SERVERS, CURLOPTTYPE_STRINGPOINT)},
  {"CURLOPT_DNS_USE_GLOBAL_CACHE", std::make_tuple(CURLOPT_DNS_USE_GLOBAL_CACHE, CURLOPTTYPE_LONG)},
  {"CURLOPT_DOH_URL", std::make_tuple(CURLOPT_DOH_URL, CURLOPTTYPE_STRINGPOINT)},
  {"CURLOPT_EGDSOCKET", std::make_tuple(CURLOPT_EGDSOCKET, CURLOPTTYPE_STRINGPOINT)},
  {"CURLOPT_EXPECT_100_TIMEOUT_MS", std::make_tuple(CURLOPT_EXPECT_100_TIMEOUT_MS, CURLOPTTYPE_LONG)},
  {"CURLOPT_FAILONERROR", std::make_tuple(CURLOPT_FAILONERROR, CURLOPTTYPE_LONG)},
  {"CURLOPT_FILETIME", std::make_tuple(CURLOPT_FILETIME, CURLOPTTYPE_LONG)},
  {"CURLOPT_FOLLOWLOCATION", std::make_tuple(CURLOPT_FOLLOWLOCATION, CURLOPTTYPE_LONG)},
  {"CURLOPT_FORBID_REUSE", std::make_tuple(CURLOPT_FORBID_REUSE, CURLOPTTYPE_LONG)},
  {"CURLOPT_FRESH_CONNECT", std::make_tuple(CURLOPT_FRESH_CONNECT, CURLOPTTYPE_LONG)},
  {"CURLOPT_FTPAPPEND", std::make_tuple(CURLOPT_FTPAPPEND, CURLOPTTYPE_LONG)},
  {"CURLOPT_FTPLISTONLY", std::make_tuple(CURLOPT_FTPLISTONLY, CURLOPTTYPE_LONG)},
  {"CURLOPT_FTPPORT", std::make_tuple(CURLOPT_FTPPORT, CURLOPTTYPE_STRINGPOINT)},
  {"CURLOPT_FTPSSLAUTH", std::make_tuple(CURLOPT_FTPSSLAUTH, CURLOPTTYPE_LONG)},
  {"CURLOPT_FTP_ACCOUNT", std::make_tuple(CURLOPT_FTP_ACCOUNT, CURLOPTTYPE_STRINGPOINT)},
  {"CURLOPT_FTP_ALTERNATIVE_TO_USER", std::make_tuple(CURLOPT_FTP_ALTERNATIVE_TO_USER, CURLOPTTYPE_STRINGPOINT)},
  {"CURLOPT_FTP_CREATE_MISSING_DIRS", std::make_tuple(CURLOPT_FTP_CREATE_MISSING_DIRS, CURLOPTTYPE_LONG)},
  {"CURLOPT_FTP_FILEMETHOD", std::make_tuple(CURLOPT_FTP_FILEMETHOD, CURLOPTTYPE_LONG)},
  {"CURLOPT_FTP_RESPONSE_TIMEOUT", std::make_tuple(CURLOPT_FTP_RESPONSE_TIMEOUT, CURLOPTTYPE_LONG)},
  {"CURLOPT_FTP_SKIP_PASV_IP", std::make_tuple(CURLOPT_FTP_SKIP_PASV_IP, CURLOPTTYPE_LONG)},
  {"CURLOPT_FTP_SSL", std::make_tuple(CURLOPT_FTP_SSL, CURLOPTTYPE_LONG)},
  {"CURLOPT_FTP_SSL_CCC", std::make_tuple(CURLOPT_FTP_SSL_CCC, CURLOPTTYPE_LONG)},
  {"CURLOPT_FTP_USE_EPRT", std::make_tuple(CURLOPT_FTP_USE_EPRT, CURLOPTTYPE_LONG)},
  {"CURLOPT_FTP_USE_EPSV", std::make_tuple(CURLOPT_FTP_USE_EPSV, CURLOPTTYPE_LONG)},
  {"CURLOPT_FTP_USE_PRET", std::make_tuple(CURLOPT_FTP_USE_PRET, CURLOPTTYPE_LONG)},
  {"CURLOPT_HEADER", std::make_tuple(CURLOPT_HEADER, CURLOPTTYPE_LONG)},
  {"CURLOPT_HTTP09_ALLOWED", std::make_tuple(CURLOPT_HTTP09_ALLOWED, CURLOPTTYPE_LONG)},
  {"CURLOPT_HTTPAUTH", std::make_tuple(CURLOPT_HTTPAUTH, CURLOPTTYPE_LONG)},
  {"CURLOPT_HTTPGET", std::make_tuple(CURLOPT_HTTPGET, CURLOPTTYPE_LONG)},
  {"CURLOPT_HTTPPROXYTUNNEL", std::make_tuple(CURLOPT_HTTPPROXYTUNNEL, CURLOPTTYPE_LONG)},
  {"CURLOPT_HTTP_CONTENT_DECODING", std::make_tuple(CURLOPT_HTTP_CONTENT_DECODING, CURLOPTTYPE_LONG)},
  {"CURLOPT_HTTP_TRANSFER_DECODING", std::make_tuple(CURLOPT_HTTP_TRANSFER_DECODING, CURLOPTTYPE_LONG)},
  {"CURLOPT_HTTP_VERSION", std::make_tuple(CURLOPT_HTTP_VERSION, CURLOPTTYPE_LONG)},
  {"CURLOPT_IGNORE_CONTENT_LENGTH", std::make_tuple(CURLOPT_IGNORE_CONTENT_LENGTH, CURLOPTTYPE_LONG)},
  {"CURLOPT_INFILESIZE", std::make_tuple(CURLOPT_INFILESIZE, CURLOPTTYPE_LONG)},
  {"CURLOPT_INTERFACE", std::make_tuple(CURLOPT_INTERFACE, CURLOPTTYPE_STRINGPOINT)},
  {"CURLOPT_IPRESOLVE", std::make_tuple(CURLOPT_IPRESOLVE, CURLOPTTYPE_LONG)},
  {"CURLOPT_ISSUERCERT", std::make_tuple(CURLOPT_ISSUERCERT, CURLOPTTYPE_STRINGPOINT)},
  {"CURLOPT_KEYPASSWD", std::make_tuple(CURLOPT_KEYPASSWD, CURLOPTTYPE_STRINGPOINT)},
  {"CURLOPT_KRB4LEVEL", std::make_tuple(CURLOPT_KRB4LEVEL, CURLOPTTYPE_STRINGPOINT)},
  {"CURLOPT_LOCALPORT", std::make_tuple(CURLOPT_LOCALPORT, CURLOPTTYPE_LONG)},
  {"CURLOPT_LOCALPORTRANGE", std::make_tuple(CURLOPT_LOCALPORTRANGE, CURLOPTTYPE_LONG)},
  {"CURLOPT_LOW_SPEED_LIMIT", std::make_tuple(CURLOPT_LOW_SPEED_LIMIT, CURLOPTTYPE_LONG)},
  {"CURLOPT_LOW_SPEED_TIME", std::make_tuple(CURLOPT_LOW_SPEED_TIME, CURLOPTTYPE_LONG)},
  {"CURLOPT_MAIL_FROM", std::make_tuple(CURLOPT_MAIL_FROM, CURLOPTTYPE_STRINGPOINT)},
  {"CURLOPT_MAXCONNECTS", std::make_tuple(CURLOPT_MAXCONNECTS, CURLOPTTYPE_LONG)},
  {"CURLOPT_MAXFILESIZE", std::make_tuple(CURLOPT_MAXFILESIZE, CURLOPTTYPE_LONG)},
  {"CURLOPT_MAXFILESIZE_LARGE", std::make_tuple(CURLOPT_MAXFILESIZE_LARGE, CURLOPTTYPE_OFF_T)},
  {"CURLOPT_MAXREDIRS", std::make_tuple(CURLOPT_MAXREDIRS, CURLOPTTYPE_LONG)},
  {"CURLOPT_MAX_RECV_SPEED_LARGE", std::make_tuple(CURLOPT_MAX_RECV_SPEED_LARGE, CURLOPTTYPE_OFF_T)},
  {"CURLOPT_MAX_SEND_SPEED_LARGE", std::make_tuple(CURLOPT_MAX_SEND_SPEED_LARGE, CURLOPTTYPE_OFF_T)},
  {"CURLOPT_NETRC", std::make_tuple(CURLOPT_NETRC, CURLOPTTYPE_LONG)},
  {"CURLOPT_NETRC_FILE", std::make_tuple(CURLOPT_NETRC_FILE, CURLOPTTYPE_STRINGPOINT)},
  {"CURLOPT_NEW_DIRECTORY_PERMS", std::make_tuple(CURLOPT_NEW_DIRECTORY_PERMS, CURLOPTTYPE_LONG)},
  {"CURLOPT_NEW_FILE_PERMS", std::make_tuple(CURLOPT_NEW_FILE_PERMS, CURLOPTTYPE_LONG)},
  {"CURLOPT_NOBODY", std::make_tuple(CURLOPT_NOBODY, CURLOPTTYPE_LONG)},
  {"CURLOPT_NOPROGRESS", std::make_tuple(CURLOPT_NOPROGRESS, CURLOPTTYPE_LONG)},
  {"CURLOPT_NOPROXY", std::make_tuple(CURLOPT_NOPROXY, CURLOPTTYPE_STRINGPOINT)},
  {"CURLOPT_NOSIGNAL", std::make_tuple(CURLOPT_NOSIGNAL, CURLOPTTYPE_LONG)},
  {"CURLOPT_PATH_AS_IS", std::make_tuple(CURLOPT_PATH_AS_IS, CURLOPTTYPE_LONG)},
  {"CURLOPT_PINNEDPUBLICKEY", std::make_tuple(CURLOPT_PINNEDPUBLICKEY, CURLOPTTYPE_STRINGPOINT)},
  {"CURLOPT_PIPEWAIT", std::make_tuple(CURLOPT_PIPEWAIT, CURLOPTTYPE_LONG)},
  {"CURLOPT_PORT", std::make_tuple(CURLOPT_PORT, CURLOPTTYPE_LONG)},
  {"CURLOPT_POST", std::make_tuple(CURLOPT_POST, CURLOPTTYPE_LONG)},
  {"CURLOPT_POSTFIELDS", std::make_tuple(CURLOPT_POSTFIELDS, CURLOPTTYPE_STRINGPOINT)},
  {"CURLOPT_POSTFIELDSIZE", std::make_tuple(CURLOPT_POSTFIELDSIZE, CURLOPTTYPE_LONG)},
  {"CURLOPT_POSTFIELDSIZE_LARGE", std::make_tuple(CURLOPT_POSTFIELDSIZE_LARGE, CURLOPTTYPE_OFF_T)},
  {"CURLOPT_PROTOCOLS", std::make_tuple(CURLOPT_PROTOCOLS, CURLOPTTYPE_LONG)},
  {"CURLOPT_PROXY", std::make_tuple(CURLOPT_PROXY, CURLOPTTYPE_STRINGPOINT)},
  {"CURLOPT_PROXYAUTH", std::make_tuple(CURLOPT_PROXYAUTH, CURLOPTTYPE_LONG)},
  {"CURLOPT_PROXYPASSWORD", std::make_tuple(CURLOPT_PROXYPASSWORD, CURLOPTTYPE_STRINGPOINT)},
  {"CURLOPT_PROXYPORT", std::make_tuple(CURLOPT_PROXYPORT, CURLOPTTYPE_LONG)},
  {"CURLOPT_PROXYTYPE", std::make_tuple(CURLOPT_PROXYTYPE, CURLOPTTYPE_LONG)},
  {"CURLOPT_PROXYUSERNAME", std::make_tuple(CURLOPT_PROXYUSERNAME, CURLOPTTYPE_STRINGPOINT)},
  {"CURLOPT_PROXYUSERPWD", std::make_tuple(CURLOPT_PROXYUSERPWD, CURLOPTTYPE_STRINGPOINT)},
  {"CURLOPT_PUT", std::make_tuple(CURLOPT_PUT, CURLOPTTYPE_LONG)},
  {"CURLOPT_RANDOM_FILE", std::make_tuple(CURLOPT_RANDOM_FILE, CURLOPTTYPE_STRINGPOINT)},
  {"CURLOPT_RANGE", std::make_tuple(CURLOPT_RANGE, CURLOPTTYPE_STRINGPOINT)},
  {"CURLOPT_REDIR_PROTOCOLS", std::make_tuple(CURLOPT_REDIR_PROTOCOLS, CURLOPTTYPE_LONG)},
  {"CURLOPT_RESUME_FROM", std::make_tuple(CURLOPT_RESUME_FROM, CURLOPTTYPE_LONG)},
  {"CURLOPT_RESUME_FROM_LARGE", std::make_tuple(CURLOPT_RESUME_FROM_LARGE, CURLOPTTYPE_OFF_T)},
  {"CURLOPT_RTSP_CLIENT_CSEQ", std::make_tuple(CURLOPT_RTSP_CLIENT_CSEQ, CURLOPTTYPE_LONG)},
  {"CURLOPT_RTSP_REQUEST", std::make_tuple(CURLOPT_RTSP_REQUEST, CURLOPTTYPE_LONG)},
  {"CURLOPT_RTSP_SERVER_CSEQ", std::make_tuple(CURLOPT_RTSP_SERVER_CSEQ, CURLOPTTYPE_LONG)},
  {"CURLOPT_RTSP_SESSION_ID", std::make_tuple(CURLOPT_RTSP_SESSION_ID, CURLOPTTYPE_STRINGPOINT)},
  {"CURLOPT_RTSP_STREAM_URI", std::make_tuple(CURLOPT_RTSP_STREAM_URI, CURLOPTTYPE_STRINGPOINT)},
  {"CURLOPT_RTSP_TRANSPORT", std::make_tuple(CURLOPT_RTSP_TRANSPORT, CURLOPTTYPE_STRINGPOINT)},
  {"CURLOPT_SASL_IR", std::make_tuple(CURLOPT_SASL_IR, CURLOPTTYPE_LONG)},
  {"CURLOPT_SOCKS5_GSSAPI_NEC", std::make_tuple(CURLOPT_SOCKS5_GSSAPI_NEC, CURLOPTTYPE_LONG)},
  {"CURLOPT_SOCKS5_GSSAPI_SERVICE", std::make_tuple(CURLOPT_SOCKS5_GSSAPI_SERVICE, CURLOPTTYPE_STRINGPOINT)},
  {"CURLOPT_SSH_AUTH_TYPES", std::make_tuple(CURLOPT_SSH_AUTH_TYPES, CURLOPTTYPE_STRINGPOINT)},
  {"CURLOPT_SSH_COMPRESSION", std::make_tuple(CURLOPT_SSH_COMPRESSION, CURLOPTTYPE_LONG)},
  {"CURLOPT_SSH_HOST_PUBLIC_KEY_MD5", std::make_tuple(CURLOPT_SSH_HOST_PUBLIC_KEY_MD5, CURLOPTTYPE_STRINGPOINT)},
  {"CURLOPT_SSH_KNOWNHOSTS", std::make_tuple(CURLOPT_SSH_KNOWNHOSTS, CURLOPTTYPE_STRINGPOINT)},
  {"CURLOPT_SSH_PRIVATE_KEYFILE", std::make_tuple(CURLOPT_SSH_PRIVATE_KEYFILE, CURLOPTTYPE_STRINGPOINT)},
  {"CURLOPT_SSH_PUBLIC_KEYFILE", std::make_tuple(CURLOPT_SSH_PUBLIC_KEYFILE, CURLOPTTYPE_STRINGPOINT)},
  {"CURLOPT_SSLCERT", std::make_tuple(CURLOPT_SSLCERT, CURLOPTTYPE_STRINGPOINT)},
  {"CURLOPT_SSLCERTTYPE", std::make_tuple(CURLOPT_SSLCERTTYPE, CURLOPTTYPE_STRINGPOINT)},
  {"CURLOPT_SSL_CIPHER_LIST", std::make_tuple(CURLOPT_SSL_CIPHER_LIST, CURLOPTTYPE_STRINGPOINT)},
  {"CURLOPT_SSLENGINE", std::make_tuple(CURLOPT_SSLENGINE, CURLOPTTYPE_STRINGPOINT)},
  {"CURLOPT_SSLENGINE_DEFAULT", std::make_tuple(CURLOPT_SSLENGINE_DEFAULT, CURLOPTTYPE_LONG)},
  {"CURLOPT_SSLKEY", std::make_tuple(CURLOPT_SSLKEY, CURLOPTTYPE_STRINGPOINT)},
  {"CURLOPT_SSLKEYTYPE", std::make_tuple(CURLOPT_SSLKEYTYPE, CURLOPTTYPE_STRINGPOINT)},
  {"CURLOPT_SSLVERSION", std::make_tuple(CURLOPT_SSLVERSION, CURLOPTTYPE_LONG)},
  {"CURLOPT_SSL_OPTIONS", std::make_tuple(CURLOPT_SSL_OPTIONS, CURLOPTTYPE_LONG)},
  {"CURLOPT_SSL_SESSIONID_CACHE", std::make_tuple(CURLOPT_SSL_SESSIONID_CACHE, CURLOPTTYPE_LONG)},
  {"CURLOPT_SSL_VERIFYHOST", std::make_tuple(CURLOPT_SSL_VERIFYHOST, CURLOPTTYPE_LONG)},
  {"CURLOPT_SSL_VERIFYPEER", std::make_tuple(CURLOPT_SSL_VERIFYPEER, CURLOPTTYPE_LONG)},
  {"CURLOPT_SSL_VERIFYSTATUS", std::make_tuple(CURLOPT_SSL_VERIFYSTATUS, CURLOPTTYPE_LONG)},
  {"CURLOPT_TCP_FASTOPEN", std::make_tuple(CURLOPT_TCP_FASTOPEN, CURLOPTTYPE_LONG)},
  {"CURLOPT_TCP_KEEPALIVE", std::make_tuple(CURLOPT_TCP_KEEPALIVE, CURLOPTTYPE_LONG)},
  {"CURLOPT_TCP_KEEPIDLE", std::make_tuple(CURLOPT_TCP_KEEPIDLE, CURLOPTTYPE_LONG)},
  {"CURLOPT_TCP_KEEPINTVL", std::make_tuple(CURLOPT_TCP_KEEPINTVL, CURLOPTTYPE_LONG)},
  {"CURLOPT_TCP_NODELAY", std::make_tuple(CURLOPT_TCP_NODELAY, CURLOPTTYPE_LONG)},
  {"CURLOPT_TFTP_BLKSIZE", std::make_tuple(CURLOPT_TFTP_BLKSIZE, CURLOPTTYPE_LONG)},
  {"CURLOPT_TFTP_NO_OPTIONS", std::make_tuple(CURLOPT_TFTP_NO_OPTIONS, CURLOPTTYPE_LONG)},
  {"CURLOPT_TIMECONDITION", std::make_tuple(CURLOPT_TIMECONDITION, CURLOPTTYPE_LONG)},
  {"CURLOPT_TIMEOUT", std::make_tuple(CURLOPT_TIMEOUT, CURLOPTTYPE_LONG)},
  {"CURLOPT_TIMEOUT_MS", std::make_tuple(CURLOPT_TIMEOUT_MS, CURLOPTTYPE_LONG)},
  {"CURLOPT_TIMEVALUE", std::make_tuple(CURLOPT_TIMEVALUE, CURLOPTTYPE_LONG)},
  {"CURLOPT_TLSAUTH_PASSWORD", std::make_tuple(CURLOPT_TLSAUTH_PASSWORD, CURLOPTTYPE_STRINGPOINT)},
  {"CURLOPT_TLSAUTH_TYPE", std::make_tuple(CURLOPT_TLSAUTH_TYPE, CURLOPTTYPE_STRINGPOINT)},
  {"CURLOPT_TLSAUTH_USERNAME", std::make_tuple(CURLOPT_TLSAUTH_USERNAME, CURLOPTTYPE_STRINGPOINT)},
  {"CURLOPT_TRANSFERTEXT", std::make_tuple(CURLOPT_TRANSFERTEXT, CURLOPTTYPE_LONG)},
  {"CURLOPT_UNIX_SOCKET_PATH", std::make_tuple(CURLOPT_UNIX_SOCKET_PATH, CURLOPTTYPE_STRINGPOINT)},
  {"CURLOPT_UNRESTRICTED_AUTH", std::make_tuple(CURLOPT_UNRESTRICTED_AUTH, CURLOPTTYPE_LONG)},
  {"CURLOPT_UPLOAD", std::make_tuple(CURLOPT_UPLOAD, CURLOPTTYPE_LONG)},
  {"CURLOPT_URL", std::make_tuple(CURLOPT_URL, CURLOPTTYPE_STRINGPOINT)},
  {"CURLOPT_USERAGENT", std::make_tuple(CURLOPT_USERAGENT, CURLOPTTYPE_STRINGPOINT)},
  {"CURLOPT_USERNAME", std::make_tuple(CURLOPT_USERNAME, CURLOPTTYPE_STRINGPOINT)},
  {"CURLOPT_USERPWD", std::make_tuple(CURLOPT_USERPWD, CURLOPTTYPE_STRINGPOINT)},
  {"CURLOPT_USE_SSL", std::make_tuple(CURLOPT_USE_SSL, CURLOPTTYPE_LONG)},
  {"CURLOPT_VERBOSE", std::make_tuple(CURLOPT_VERBOSE, CURLOPTTYPE_LONG)},
  {"CURLOPT_WILDCARDMATCH", std::make_tuple(CURLOPT_WILDCARDMATCH, CURLOPTTYPE_LONG)}
};

// define a udf management class to configure functions
class udf_manager {
  typedef std::list<std::string> string_list;

  private:
    string_list set;

  public:
    ~udf_manager() { 
      unregister_all_functions(); 
    }

    // register given function
    bool register_function(const char *func_name, enum Item_result return_type, Udf_func_any func, Udf_func_init init_func = NULL, Udf_func_deinit deinit_func = NULL) {
      if (!mysql_service_udf_registration->udf_register(func_name, return_type, func, init_func, deinit_func)) {
        set.push_back(func_name);

        // registration is successful
        return true;
      }
      
      return false;
    }

    // unregister all previously registered functions in this component
    bool unregister_all_functions() {
      string_list delete_set;
      
      for (auto udf : set) {
        int was_present = 0;
        if (!mysql_service_udf_registration->udf_unregister(udf.c_str(), &was_present) || !was_present) {
          delete_set.push_back(udf);
        }
      }

      for (auto udf : delete_set) {
        set.remove(udf);
      }

      // successful if list of functions is empty
      if (set.empty()) {
        return true;
      }

      return false;
    }

};
udf_manager *my_udf_manager;

namespace udf_impl {
  const char *udf_init = "udf_init", *my_udf = "my_udf";

  thread_local bool nowait = false;

  // initialize any given function
  static bool httpclient_udf_init(UDF_INIT *initid, UDF_ARGS *, char *) {
    const char* name = "utf8mb4";
    char *value = const_cast<char*>(name);
    initid->ptr = const_cast<char *>(udf_init);
    if (mysql_service_mysql_udf_metadata->result_set(initid, "charset", const_cast<char *>(value))) {
      LogComponentErr(ERROR_LEVEL, ER_LOG_PRINTF_MSG, "failed to set result charset");
      return false;
    }
    return 0;
  }

  // initialize any given function
  static void httpclient_udf_deinit(__attribute__((unused)) UDF_INIT *initid) {
    assert(initid->ptr == udf_impl::udf_init || initid->ptr == udf_impl::my_udf);
  }

  // check if privilege is granted for current user
  bool has_privilege(void *opaque_thd) {
    // get the security context of the thread
    Security_context_handle ctx = nullptr;
    if (mysql_service_mysql_thd_security_context->get(opaque_thd, &ctx) || !ctx) {
      LogComponentErr(ERROR_LEVEL, ER_LOG_PRINTF_MSG, "problem trying to get security context");
      return false;
    }

    if (mysql_service_global_grants_check->has_global_grant(ctx, HTTPCLIENT_PRIVILEGE_NAME, strlen(HTTPCLIENT_PRIVILEGE_NAME))) {
      return true;
    }

    return false;
  }

  // curl write function
  size_t write_callback(void *contents, size_t size, size_t nmemb, void *userp) {
    size_t total_size = size * nmemb;
    std::string *response = static_cast<std::string *>(userp);
    response->append(static_cast<const char *>(contents), total_size);
    return total_size;
  }

  // perform curl request with given options
  const char *httpclient_request_udf(UDF_INIT *initid, UDF_ARGS *args, char *outp, unsigned long *length, char *is_null, char *error) {
    MYSQL_THD thd;
    mysql_service_mysql_current_thread_reader->get(&thd);

    if (!has_privilege(thd)) {
      mysql_error_service_printf(ER_SPECIFIC_ACCESS_DENIED_ERROR, 0, HTTPCLIENT_PRIVILEGE_NAME);
      *error = 1;
      *is_null = 1;
      return 0;
    }

    auto arg_count = args->arg_count;
    if (arg_count < 2) {
      mysql_error_service_printf(ER_AUDIT_LOG_UDF_INVALID_ARGUMENT_COUNT, 0);
      *error = 1;
      *is_null = 1;
      return 0;
    }

    const char *method = args->args[0];
    const char *url = args->args[1];
    const char *body = arg_count > 2 ? args->args[2] : nullptr;
    const char *headers = arg_count > 3 ? args->args[3] : nullptr;
    const char *curl_options = arg_count > 4 ? args->args[4] : nullptr;
    const bool fire_and_forget = nowait;
    nowait = false;

    CURL *curl;
    long http_status_code = -1;

    try {
      curl = curl_easy_init();
      if (!curl) {
        throw "curl init failed";
      }

      if (fire_and_forget) {
        curl_easy_setopt(curl, CURLOPT_TIMEOUT_MS, 1);
      }

      // set all given curl options
      if (curl_options != nullptr && strcmp(curl_options, "") != 0) {
        json curl_options_json = json::parse(curl_options);

        for (auto& item : curl_options_json.items())
        {
          std::string opt_name = item.key();

          auto it = curl_options_available.find(opt_name);
          if (it == curl_options_available.end()) {
            continue;
          }

          CURLoption opt_constant = std::get<0>(curl_options_available[opt_name]);
          long opt_type = std::get<1>(curl_options_available[opt_name]);
          auto opt_type_str = std::to_string(opt_type);

          if (opt_type == CURLOPTTYPE_LONG) {
            auto value = item.value().get<long>();
            auto value_str = std::to_string(value);
            curl_easy_setopt(curl, opt_constant, value);
          }
          else if (opt_type == CURLOPTTYPE_STRINGPOINT) {
            auto value = item.value().get<std::string>();
            curl_easy_setopt(curl, opt_constant, value);
          }
        }
      }

      // set all given headers
      if (headers != nullptr && strcmp(headers, "") != 0) {
        struct curl_slist* header_list = NULL;
        json headers_json = json::parse(headers);

        for (auto& item : headers_json.items())
        {
          auto key = item.key();
          auto value = item.value().get<std::string>();
          auto header = key + ": " + value;
          auto header_chr = header.c_str();
          header_list = curl_slist_append(header_list, header_chr);
        }
        
        if (header_list != nullptr) {
          curl_easy_setopt(curl, CURLOPT_HTTPHEADER, header_list);
        }
      }

      // set request method and url
      curl_easy_setopt(curl, CURLOPT_URL, url);

      std::string method_upper = method;
      std::transform(method_upper.begin(), method_upper.end(), method_upper.begin(), ::toupper);

      if (method_upper == "POST") {
        curl_easy_setopt(curl, CURLOPT_POST, 1);
      }
      else if (method_upper == "PUT" || method_upper == "PATCH" || method_upper == "DELETE") {
        curl_easy_setopt(curl, CURLOPT_CUSTOMREQUEST, method_upper);
      }
      else {
        curl_easy_setopt(curl, CURLOPT_HTTPGET, 1);
        curl_easy_setopt(curl, CURLOPT_CUSTOMREQUEST, "GET");
      }

      if (body != nullptr) {
        curl_easy_setopt(curl, CURLOPT_POSTFIELDS, body);
      }

      // set the callback function for writing the response data
      std::string response;
      curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, write_callback);
      curl_easy_setopt(curl, CURLOPT_WRITEDATA, &response);
      
      // send the request
      auto start_time = std::chrono::steady_clock::now();
      CURLcode res = curl_easy_perform(curl);
      auto end_time = std::chrono::steady_clock::now();

      curl_easy_getinfo(curl, CURLINFO_RESPONSE_CODE, &http_status_code);
      
      curl_easy_cleanup(curl);

      // log to mysql error log
      if (res != CURLE_OK) {
        auto http_error_message = curl_easy_strerror(res);
        std::string msg = std::string(method) + " " + std::string(url) + " failed with error code " + std::to_string(http_status_code) + ": " + std::string(http_error_message);
        throw std::runtime_error(msg);
      }
      
      std::string msg = std::string(method) + " " + std::string(url) + " returned status code " + std::to_string(http_status_code);
      LogComponentErr(INFORMATION_LEVEL, ER_LOG_PRINTF_MSG, msg.c_str());

      auto elapsed_time = std::chrono::duration_cast<std::chrono::milliseconds>(end_time - start_time).count();
      time_spent_ms += elapsed_time;
      number_of_requests++;

      // since message size is possibly bigger than 255, "outp" buffer is not usable (e.g. memcpy(outp, response.c_str()))
      // we must a dynamically allocated buffer defined in init function
      initid->ptr = strdup(response.c_str());
      *length = response.size();
    }
    catch (const std::exception& ex) {
      initid->ptr = NULL;

      auto msg = std::string(ex.what());
      if (fire_and_forget && msg.find("Timeout") != std::string::npos) {
        // bypass timeout error to allow fire and forget
      }
      else {
        LogComponentErr(ERROR_LEVEL, ER_LOG_PRINTF_MSG, msg.c_str());
        mysql_error_service_printf(ER_GET_ERRMSG, 0, http_status_code, msg.c_str(), "curl request");
        *error = 1;
        *is_null = 1;
      }
    }

    return initid->ptr;
  }

  // perform curl request and do not wait for response
  const char *httpclient_request_nowait_udf(UDF_INIT *initid, UDF_ARGS *args, char *outp, unsigned long *length, char *is_null, char *error) {
    nowait = true;
    return httpclient_request_udf(initid, args, outp, length, is_null, error);
  }
}

// initialize the component
static mysql_service_status_t httpclient_service_init() {
  mysql_service_status_t result = 0;

  log_bi = mysql_service_log_builtins;
  log_bs = mysql_service_log_builtins_string;

  LogComponentErr(INFORMATION_LEVEL, ER_LOG_PRINTF_MSG, "started initializing the component");

  // register custom status variables
  if (mysql_service_status_variable_registration->register_variable((SHOW_VAR *)&httpclient_status_variables)) {
    LogComponentErr(ERROR_LEVEL, ER_LOG_PRINTF_MSG, "failed to register status variable(s)");
  }
  else {
    LogComponentErr(INFORMATION_LEVEL, ER_LOG_PRINTF_MSG, "status variable(s) registered");
  }

  // register a custom privilege
  if (mysql_service_dynamic_privilege_register->register_privilege(HTTPCLIENT_PRIVILEGE_NAME, strlen(HTTPCLIENT_PRIVILEGE_NAME))) {
    LogComponentErr(ERROR_LEVEL, ER_LOG_PRINTF_MSG, "failed to register privilege 'HTTP_CLIENT'");
    result = 1;
  } 
  else {
    LogComponentErr(INFORMATION_LEVEL, ER_LOG_PRINTF_MSG, "new privilege 'HTTP_CLIENT' has been registered successfully");
  }

  // register user defined functions
  my_udf_manager = new udf_manager();
  if (!my_udf_manager->register_function("http_request", Item_result::STRING_RESULT, (Udf_func_any)udf_impl::httpclient_request_udf, udf_impl::httpclient_udf_init, udf_impl::httpclient_udf_deinit)) {
    // failed to register udf
    delete my_udf_manager;
    return 1;
  }

  if (!my_udf_manager->register_function("http_request_nowait", Item_result::STRING_RESULT, (Udf_func_any)udf_impl::httpclient_request_nowait_udf, udf_impl::httpclient_udf_init, udf_impl::httpclient_udf_deinit)) {
    // failed to register udf
    delete my_udf_manager;
    return 1;
  }

  return result;
}

// de-initialize the component and cleanup
static mysql_service_status_t httpclient_service_deinit() {
  mysql_service_status_t deinit_result = 0;

  // unregister custom status variables
  if (mysql_service_status_variable_registration->unregister_variable((SHOW_VAR *)&httpclient_status_variables)) {
    LogComponentErr(ERROR_LEVEL, ER_LOG_PRINTF_MSG, "failed to unregister status variable(s)");
  }
  else {
    LogComponentErr(INFORMATION_LEVEL, ER_LOG_PRINTF_MSG, "status variable(s) unregistered");
  }

  // unregister custom privileges
  if (mysql_service_dynamic_privilege_register->unregister_privilege(HTTPCLIENT_PRIVILEGE_NAME, strlen(HTTPCLIENT_PRIVILEGE_NAME))) {
    LogComponentErr(ERROR_LEVEL, ER_LOG_PRINTF_MSG, "failed to unregister privilege 'HTTP_CLIENT'");
    deinit_result = 1;
  } 
  else {
    LogComponentErr(INFORMATION_LEVEL, ER_LOG_PRINTF_MSG, "privilege 'HTTP_CLIENT' has been unregistered successfully");
  }

  // unregister all user defined functions
  if (!my_udf_manager->unregister_all_functions()) {
    // failed to unregister some functions
    return 1;
  }

  delete my_udf_manager;

  LogComponentErr(INFORMATION_LEVEL, ER_LOG_PRINTF_MSG, "component is now fully uninstalled");

  return deinit_result;
}

// declare list of services defined
BEGIN_COMPONENT_PROVIDES(httpclient_service)
END_COMPONENT_PROVIDES();

// declare which services are required for this component
BEGIN_COMPONENT_REQUIRES(httpclient_service)
  REQUIRES_SERVICE(log_builtins),
  REQUIRES_SERVICE(log_builtins_string),
  REQUIRES_SERVICE(dynamic_privilege_register),
  REQUIRES_SERVICE(mysql_udf_metadata),
  REQUIRES_SERVICE(udf_registration),
  REQUIRES_SERVICE(mysql_thd_security_context),
  REQUIRES_SERVICE(global_grants_check),
  REQUIRES_SERVICE(mysql_current_thread_reader),
  REQUIRES_SERVICE(mysql_runtime_error),
  REQUIRES_SERVICE(status_variable_registration),
END_COMPONENT_REQUIRES();

// declare component metadata
BEGIN_COMPONENT_METADATA(httpclient_service)
  METADATA("mysql.author", "Arda Beyazoglu"),
  METADATA("mysql.dev", "ardabeyazoglu"),
  METADATA("mysql.license", "MIT"), 
END_COMPONENT_METADATA();

DECLARE_COMPONENT(httpclient_service, "mysql:httpclient_service")
  httpclient_service_init,
  httpclient_service_deinit 
END_DECLARE_COMPONENT();

// declare list of components defined
DECLARE_LIBRARY_COMPONENTS 
  &COMPONENT_REF(httpclient_service)
END_DECLARE_LIBRARY_COMPONENTS