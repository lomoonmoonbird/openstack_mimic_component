Name:           spdlog
Version:        1.9.0
Release:        1%{?dist}
Summary:        spdlog

License:        GPL
Source0:        %{name}-%{version}.tar.gz


%description
spdlog


%prep
tar -zxvf %{_sourcedir}/%{name}-%{version}.tar.gz
scp %{_sourcedir}//cmake-3.23.0-linux-x86_64.tar.gz %{_builddir}


%build
tar -zxvf %{_builddir}/cmake-3.23.0-linux-x86_64.tar.gz
pushd %{_builddir}/%{name}-%{version}/
    [ -d build ] || mkdir build
    pushd build
        %{_builddir}/cmake-3.23.0-linux-x86_64/bin/cmake .. -B %{_builddir}/%{name}-%{version}/build
        make -j20
    popd
popd


%install
pushd %{_builddir}/%{name}-%{version}/build
    make install DESTDIR=$RPM_BUILD_ROOT
popd


%clean
rm -rf %{_builddir}
rm -rf %{buildroot}


%files
/usr/local/include/spdlog/async.h
/usr/local/include/spdlog/async_logger-inl.h
/usr/local/include/spdlog/async_logger.h
/usr/local/include/spdlog/cfg/argv.h
/usr/local/include/spdlog/cfg/env.h
/usr/local/include/spdlog/cfg/helpers-inl.h
/usr/local/include/spdlog/cfg/helpers.h
/usr/local/include/spdlog/common-inl.h
/usr/local/include/spdlog/common.h
/usr/local/include/spdlog/details/backtracer-inl.h
/usr/local/include/spdlog/details/backtracer.h
/usr/local/include/spdlog/details/circular_q.h
/usr/local/include/spdlog/details/console_globals.h
/usr/local/include/spdlog/details/file_helper-inl.h
/usr/local/include/spdlog/details/file_helper.h
/usr/local/include/spdlog/details/fmt_helper.h
/usr/local/include/spdlog/details/log_msg-inl.h
/usr/local/include/spdlog/details/log_msg.h
/usr/local/include/spdlog/details/log_msg_buffer-inl.h
/usr/local/include/spdlog/details/log_msg_buffer.h
/usr/local/include/spdlog/details/mpmc_blocking_q.h
/usr/local/include/spdlog/details/null_mutex.h
/usr/local/include/spdlog/details/os-inl.h
/usr/local/include/spdlog/details/os.h
/usr/local/include/spdlog/details/periodic_worker-inl.h
/usr/local/include/spdlog/details/periodic_worker.h
/usr/local/include/spdlog/details/registry-inl.h
/usr/local/include/spdlog/details/registry.h
/usr/local/include/spdlog/details/synchronous_factory.h
/usr/local/include/spdlog/details/tcp_client-windows.h
/usr/local/include/spdlog/details/tcp_client.h
/usr/local/include/spdlog/details/thread_pool-inl.h
/usr/local/include/spdlog/details/thread_pool.h
/usr/local/include/spdlog/details/windows_include.h
/usr/local/include/spdlog/fmt/bin_to_hex.h
/usr/local/include/spdlog/fmt/bundled/args.h
/usr/local/include/spdlog/fmt/bundled/chrono.h
/usr/local/include/spdlog/fmt/bundled/color.h
/usr/local/include/spdlog/fmt/bundled/compile.h
/usr/local/include/spdlog/fmt/bundled/core.h
/usr/local/include/spdlog/fmt/bundled/format-inl.h
/usr/local/include/spdlog/fmt/bundled/format.h
/usr/local/include/spdlog/fmt/bundled/locale.h
/usr/local/include/spdlog/fmt/bundled/os.h
/usr/local/include/spdlog/fmt/bundled/ostream.h
/usr/local/include/spdlog/fmt/bundled/printf.h
/usr/local/include/spdlog/fmt/bundled/ranges.h
/usr/local/include/spdlog/fmt/bundled/xchar.h
/usr/local/include/spdlog/fmt/chrono.h
/usr/local/include/spdlog/fmt/compile.h
/usr/local/include/spdlog/fmt/fmt.h
/usr/local/include/spdlog/fmt/ostr.h
/usr/local/include/spdlog/fmt/xchar.h
/usr/local/include/spdlog/formatter.h
/usr/local/include/spdlog/fwd.h
/usr/local/include/spdlog/logger-inl.h
/usr/local/include/spdlog/logger.h
/usr/local/include/spdlog/pattern_formatter-inl.h
/usr/local/include/spdlog/pattern_formatter.h
/usr/local/include/spdlog/sinks/android_sink.h
/usr/local/include/spdlog/sinks/ansicolor_sink-inl.h
/usr/local/include/spdlog/sinks/ansicolor_sink.h
/usr/local/include/spdlog/sinks/base_sink-inl.h
/usr/local/include/spdlog/sinks/base_sink.h
/usr/local/include/spdlog/sinks/basic_file_sink-inl.h
/usr/local/include/spdlog/sinks/basic_file_sink.h
/usr/local/include/spdlog/sinks/daily_file_sink.h
/usr/local/include/spdlog/sinks/dist_sink.h
/usr/local/include/spdlog/sinks/dup_filter_sink.h
/usr/local/include/spdlog/sinks/hourly_file_sink.h
/usr/local/include/spdlog/sinks/mongo_sink.h
/usr/local/include/spdlog/sinks/msvc_sink.h
/usr/local/include/spdlog/sinks/null_sink.h
/usr/local/include/spdlog/sinks/ostream_sink.h
/usr/local/include/spdlog/sinks/qt_sinks.h
/usr/local/include/spdlog/sinks/ringbuffer_sink.h
/usr/local/include/spdlog/sinks/rotating_file_sink-inl.h
/usr/local/include/spdlog/sinks/rotating_file_sink.h
/usr/local/include/spdlog/sinks/sink-inl.h
/usr/local/include/spdlog/sinks/sink.h
/usr/local/include/spdlog/sinks/stdout_color_sinks-inl.h
/usr/local/include/spdlog/sinks/stdout_color_sinks.h
/usr/local/include/spdlog/sinks/stdout_sinks-inl.h
/usr/local/include/spdlog/sinks/stdout_sinks.h
/usr/local/include/spdlog/sinks/syslog_sink.h
/usr/local/include/spdlog/sinks/systemd_sink.h
/usr/local/include/spdlog/sinks/tcp_sink.h
/usr/local/include/spdlog/sinks/win_eventlog_sink.h
/usr/local/include/spdlog/sinks/wincolor_sink-inl.h
/usr/local/include/spdlog/sinks/wincolor_sink.h
/usr/local/include/spdlog/spdlog-inl.h
/usr/local/include/spdlog/spdlog.h
/usr/local/include/spdlog/stopwatch.h
/usr/local/include/spdlog/tweakme.h
/usr/local/include/spdlog/version.h
/usr/local/lib64/cmake/spdlog/spdlogConfig.cmake
/usr/local/lib64/cmake/spdlog/spdlogConfigTargets-release.cmake
/usr/local/lib64/cmake/spdlog/spdlogConfigTargets.cmake
/usr/local/lib64/cmake/spdlog/spdlogConfigVersion.cmake
/usr/local/lib64/libspdlog.a
/usr/local/lib64/pkgconfig/spdlog.pc
