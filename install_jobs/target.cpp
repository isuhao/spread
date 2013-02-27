#include "target.hpp"

#include <htasks/copyhash.hpp>
#include <htasks/downloadhash.hpp>
#include <htasks/unpackhash.hpp>
#include <stdexcept>

using namespace Spread;

std::string Target::fetchFile(const Hash &hash)
{
  JobPtr fetchTask;
  std::string output = owner->fetchTmpFile(hash, fetchTask);
  assert(output != "");

  /* If there is a job required to fetch this file, then
     fetchTmpFile() will have set it up for us. Execute it.
  */
  if(fetchTask)
    execJob(fetchTask, true);

  return output;
}

bool Target::execHashTask(HashTask *htask, bool failOnError)
{
  assert(htask);
  assert(!src.hash.isNull());

  // Add inputs
  for(int i=0; i<src.deps.size(); i++)
    {
      const Hash &input = src.deps[i];
      assert(!input.isNull());
      assert(input != src.hash);
      htask->addInput(input, fetchFile(input));
    }

  // Add outputs
  Hash::DirMap::const_iterator it;
  for(it = output.begin(); it != output.end(); it++)
    htask->addOutput(it->second, it->first);

  return execJob(htask, failOnError);
}

void Target::doJob()
{
  assert(owner);
  if(src.type == TST_File)
    execHashTask(new CopyHash(src.value), true);
  else if(src.type == TST_Download)
    {
      if(!execHashTask(new DownloadHash(src.value), false))
        {
          // Let the caller try to find a replacement
          owner->brokenURL(src.hash, src.value);
          throw std::runtime_error(lastJob->getMessage());
        }
    }
  else if(src.type == TST_Archive)
    {
      assert(src.dir);
      execHashTask(new UnpackHash(*src.dir), true);
    }
  else assert(0);
  if(checkStatus()) return;

  assert(output.size());
  owner->addToCache(output);
  setDone();
}
