#define LOG_COMPONENT_TAG "httpclient"
#define NO_SIGNATURE_CHANGE 0
#define SIGNATURE_CHANGE 1

#include <iostream>
#include <components/httpclient/httpclient.h>

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
REQUIRES_SERVICE_PLACEHOLDER(component_sys_variable_register);
REQUIRES_SERVICE_PLACEHOLDER(component_sys_variable_unregister);
REQUIRES_SERVICE_PLACEHOLDER(system_variable_source);

SERVICE_TYPE(log_builtins) * log_bi;
SERVICE_TYPE(log_builtins_string) * log_bs;

static const char *HTTPCLIENT_PRIVILEGE_NAME = "HTTP_CLIENT";

// keep a global status variable for time spent in http requests
static unsigned long time_spent = 0;

static SHOW_VAR httpclient_status_variables[] = {
  {"httpclient.time_spent", (char *)&time_spent, SHOW_LONG, SHOW_SCOPE_GLOBAL},
   {nullptr, nullptr, SHOW_LONG, SHOW_SCOPE_GLOBAL}
};

// configure curl options using mysql variables
static long var_curl_timeout_ms = 30000;

thread_local static long var_curl_followlocation = 1;
static long global_var_curl_followlocation = 1;

// Status of registration of the system variable. Note that there should
// be multiple such flags, if more system variables are intoduced, so
// that we can keep track of the register/unregister status for each
// variable.
static std::atomic<bool> httpclient_component_sys_var_registered{false};

// udf configuration
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

} * my_udf_list;

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

long get_global_followlocation() {
  size_t var_len = 2;
  char *buffer = new char[var_len];

  int ret = mysql_service_component_sys_variable_register->get_variable(
    LOG_COMPONENT_TAG, 
    "curlopt_followlocation", 
    (void **)&buffer, 
    &var_len
  );
  if (ret != 0) {
    LogComponentErr(ERROR_LEVEL, ER_LOG_PRINTF_MSG, "get_variable failed");
    return global_var_curl_followlocation;
  }

  global_var_curl_followlocation = std::stol(buffer);

  std::stringstream ss_var_ptr;
  ss_var_ptr << std::hex << reinterpret_cast<uintptr_t>(&global_var_curl_followlocation);
  auto msg = "global buffer: 0x" + ss_var_ptr.str();
  LogComponentErr(INFORMATION_LEVEL, ER_LOG_PRINTF_MSG, msg.c_str());
  
  return global_var_curl_followlocation;
}

static void update_variable_followlocation(MYSQL_THD thd [[maybe_unused]], SYS_VAR *self [[maybe_unused]], void *var_ptr, const void *save) {
  LogComponentErr(INFORMATION_LEVEL, ER_LOG_PRINTF_MSG, "update_variable_followlocation executed");

  /*
  std::stringstream ss_var_ptr, ss_var_curl_followlocation, ss_global_var_curl_followlocation;
  ss_var_ptr << std::hex << reinterpret_cast<uintptr_t>(var_ptr);
  ss_var_curl_followlocation << std::hex << reinterpret_cast<uintptr_t>(&var_curl_followlocation);
  ss_global_var_curl_followlocation << std::hex << reinterpret_cast<uintptr_t>(&global_var_curl_followlocation);
  auto msg = "var_ptr: 0x" + ss_var_ptr.str() + ", var_curl_followlocation: 0x" + ss_var_curl_followlocation.str() + ", global_var_curl_followlocation: 0x" + ss_global_var_curl_followlocation.str();
  LogComponentErr(INFORMATION_LEVEL, ER_LOG_PRINTF_MSG, msg.c_str());
  */

  const long new_val = *(static_cast<const long *>(save));
  LogComponentErr(INFORMATION_LEVEL, ER_LOG_PRINTF_MSG, "new httpclient.followlocation");

  auto x = std::to_string(new_val);
  LogComponentErr(INFORMATION_LEVEL, ER_LOG_PRINTF_MSG, x.c_str());

  *(static_cast<long *>(var_ptr)) = new_val;

  var_curl_followlocation = new_val;
}

