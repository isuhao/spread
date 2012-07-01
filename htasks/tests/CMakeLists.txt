cmake_minimum_required(VERSION 2.6)

find_package(Boost COMPONENTS filesystem system thread REQUIRED)
find_package(CURL REQUIRED)
set(LIBS ${Boost_LIBRARIES} zzip ${CURL_LIBRARIES})

include_directories("../")
include_directories("../../")
include_directories("../../libs/")
include_directories("../../libs/jsoncpp/include/")

set(SPDIR ../..)
set(LIBDIR ../../libs)

set(SLDIR ${SPDIR}/spreadlib)
set(MDIR ${LIBDIR}/mangle)
set(JS ${LIBDIR}/jsoncpp/src)
set(MIDIR ${SPDIR}/misc)
set(UDIR ${SPDIR}/unpack)
set(TDIR ${SPDIR}/tasks)
set(JDIR ${SPDIR}/job)
set(HDIR ${SPDIR}/hash)
set(HTDIR ${SPDIR}/htasks)

set(UNPACK ${UDIR}/dirwriter.cpp ${UDIR}/base.cpp ${UDIR}/unpack_zip.cpp ${UDIR}/auto.cpp)
set(JOB ${JDIR}/thread.cpp ${JDIR}/job.cpp)
set(TASKS ${TDIR}/unpack.cpp ${TDIR}/curl.cpp ${TDIR}/download.cpp ${TDIR}/install.cpp ${TDIR}/multi.cpp ${TDIR}/fileop.cpp ${TDIR}/dircopy.cpp ${TDIR}/notify.cpp ${TDIR} ${JOB} ${UNPACK})

set(C85 ${MIDIR}/comp85.cpp)
set(HASH ${HDIR}/hash.cpp ${LIBDIR}/sha2/sha2.c ${C85})

set(HTASKS ${HTDIR}/hashtask.cpp ${HASH} ${TASKS})

set(MANGLE ${MDIR}/stream/clients/io_stream.cpp)
set(JSON ${JS}/json_reader.cpp ${JS}/json_writer.cpp ${JS}/json_value.cpp)
set(READJSON ${JSON} ${MANGLE} ${MIDIR}/readjson.cpp)

set(SPL ${SLDIR}/sr0.cpp ${TASKS} ${READJSON} ${HASH})

add_executable(hashtask_test hashtask_test.cpp ${HTASKS})
target_link_libraries(hashtask_test ${LIBS})