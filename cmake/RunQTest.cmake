# 跑一个 Qt Test 可执行文件，并把它的输出打印到自己的标准输出上。
#
# 为什么需要这一层：本机环境下 `QTEST_MAIN` 生成的进程把结果写到标准输出时，
# 我们的“重定向/管道”收不到任何东西（同一进程里 `printf` 却完全正常，
# 而 `-o <文件>` 也正常），于是 `ctest --output-on-failure` 只能看到
# “Test #N ... ***Failed”，看不到到底是哪一条断言挂了。
# 让测试自己写文件、再由本脚本 cat 出来，就绕开了这个坑。
#
# 用法（由 CMakeLists.txt 里的 flowkeyd_add_test 调用）：
#   cmake -DTEST_EXE=... -DTEST_LOG=... -P cmake/RunQTest.cmake
if(NOT DEFINED TEST_EXE OR NOT DEFINED TEST_LOG)
    message(FATAL_ERROR "RunQTest.cmake 需要 TEST_EXE 与 TEST_LOG")
endif()

execute_process(
    COMMAND "${TEST_EXE}" -o "${TEST_LOG},txt"
    RESULT_VARIABLE _result
)

if(EXISTS "${TEST_LOG}")
    execute_process(COMMAND "${CMAKE_COMMAND}" -E cat "${TEST_LOG}")
    file(REMOVE "${TEST_LOG}")
endif()

if(NOT _result EQUAL 0)
    message(FATAL_ERROR "test failed with exit code ${_result}")
endif()
