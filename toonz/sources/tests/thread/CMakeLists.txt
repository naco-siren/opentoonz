# Tests of TThread::QtRunnable, the Qt side of the thread pool (step 9d of
# doc/nexttoonz_core_noqt.md); the pool itself is tested without Qt in
# core/test_executor.cpp. A binary of its own: main() makes the
# QCoreApplication and calls TThread::init(), and Executor::shutdown(), which
# the last test calls, is for the whole process.

add_executable(nexttoonz_thread_tests
    thread_test_main.cpp
    test_qtrunnable.cpp
)
# test_qtrunnable.cpp declares a Q_OBJECT class and includes its moc output
set_target_properties(nexttoonz_thread_tests PROPERTIES AUTOMOC ON)
target_link_libraries(nexttoonz_thread_tests
    PRIVATE
        tnzcore
        Qt5::Core
        GTest::gtest
)
nexttoonz_stage_runtime(nexttoonz_thread_tests tnzcore)

# Registered as "thread.<Suite>.<Test>" with the label "thread"; ctest runs
# each test in a process of its own.
gtest_discover_tests(nexttoonz_thread_tests
    TEST_PREFIX "thread."
    PROPERTIES LABELS thread
    DISCOVERY_TIMEOUT 60
)
