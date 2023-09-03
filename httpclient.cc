#define LOG_COMPONENT_TAG "httpclient"
#define NO_SIGNATURE_CHANGE 0
#define SIGNATURE_CHANGE 1

#include <components/httpclient/httpclient.h>

REQUIRES_SERVICE_PLACEHOLDER(log_builtins);
REQUIRES_SERVICE_PLACEHOLDER(log_builtins_string);
REQUIRES_SERVICE_PLACEHOLDER(dynamic_privilege_register);
REQUIRES_SERVICE_PLACEHOLDER(udf_registration);
REQUIRES_SERVICE_PLACEHOLDER(mysql_udf_metadata);
REQUIRES_SERVICE_PLACEHOLDER(mysql_thd_security_context);
REQUIRES_SERVICE_PLACEHOLDER(global_grants_check);
REQUIRES_SERVICE_PLACEHOLDER(mysql_current_thread_reader);
REQUIRES_SERVICE_PLACEHOLDER(mysql_runtime_error);
REQUIRES_SERVICE_PLACEHOLDER(status_variable_registration);
REQUIRES_SERVICE_PLACEHOLDER(component_sys_variable_register);

SERVICE_TYPE(log_builtins) * log_bi;
SERVICE_TYPE(log_builtins_string) * log_bs;

static const char *HTTPCLIENT_PRIVILEGE_NAME = "HTTP_CLIENT";

static unsigned int time_spent = 0;

static SHOW_VAR httpclient_status_variables[] = {
  {"httpclient.time_spent", (char *)&time_spent, SHOW_INT, SHOW_SCOPE_GLOBAL},
  //{"httpclient.clamav_engine_version", (char *)&clamav_version, SHOW_CHAR, SHOW_SCOPE_GLOBAL},
  //{"httpclient.virus_found", (char *)&virusfound_status, SHOW_INT, SHOW_SCOPE_GLOBAL},
   {nullptr, nullptr, SHOW_LONG, SHOW_SCOPE_GLOBAL}
};

class udf_list {
  typedef std::list<std::string> udf_list_t;

  private:
    udf_list_t set;

  public:
    ~udf_list() { 
      unregister(); 
    }

    bool add_scalar(const char *func_name, enum Item_result return_type, Udf_func_any func, Udf_func_init init_func = NULL, Udf_func_deinit deinit_func = NULL) {
      if (!mysql_service_udf_registration->udf_register(func_name, return_type, func, init_func, deinit_func)) {
        set.push_back(func_name);
        return false;
      }
      return true;
    }

    bool unregister() {
      udf_list_t delete_set;
      
      /* try to unregister all of the udfs */
      for (auto udf : set) {
        int was_present = 0;
        if (!mysql_service_udf_registration->udf_unregister(udf.c_str(), &was_present) || !was_present)
          delete_set.push_back(udf);
      }

      /* remove the unregistered ones from the list */
      for (auto udf : delete_set) set.remove(udf);

      /* success: empty set */
      if (set.empty()) return false;

      /* failure: entries still in the set */
      return true;
    }

} * list;

int register_status_variables() {
  if (mysql_service_status_variable_registration->register_variable((SHOW_VAR *)&httpclient_status_variables)) {
    LogComponentErr(ERROR_LEVEL, ER_LOG_PRINTF_MSG, "Failed to register status variable");
    return 1;
  }
  LogComponentErr(INFORMATION_LEVEL, ER_LOG_PRINTF_MSG, "Status variable(s) registered");
  return 0;
}

int unregister_status_variables() {
  if (mysql_service_status_variable_registration->unregister_variable((SHOW_VAR *)&httpclient_status_variables)) {
    LogComponentErr(ERROR_LEVEL, ER_LOG_PRINTF_MSG, "Failed to unregister status variable");
    return 1;
  }
  LogComponentErr(INFORMATION_LEVEL, ER_LOG_PRINTF_MSG, "Status variable(s) unregistered");
  return 0;
}

namespace udf_impl {
  const char *udf_init = "udf_init", *my_udf = "my_udf", *my_udf_clear = "my_clear", *my_udf_add = "my_udf_add";

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

  static void httpclient_udf_deinit(__attribute__((unused)) UDF_INIT *initid) {
    assert(initid->ptr == udf_init || initid->ptr == my_udf);
  }

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

  size_t write_callback(void *contents, size_t size, size_t nmemb, void *userp) {
    size_t total_size = size * nmemb;
    std::string *response = static_cast<std::string *>(userp);
    response->append(static_cast<const char *>(contents), total_size);
    return total_size;
  }

