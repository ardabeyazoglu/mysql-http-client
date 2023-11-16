mysql-http-client
=================

A MySQL 8 component that enables synchronous http(s) requests using SQL functions. 

This component extends MySQL with http/curl support and installs two http request UDFs:

1. ``http_request(METHOD, URL, BODY, HEADERS, CURL_OPTIONS)`` : sends http requests
2. ``http_request_nowait(METHOD, URL, BODY, HEADERS, CURL_OPTIONS)`` : sends http request but does not wait for response nor fail on timeout

Tested only in 8.0.34 and 8.1.0 so far.

## Usage

First download component_httpclient.so and place it in mysql plugin directory (/usr/lib/mysql/plugin) then connect to mysql.

    # install component and grant required privilege
    mysql> INSTALL COMPONENT "file://component_httpclient";
    mysql> GRANT HTTP_CLIENT ON *.* TO root@localhost;

    # example basic get request
    mysql> SELECT http_request('GET', 'https://dummyjson.com/products?limit=1') AS response;
    mysql> SELECT http_request('POST', 'https://httpbin.org/anything', 'param1=value1&param2=value2') AS response;
    mysql> SELECT http_request('POST', 'https://httpbin.org/anything', '{"param1":"value1","param2":"value2"}', '{"Content-Type":"application/json"}') AS response;
    mysql> SELECT http_request('POST', 'https://httpbin.org/anything', '{"param1":"value1","param2":"value2"}', '{"Content-Type":"application/json"}', '{"CURLOPT_AUTHORIZATION":"Bearer XXX"}') AS response;

    # example using json parser
    mysql> SELECT JSON_VALUE(http_request('GET', 'https://dummyjson.com/products?limit=1'), '$.products[0].description') AS response;

    # example with json table
    mysql> SELECT * FROM JSON_TABLE(http_request('GET', 'https://dummyjson.com/products?limit=10'), '$.products[*]' COLUMNS(rowIndex FOR ORDINALITY, id INT PATH '$.id', title VARCHAR(100) PATH '$.title')) AS response;

    # example fire and forget (no timeout error)
    mysql> SELECT http_request_nowait('POST', 'https://httpbin.org/anything', 'param1=value1&param2=value2') AS response;

    # time spent in http requests
    mysql> SHOW GLOBAL STATUS LIKE '%httpclient%';


## Building From Source

### Requirements

    # debian, ubuntu etc.
    sudo apt install cmake clang bison libbison-dev libncurses5-dev

    # osx
    brew install bison ncurses curl cmake clang

    # download mysql-server source code
    git clone --single-branch --branch=8.1.0 https://github.com/mysql/mysql-server

### Build

    # first, place the repository in correct location
    mv httpclient mysql-server/components/

    # build mysql server for once
    cd mysql-server
    mkdir BIN-DEBUG; cd BIN-DEBUG
    cmake .. -DDOWNLOAD_BOOST=1 -DWITH_BOOST=../downloads
    make -j 4

    # build httpclient component once and everytime it is modified
    # built component file is located in BIN-DEBUG/plugin_output_directory/component_httpclient.so
    cd mysql-server/BIN-DEBUG
    make component_httpclient

### Test

    # start mysql test server in one terminal
    cd mysql-server/BIN-DEBUG/mysql-test
    perl mtr --mem --start

    # connect to mysql test server in a second terminal
    # $SOCKET_NAME is printed by previous command
    mysql -uroot -S $SOCKET_NAME

## Contributions

There is still room for improvement. Feel free to write an issue, fork the repo and send a pull request.

Some improvements could be:

- Store each request meta data and response status in a performance_schema table with a ttl.
- Add async (non-blocking) support similar to [pg_net](https://github.com/supabase/pg_net)
