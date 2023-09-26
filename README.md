# mysql-http-client

An experimental MySQL curl extension based on MySQL 8's component infrastracture. 

This component extends MySQL with http/curl support and installs an `http_request` function.

## Requirements

    # debian, ubuntu etc.
    sudo apt install cmake clang bison libbison-dev libncurses5-dev

    # osx
    brew install bison ncurses curl cmake clang

    # download mysql-server source code
    git clone --single-branch --branch=8.1.0 https://github.com/mysql/mysql-server

## Build

    # build mysql server for once
    cd mysql-server
    mkdir BIN-DEBUG; cd BIN-DEBUG
    cmake .. -DDOWNLOAD_BOOST=1 -DWITH_BOOST=../downloads
    make -j 4

    # build httpclient component once and everytime it is modified
    cd mysql-server/BIN-DEBUG
    cmake .. -DDOWNLOAD_BOOST=1 -DWITH_BOOST=../downloads
    make component_httpclient

## Test

    # start mysql test server in one terminal
    cd mysql-server/BIN-DEBUG/mysql-test
    perl mtr --mem --start

    # connect to mysql test server in a second terminal
    # $SOCKET_NAME is printed by previous command
    mysql -uroot -S $SOCKET_NAME

    mysql> INSTALL COMPONENT "file://component_httpclient";
    mysql> GRANT HTTP_CLIENT ON *.* TO root@localhost;

## Usage

    # example http requests
    mysql> SELECT http_request('GET', 'https://dummyjson.com/products?limit=1', '', '');
    mysql> SET GLOBAL httpclient.curlopt_timeout_ms = 1000; SELECT http_request('GET', 'https://dummyjson.com/products?limit=1', '', '');

    # example using json parser
    mysql> SELECT JSON_VALUE(http_request('GET', 'https://dummyjson.com/products?limit=1', '', ''), '$.products[0].description');

    # time spent in http requests
    mysql> SHOW GLOBAL STATUS LIKE '%httpclient%';


