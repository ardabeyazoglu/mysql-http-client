# mysql-http-client

An experimental MySQL component using MySQL 8's component infrastracture. 

This component extends MySQL with http/curl support and installs an `http_request` function.

## Requirements

    # debian, ubuntu etc.
    sudo apt install cmake gcc libbison-dev libncurses5-dev

## Build

    cmake .. -DDOWNLOAD_BOOST=1 -DWITH_BOOST=../downloads
    make -j 6

## Test

    cd mysql-test
    perl mtr --mem --start