  bool perform_http_get(const char *url, std::string &response) {
    CURL *curl = curl_easy_init();
    if (!curl) {
        return false;
    }

    // Set the URL for the GET request
    curl_easy_setopt(curl, CURLOPT_URL, url);

    // Set the callback function for writing the response data
    curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, write_callback);
    curl_easy_setopt(curl, CURLOPT_WRITEDATA, &response);
    
    // Perform the GET request
    CURLcode res = curl_easy_perform(curl);
    
    // Clean up and finalize the request
    curl_easy_cleanup(curl);

    time_spent++;

    return res == CURLE_OK;
  }

  const char *httpclient_request_udf(UDF_INIT *initid, UDF_ARGS *args, char *outp, unsigned long *length, char *is_null, char *error) {
    MYSQL_THD thd;
    mysql_service_mysql_current_thread_reader->get(&thd);

    if (!has_privilege(thd)) {
       mysql_error_service_printf(ER_SPECIFIC_ACCESS_DENIED_ERROR, 0, HTTPCLIENT_PRIVILEGE_NAME);
       *error = 1;
       *is_null = 1;
       return 0;
    }

    // Prepare the URL for the HTTP request
    const char *url = args->args[0];
    
    // Perform the HTTP request
    // Note: This is a simplified example. In a production scenario, consider using a proper HTTP library.
    std::string response;
    if (perform_http_get(url, response)) {
      // Copy the response to the output buffer
      strncpy(outp, response.c_str(), response.size());
      *length = response.size();
    } 
    else {
      // If the request fails, return an error message
      const char *error_message = "HTTP request failed";
      strncpy(outp, error_message, strlen(error_message));
      *length = strlen(error_message);
    }
    
    return outp;
  }
}

static mysql_service_status_t httpclient_service_init() {
  mysql_service_status_t result = 0;

  log_bi = mysql_service_log_builtins;
  log_bs = mysql_service_log_builtins_string;

  LogComponentErr(INFORMATION_LEVEL, ER_LOG_PRINTF_MSG, "initializing…");

  register_status_variables();

  // Registration of the privilege
  if (mysql_service_dynamic_privilege_register->register_privilege(HTTPCLIENT_PRIVILEGE_NAME, strlen(HTTPCLIENT_PRIVILEGE_NAME))) {
    LogComponentErr(ERROR_LEVEL, ER_LOG_PRINTF_MSG, "could not register privilege 'HTTP_CLIENT'.");
    result = 1;
  } 
  else {
    LogComponentErr(INFORMATION_LEVEL, ER_LOG_PRINTF_MSG, "new privilege 'HTTP_CLIENT' has been registered successfully.");
  }

  list = new udf_list();

  if (list->add_scalar("http_request", Item_result::STRING_RESULT, (Udf_func_any)udf_impl::httpclient_request_udf, udf_impl::httpclient_udf_init, udf_impl::httpclient_udf_deinit)) {
    delete list;
    // failure: UDF registration failed
    return 1;
  }

  return result;
}

static mysql_service_status_t httpclient_service_deinit() {
  mysql_service_status_t result = 0;

  unregister_status_variables();

  if (mysql_service_dynamic_privilege_register->unregister_privilege(HTTPCLIENT_PRIVILEGE_NAME, strlen(HTTPCLIENT_PRIVILEGE_NAME))) {
    LogComponentErr(ERROR_LEVEL, ER_LOG_PRINTF_MSG, "could not unregister privilege 'HTTP_CLIENT'.");
    result = 1;
  } 
  else {
    LogComponentErr(INFORMATION_LEVEL, ER_LOG_PRINTF_MSG, "privilege 'HTTP_CLIENT' has been unregistered successfully.");
  }

  // failure: some UDFs still in use
  if (list->unregister()) {
    return 1;
  }

  delete list;

  LogComponentErr(INFORMATION_LEVEL, ER_LOG_PRINTF_MSG, "uninstalled.");

  return result;
}

BEGIN_COMPONENT_PROVIDES(httpclient_service)
END_COMPONENT_PROVIDES();

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
  REQUIRES_SERVICE(component_sys_variable_register),
END_COMPONENT_REQUIRES();

// A list of metadata to describe the Component.
BEGIN_COMPONENT_METADATA(httpclient_service)
  METADATA("mysql.author", "Arda Beyazoglu"),
  METADATA("mysql.dev", "ardabeyazoglu"),
  METADATA("mysql.license", "MIT"), 
END_COMPONENT_METADATA();

// Declaration of the Component.
DECLARE_COMPONENT(httpclient_service, "mysql:httpclient_service")
  httpclient_service_init,
  httpclient_service_deinit END_DECLARE_COMPONENT();

/* 
Defines list of Components contained in this library. Note that for now
we assume that library will have exactly one Component. 
*/
DECLARE_LIBRARY_COMPONENTS &COMPONENT_REF(httpclient_service)
  END_DECLARE_LIBRARY_COMPONENTS