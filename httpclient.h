// define mysql component name
#define LOG_COMPONENT_TAG "httpclient"

// include component services neccessary for this plugin
#include <mysqld_error.h>
#include <mysql/components/component_implementation.h>
#include <mysql/components/services/log_builtins.h>
#include <mysql/components/services/dynamic_privilege.h>
#include <mysql/components/services/udf_metadata.h>
#include <mysql/components/services/udf_registration.h>
#include <mysql/components/services/security_context.h>
#include <mysql/components/services/mysql_current_thread_reader.h>
#include <mysql/components/services/mysql_thd_store_service.h>
#include <mysql/components/services/mysql_runtime_error_service.h>
#include <mysql/components/services/component_status_var_service.h>

#include <list>
#include <string>
#include <iostream>
#include <sstream>
#include <chrono>
#include <atomic>
#include <cctype>
#include <algorithm>
#include <map>
#include <tuple>
#include <utility>

// include 3rd party headers for this plugin
#include <curl/curl.h>
#include <nlohmann/json.hpp>

// declare which services are required for this component
extern REQUIRES_SERVICE_PLACEHOLDER(log_builtins);
extern REQUIRES_SERVICE_PLACEHOLDER(log_builtins_string);
extern REQUIRES_SERVICE_PLACEHOLDER(dynamic_privilege_register);
extern REQUIRES_SERVICE_PLACEHOLDER(udf_registration);
extern REQUIRES_SERVICE_PLACEHOLDER(mysql_udf_metadata);
extern REQUIRES_SERVICE_PLACEHOLDER(mysql_thd_security_context);
extern REQUIRES_SERVICE_PLACEHOLDER(global_grants_check);
extern REQUIRES_SERVICE_PLACEHOLDER(mysql_current_thread_reader);
extern REQUIRES_SERVICE_PLACEHOLDER(mysql_runtime_error);
extern REQUIRES_SERVICE_PLACEHOLDER(status_variable_registration);

extern SERVICE_TYPE(log_builtins) * log_bi;
extern SERVICE_TYPE(log_builtins_string) * log_bs;