/**
  Register the server system variables defined by this component.

  @return Status
  @retval false success
  @retval true failure
*/
static bool register_system_variables() {
  if (httpclient_component_sys_var_registered) {
    // System variable is already registered.
    return (false);
  }

  // defined different scopes for each variable otherwise INTEGRAL_CHECK_ARG produces error for same type
  {
    INTEGRAL_CHECK_ARG(ulong) timeout_ms_arg;
    timeout_ms_arg.def_val = 30000;
    timeout_ms_arg.min_val = 0;
    timeout_ms_arg.max_val = 300000;
    timeout_ms_arg.blk_sz = 0;

    // register curl timeout variable
    if (mysql_service_component_sys_variable_register->register_variable(
          "httpclient", "curlopt_timeout_ms",
          PLUGIN_VAR_LONG | PLUGIN_VAR_MEMALLOC | PLUGIN_VAR_NOPERSIST,
          "curl request timeout in milliseconds", 
          nullptr,
          nullptr, 
          (void *)&timeout_ms_arg,
          (void *)&var_curl_timeout_ms
        )
    ) {
      LogEvent()
          .type(LOG_TYPE_ERROR)
          .prio(ERROR_LEVEL)
          .lookup(ER_LOG_PRINTF_MSG, "httpclient.curlopt_timeout_ms register failed.");
      
      return (true);
    }
  }

  {
    INTEGRAL_CHECK_ARG(ulong) follow_location_arg;
    follow_location_arg.def_val = 1;
    follow_location_arg.min_val = 0;
    follow_location_arg.max_val = 1;
    follow_location_arg.blk_sz = 0;

    // register follow redirect variable
    if (mysql_service_component_sys_variable_register->register_variable(
          "httpclient", "curlopt_followlocation",
          PLUGIN_VAR_LONG | PLUGIN_VAR_MEMALLOC | PLUGIN_VAR_NOPERSIST | PLUGIN_VAR_THDLOCAL,
          "curl request follow redirects", 
          nullptr,
          update_variable_followlocation, 
          (void *)&follow_location_arg,
          //(void *)&var_curl_followlocation
          nullptr
        )
    ) {
      std::string msg{};
      LogEvent()
          .type(LOG_TYPE_ERROR)
          .prio(ERROR_LEVEL)
          .lookup(ER_LOG_PRINTF_MSG, "httpclient.curlopt_followlocation register failed.");
      
      return (true);
    }
  }

  // System variable is registered successfully.
  httpclient_component_sys_var_registered = true;

  LogComponentErr(INFORMATION_LEVEL, ER_LOG_PRINTF_MSG, "System variables has been registered successfully.");

  return (false);
}

