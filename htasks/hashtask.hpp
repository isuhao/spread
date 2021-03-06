#ifndef __HASH_TASK_HPP_
#define __HASH_TASK_HPP_

#include "hashtaskbase.hpp"
#include <mangle/stream/stream.hpp>

/* HashTask is the base class for jobs that depend on or produce a
   verified set of input/output data. Exactly what data is needed or
   produced depends on the child class.

   The task is given a set of zero or more input files, identified by
   hashes, which subclasses may use anyway they like. The task should
   signal an error if the given input is not sufficient.

   The task is also given a set of zero or more requested of output
   files, identified by hashes. The task is required to produce all of
   them; if it can't, it is an error. It is not an error however, if
   the task is able to produce data that is not requested. Non-needed
   data is simply ignored.

   An output hash may be requested more than once. HashTask will
   automatically copy the output data to all requested locations.
 */

namespace Spread
{
  struct HashTask : HashTaskBase
  {

    /* Use this in subclasses to write data. Produces an output stream
       for the requested hash, or an empty ptr if nobody requested the
       data.

       This guarantees that all output locations are created properly,
       and that the written data matches the requested hash.

       Only one stream can be used at any given time, so make sure you
       write all the data to the stream before opening a new one.
     */
    Mangle::Stream::StreamPtr getOutStream(const Hash &h);

    HashTask();

  protected:
    // Create the job that performs this task.
    virtual Job *createJob() = 0;

  private:
    void doJob();
    void closeStream();
    struct _HashTaskHidden;
    boost::shared_ptr<_HashTaskHidden> ptr;

  protected:
    std::string desc; // Used for error messages
  };
};

#endif
