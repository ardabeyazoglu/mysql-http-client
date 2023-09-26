DISABLE_MISSING_PROFILE_WARNING()

# Fetch and include the nlohmann/json library
include(FetchContent)
FetchContent_Declare(
    nlohmann_json
    GIT_REPOSITORY https://github.com/nlohmann/json.git
    GIT_TAG v3.11.2
)
FetchContent_MakeAvailable(nlohmann_json)

MYSQL_ADD_COMPONENT(httpclient
httpclient.cc 
  MODULE_ONLY
  TEST_ONLY
  LINK_LIBRARIES curl
  LINK_LIBRARIES nlohmann_json
)