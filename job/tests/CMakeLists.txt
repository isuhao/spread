cmake_minimum_required(VERSION 2.6)

include_directories("../")
include_directories("../../")

find_package(Boost COMPONENTS thread REQUIRED)

set(JDIR ../../job)
set(JOB ${JDIR}/job.cpp ${JDIR}/jobinfo.cpp ${JDIR}/thread.cpp ${JOB})

add_executable(thread1_test thread1_test.cpp ${JOB})
target_link_libraries(thread1_test ${Boost_LIBRARIES})

add_executable(cleanup_test cleanup_test.cpp ${JOB})
target_link_libraries(cleanup_test ${Boost_LIBRARIES})

add_executable(client_test client_test.cpp ${JOB})
target_link_libraries(client_test ${Boost_LIBRARIES})

add_executable(delete_test delete_test.cpp ${JOB})
target_link_libraries(delete_test ${Boost_LIBRARIES})

add_executable(abortfail_test abortfail_test.cpp ${JOB})
target_link_libraries(abortfail_test ${Boost_LIBRARIES})