/**
  Unregister the server system variables defined by this component.

  @return Status
  @retval false success
  @retval true failure
*/
static bool unregister_system_variables() {
  if (mysql_service_component_sys_variable_unregister->unregister_variable("httpclient", "curlopt_timeout_ms")) {
    if (!httpclient_component_sys_var_registered) {
      return (false);
    }

    LogEvent()
        .type(LOG_TYPE_ERROR)
        .prio(ERROR_LEVEL)
        .lookup(ER_LOG_PRINTF_MSG, "httpclient.curlopt_timeout_ms unregister failed.");
    return (true);
  }

  if (mysql_service_component_sys_variable_unregister->unregister_variable("httpclient", "curlopt_followlocation")) {
    if (!httpclient_component_sys_var_registered) {
      return (false);
    }

    LogEvent()
        .type(LOG_TYPE_ERROR)
        .prio(ERROR_LEVEL)
        .lookup(ER_LOG_PRINTF_MSG, "httpclient.curlopt_followlocation unregister failed.");
    return (true);
  }

  // system variables are un-registered
  httpclient_component_sys_var_registered = false;

  return (false);
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
    assert(initid->ptr == udf_impl::udf_init || initid->ptr == udf_impl::my_udf);
    free(initid->ptr);
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

  bool perform_curl_request(std::string &response, const char *method, const char *url, const char *data = nullptr, const char* content_type = "application/x-www-form-urlencoded") {
    CURL *curl = curl_easy_init();
    if (!curl) {
      return false;
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

    curl_easy_setopt(curl, CURLOPT_POSTFIELDS, data);

    // set headers
    struct curl_slist *headers = NULL;
    std::string header = "Content-Type: " + std::string(content_type);
    headers = curl_slist_append(headers, header.c_str());
    curl_easy_setopt(curl, CURLOPT_HTTPHEADER, headers);

    // set other defined curl options
    curl_easy_setopt(curl, CURLOPT_TIMEOUT_MS, var_curl_timeout_ms);
    curl_easy_setopt(curl, CURLOPT_FOLLOWLOCATION, var_curl_followlocation);

    auto x = std::to_string(var_curl_followlocation);
    LogComponentErr(WARNING_LEVEL, ER_LOG_PRINTF_MSG, "var_curl_followlocation");
    LogComponentErr(WARNING_LEVEL, ER_LOG_PRINTF_MSG, x.c_str());

    // set the callback function for writing the response data
    curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, write_callback);
    curl_easy_setopt(curl, CURLOPT_WRITEDATA, &response);
    
    // send the request
    auto start_time = std::chrono::steady_clock::now();
    CURLcode res = curl_easy_perform(curl);
    auto end_time = std::chrono::steady_clock::now();

    long http_status_code;
    const char *http_error_message;

    curl_easy_getinfo(curl, CURLINFO_RESPONSE_CODE, &http_status_code);
    if (res != CURLE_OK) {
      http_error_message = curl_easy_strerror(res);
    }
    
    curl_easy_cleanup(curl);

    // log to mysql error log
    if (res != CURLE_OK) {
      std::string msg = std::string(method) + " " + std::string(url) + " failed with error code " + std::to_string(http_status_code) + ": " + std::string(http_error_message);
      LogComponentErr(WARNING_LEVEL, ER_LOG_PRINTF_MSG, msg.c_str());
      response.assign("Error Code=" + std::to_string(http_status_code) + "; Message=" + std::string(http_error_message));
    }
    else {
      std::string msg = std::string(method) + " " + std::string(url) + " returned status code " + std::to_string(http_status_code);
      LogComponentErr(INFORMATION_LEVEL, ER_LOG_PRINTF_MSG, msg.c_str());
    }

    auto elapsed_time = std::chrono::duration_cast<std::chrono::milliseconds>(end_time - start_time).count();
    time_spent += elapsed_time;

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

    const char *request_method = args->args[0];
    const char *request_url = args->args[1];
    const char *request_body = args->args[2];
    const char *request_content_type = args->args[3];

    std::string response;
    if (perform_curl_request(response, request_method, request_url, request_body, request_content_type)) {
      // Copy the response to the output buffer
      initid->ptr = strdup(response.c_str());
      *length = response.size();
    } 
    else {
      // If the request fails, return an error message
      auto error_message = "HTTP request failed: " + response;
      initid->ptr = strdup(error_message.c_str());
      *length = error_message.size();
    }
    
    return initid->ptr;
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

  my_udf_list = new udf_list();

  if (my_udf_list->add_scalar("http_request", Item_result::STRING_RESULT, (Udf_func_any)udf_impl::httpclient_request_udf, udf_impl::httpclient_udf_init, udf_impl::httpclient_udf_deinit)) {
    // failure: UDF registration failed
    delete my_udf_list;
    return 1;
  }

  register_system_variables();

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
  if (my_udf_list->unregister()) {
    return 1;
  }

  delete my_udf_list;

  unregister_system_variables();

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
  REQUIRES_SERVICE(component_sys_variable_unregister),
  REQUIRES_SERVICE(system_variable_source),
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