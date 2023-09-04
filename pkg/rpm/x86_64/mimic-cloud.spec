Name:           mimic_cloud
Version:        1.0
Release:        1%{?dist}
Summary:        mimic_cloud

License:        GPL

%description
mimic_cloud services, include:
proxy_horizon, proxy_nova, proxy_neutron, proxy_cinder,
proxy_keystone, judge_horizon, judge_controller, schedule_horizon, schedule_controller,
schedule_keystone


%prep
scp -r %{_sourcedir}/* %{_builddir}


%build
tar -zxvf %{_builddir}/cmake-3.23.0-linux-x86_64.tar.gz
rm -rf %{_builddir}/build/*
%{_builddir}/cmake-3.23.0-linux-x86_64/bin/cmake %{_builddir} -B %{_builddir}/build
pushd %{_builddir}/build
    make -j20
popd


%install
pushd %{_builddir}/build
    make install DESTDIR=$RPM_BUILD_ROOT
popd


%clean
rm -rf %{_builddir}
rm -rf %{buildroot}


%files
/usr/local/bin/mimic-cloud
/usr/local/lib/libjsoncpp.so
/usr/local/lib/libyamlcpp.so
/etc/mimic-cloud/mimic_cloud.yml
/usr/lib/systemd/system/mimic-cloud@.service
/opt/keystone_util.py
/opt/keystone_util.pyc
/opt/keystone_util.pyo
/home/deploy/deploy_mimic/config.sh
/home/deploy/deploy_mimic/deploy.sh
/home/deploy/deploy_mimic/functions.sh
/home/deploy/deploy_mimic/logcuter.sh
/home/deploy/deploy_mimic/variables.sh
