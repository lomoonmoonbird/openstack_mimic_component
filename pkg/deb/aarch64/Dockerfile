FROM ubuntu:18.04
ENV https_proxy=http://192.168.66.77:3128 \
    http_proxy=http://192.168.66.77:3128 \
    no_proxy=192.168.*.*,*.local,localhost,127.0.0.1

COPY ./deb_build_run.sh /

RUN apt-get update && apt install -y vim dpkg-dev wget \
&&apt-get install -y gcc g++ cmake \
&&apt-get install -y libevent-dev \
&&apt-get install -y libmysqlclient-dev \
&&apt-get install -y dh-make

WORKDIR /root/debbuild/sources/

CMD ["/bin/bash", "/deb_build_run.sh"]
