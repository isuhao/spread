#include "unpackhash.hpp"
#include "tasks/unpack.hpp"
#include <mangle/vfs/stream_factory.hpp>
#include <mangle/stream/servers/null_stream.hpp>
#include "hash/hash_stream.hpp"
#include <stdexcept>

using namespace Spread;
using namespace Jobify;
using namespace Tasks;
using namespace Mangle::Stream;

/* This output writer is used for indexing. It swallows data and
   filenames, and stores the names + hashes in an index.
 */
struct UH_ListHasher : Mangle::VFS::StreamFactory
{
  UnpackHash::HashMap *index;

  HashStreamPtr stream;
  std::string lastName;

  // This follows the same close-then-open mechanics as
  // getOutStream()/closeStream() in HashTask.
  StreamPtr open(const std::string &name)
  {
    // Store the last hash, if any
    if(stream)
      {
        // Ignore directories
        char ch = 0;
        if(lastName.size()) ch = lastName[lastName.size()-1];
        if(ch != '/' && ch != '\\')
          (*index)[lastName] = stream->finish();
      }
    else
      stream.reset(new HashStream(StreamPtr(new NullStream)));

    // Use this after the last stream has finished
    if(name == "")
      return StreamPtr();

    lastName = name;
    return stream;
  }
};

void UnpackHash::makeIndex(const std::string &arcFile,
                           HashMap &index)
{
  UH_ListHasher *m = new UH_ListHasher;
  m->index = &index;
  Mangle::VFS::StreamFactoryPtr mp(m);

  UnpackTask unp(arcFile, mp);
  unp.run();
  // Needed to include the last one
  m->open("");
}

/* This output writer is used for unpacking. It looks up filenames and
   finds their hashes.
 */
struct UH_ListUser : Mangle::VFS::StreamFactory
{
  UnpackHash::HashMap index;
  HashTask *owner;

  UH_ListUser(HashTask *t) : owner(t) { assert(owner); }

  StreamPtr open(const std::string &name)
  {
    // Look up the hash from the name
    Hash h = index[name];

    // Fail if the unpacker threw an unknown file at us
    if(h.isNull())
      throw std::runtime_error("Unexpected file in archive: " + name);

    // HashTask takes care of the rest
    return owner->getOutStream(h);
  }
};

Job *UnpackHash::createJob()
{
  UH_ListUser *m = new UH_ListUser(this);
  Mangle::VFS::StreamFactoryPtr mp(m);

  // Copy requested items from the full index over to the working
  // index
  HashMap::iterator it;

  // Just to optimize a tiny bit more - use this to prune repeated
  // hashes from the file list
  std::set<Hash> hashes;

  for(it = index.begin(); it != index.end(); ++it)
    {
      const std::string &name = it->first;
      const Hash &hsh = it->second;

      // Do we need this object?
      if(outputs.find(hsh) == outputs.end())
        // Hash was not found in the requested output list, skip it.
        continue;

      // Have we already listed this hash?
      if(hashes.count(hsh) != 0)
        // Prune dupicates
        continue;

      // Is the hash empty? Might be the case for directories. If so,
      // skip it.
      if(hsh.isNull())
        continue;

      /* This file is a keeper. Add it to the file list, as well as to
         the live lookup index.
       */
      list.insert(name);
      m->index[name] = hsh;
      hashes.insert(hsh);
    }

  // Get the input filename
  assert(inputs.size() == 1);
  std::string file = inputs.begin()->second;

  // Set up the unpacking job
  return new UnpackTask(file, mp, Jobify::JobInfoPtr(), list);
}