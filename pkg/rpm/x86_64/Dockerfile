FROM centos:7
ENV https_proxy=http://192.168.66.77:3128 \
    http_proxy=http://192.168.66.77:3128 \
    no_proxy=192.168.*.*,*.local,localhost,127.0.0.1

COPY ./rpm_build_run.sh /

RUN yum -y install centos-release-scl centos-release-scl-rh \
&& yum -y install vim wget rpm-build rpmdevtools \
&& yum -y install devtoolset-7 \
&& yum -y install libevent-devel.x86_64 \
&& yum -y install mariadb-devel.x86_64

CMD ["/bin/bash", "/rpm_build_run.sh"]
