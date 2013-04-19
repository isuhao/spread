#include "spread.hpp"
#include "sr0/sr0.hpp"
#include "rules/ruleset.hpp"
#include "tasks/download.hpp"
#include "hash/hash_stream.hpp"
#include "chanlist.hpp"
#include <mangle/stream/servers/file_stream.hpp>
#include <mangle/stream/clients/copy_stream.hpp>
#include <boost/filesystem.hpp>
#include <boost/thread/recursive_mutex.hpp>
#include <job/thread.hpp>

//#define PRINT_DEBUG
#ifdef PRINT_DEBUG
#include <iostream>
#define PRINT(a) std::cout << a << "\n"
#else
#define PRINT(a)
#endif

using namespace Spread;
namespace bf = boost::filesystem;

JobInfoPtr SpreadLib::download(const std::string &url,
                               const std::string &dest,
                               bool async)
{
  DownloadTask *job = new DownloadTask(url, dest);
  return Thread::run(job, async);
}

static std::string abs(const bf::path &path)
{
  return bf::absolute(path).string();
}

static void parent(const bf::path &file)
{
  bf::create_directories(file.parent_path());
}

typedef std::map<std::string, bool> WasUpdated;

struct SpreadLib::_Internal
{
  /* TODO: We have a potential race condition here because multiple
     threads can access the RuleSet at the same time. While the class
     itself is thread safe, the data may not be. A PackList may load a
     new rule file while an install is in progress, potentially
     overwriting rules needed for the install.

     This can be avoided with careful data setup though, and the worst
     case scenario is a failed install. Therefore this is not critical
     enough to justify the extra complexity of keeping separate
     RuleSet instances for each running installer job.
   */

  Cache::Cache cache;
  RuleSet rules;
  boost::recursive_mutex mutex;
  JobManagerPtr manager;

  ChanList chan;

  // This keeps track of updates in progress for a given channel
  bf::path repoDir;

  WasUpdated wasUpdated;

  std::string getPath(const bf::path &file)
  {
    bf::path res = repoDir/file;
    parent(res);
    return res.string();
  }

  std::string chanPath(const std::string &channel, const std::string &file = "")
  {
    if(channel.find('/') != std::string::npos ||
       channel.find('\\') != std::string::npos)
      throw std::runtime_error("Invalid channel name " + channel + " (contains slashes)");

    return getPath("channels/" + channel + "/" + file);
  }

  _Internal(const std::string &baseDir)
    : chan(baseDir, rules), repoDir(baseDir)
  {}

  ~_Internal()
  {
    manager->finish();
    manager->getInfo()->abort();
    try { bf::remove_all(cache.tmpDir); } catch(...) {}
  }
};

#define LOCK boost::lock_guard<boost::recursive_mutex> lock(ptr->mutex)

SpreadLib::SpreadLib(const std::string &outDir, const std::string &tmpDir)
{
  PRINT("SpreadLib()");
  ptr.reset(new _Internal(abs(outDir)));

  PRINT("  repoDir=" << ptr->repoDir);

  ptr->cache.index.load(ptr->getPath("cache.conf"));
  ptr->cache.tmpDir = abs(tmpDir);
  ptr->cache.files.basedir = ptr->getPath("cache/");
  ptr->manager.reset(new JobManager(ptr->cache));

  PRINT("  Starting JobManager");
  Thread::run(ptr->manager);
}

JobManagerPtr SpreadLib::getJobManager() const { return ptr->manager; }

void SpreadLib::setURLCallback(CBFunc cb) { ptr->rules.setURLCallback(cb); }

bool SpreadLib::wasUpdated(const std::string &channel) const
{
  WasUpdated::iterator it = ptr->wasUpdated.find(channel);
  if(it == ptr->wasUpdated.end()) return false;
  return it->second;
}

JobInfoPtr SpreadLib::updateFromURL(const std::string &channel,
                                    const std::string &url,
                                    bool async)
{
  LOCK;
  // Load any existing data into memory, ignore errors.
  try { ptr->chan.load(channel); } catch(...) {}

  JobInfoPtr info = SR0::fetchURL(url, ptr->chanPath(channel), ptr->manager, async,
                                  &ptr->wasUpdated[channel]);

  // Notify the channel that we are updating the files on disk, so
  // that future loads are blocked while the update is in progress.
  ptr->chan.setChannelJob(channel, info);
  return info;
}

JobInfoPtr SpreadLib::updateFromFile(const std::string &channel,
                                     const std::string &path,
                                     bool async)
{
  LOCK;
  try { ptr->chan.load(channel); } catch(...) {}
  JobInfoPtr info = SR0::fetchFile(path, ptr->chanPath(channel), ptr->manager, async,
                                   &ptr->wasUpdated[channel]);
  ptr->chan.setChannelJob(channel, info);
  return info;
}

const PackInfo &SpreadLib::getPackInfo(const std::string &channel,
                                       const std::string &package) const
{
  LOCK;
  return ptr->chan.getPackList(channel).get(package);
}

const PackInfoList &SpreadLib::getInfoList(const std::string &channel) const
{
  LOCK;
  return ptr->chan.getPackList(channel).getList();
}

struct InstallMonitor : Job
{
  JobInfoPtr client;
  boost::shared_ptr<SpreadLib::_Internal> ptr;
  PackInfo pack;
  std::string where;

  void doJob()
  {
    setDone();

    if(waitClient(client)) return;
    assert(client->isSuccess());

    // Notify the system that this package was installed successfully
    LOCK;
    ptr->chan.getStatusList().setEntry(pack, where);

    /* Attempt to notify the status list directly if the entry has
       been updated since our install started. For example: you start
       installing a package, then update the data set, then the
       package install finishes. The newly installed package is now
       potentially already outdated. This will mark it as such.

       This may throw if the package no longer exists, or if anything
       else went wrong with the loading. This is OK, we have done
       everything required at this point, so if this job fails now it
       doesn't affect anything else.
    */
    const PackInfo &newest = ptr->chan.getPackList(pack.channel).get(pack.package);
    ptr->chan.getStatusList().notifyNew(newest);

    setDone();
  }
};

JobInfoPtr SpreadLib::installPack(const std::string &channel,
                                  const std::string &package,
                                  const std::string &_where,
                                  std::string *version,
                                  bool async,
                                  bool doUpgrade,
                                  bool enableAsk)
{
  assert(channel != "" && package != "" && _where != "");
  LOCK;

  std::string where = abs(_where);

  // Request the package information we are installing
  const PackInfo& p = getPackInfo(channel,package);
  if(version) *version = p.version;

  const PackStatus *ps = NULL;
  if(doUpgrade)
    {
      // If we are upgrading, check against existing installations
      // first.
      ps = getPackStatus(channel, package, where);

      // If we already have the latest version, don't do anything.
      if(ps && p.match(ps->info))
        return JobInfoPtr();
    }

  // Create an installer job (without starting it)
  InstallerPtr inst = ptr->manager->createInstaller(where, ptr->rules, enableAsk);

  // Add the output directory hashes
  assert(p.dirs.size() == p.paths.size());
  for(int i=0; i<p.dirs.size(); i++)
    inst->addDir(Hash(p.dirs[i]), p.paths[i]);

  if(ps)
    {
      assert(doUpgrade);
      // If we are upgrading from an older version, then notify the
      // installer. It will handle all the details for us.
      assert(ps->info.dirs.size() == ps->info.paths.size());
      for(int i=0; i<ps->info.dirs.size(); i++)
        inst->remDir(Hash(ps->info.dirs[i]), ps->info.paths[i]);
    }

  // Start the job
  JobInfoPtr info = ptr->manager->addInst(inst);

  // Add monitor job to update our status. It will exit when the
  // installer exits.
  InstallMonitor *job = new InstallMonitor;
  job->client = info;

  /* Set the PackInfo information, which will be injected into the
     install-list database if/when the install job finishes. The info
     we set here controls what will be available through
     getPackStatus() etc in the future.

     Notice that we COPY the information here, it is not a reference.
     This is intentional and important; the PackInfo referenced by 'p'
     above belongs to the PackList owned by ptr->chan. If the channel
     data is reloaded while our install job is in progress, not only
     will old references be invalid - but even if they WERE valid they
     would contain the wrong data! The 'updated' PackInfo would refer
     to the newest version, not to the data we are actually
     installing.
   */
  job->pack = p;
  job->ptr = ptr;
  job->where = where;
  Thread::run(job, async);

  // Return the installer job info, not our monitor job info.
  return info;
}

struct RemoveJob : Job
{
  std::string where;
  RemoveJob(const std::string &w) : where(w) {}

  void doJob()
  {
    bf::remove_all(where);
    setDone();
  }
};

JobInfoPtr SpreadLib::uninstallPack(const std::string &channel,
                                    const std::string &package,
                                    const std::string &_where,
                                    bool async)
{
  LOCK;
  std::string where = abs(_where);

  // Remove the install entry first.
  ptr->chan.getStatusList().remove(channel, package, where);

  /* Then create a background job that kills the files.

     Notice that we kill the entire directory, this might not be what
     the user expect. We should at least document it. An alternative
     is to run the full-blown installer in reverse, ie. adding
     existing files with remDir() only. This has the side-effect of
     keeping all locally created files (savegames etc) intact, which
     may be considered good or bad.

     A BETTER option: allow both through a user parameter, but this
     isn't a priority.
   */
  return Thread::run(new RemoveJob(where));
}

const PackStatus *SpreadLib::getPackStatus(const std::string &channel,
                                           const std::string &package,
                                           const std::string &where) const
{
  assert(channel != "" && package != "");
  PackStatusList lst;

  getStatusList(lst, channel, package, where);

  // Return first element in the list, if any
  if(lst.size()) return *lst.begin();

  // No element found
  return NULL;
}

void SpreadLib::getStatusList(PackStatusList &output,
                              const std::string &channel,
                              const std::string &package,
                              const std::string &where) const
{
  LOCK;
  ptr->chan.getStatusList().getList(output, channel, package, abs(where));
}

JobInfoPtr SpreadLib::unpackURL(const std::string &url, const std::string &where,
                                bool async)
{
  LOCK;
  return SR0::fetchURL(url, abs(where), ptr->manager, async);
}

void SpreadLib::verifyCache()
{
  PRINT("verifyCache() start");
  ptr->cache.index.verify();
  PRINT("verifyCache() done");
}

std::string SpreadLib::cacheFile(const std::string &file)
{
  // Doesn't need LOCK, since CacheIndex does its own internal locking
  return ptr->cache.index.addFile(abs(file)).toString();
}

void SpreadLib::cacheCopy(const std::vector<std::string> &inputs,
                          const std::vector<std::string> &outputs,
                          JobInfoPtr info)
{
  assert(inputs.size() == outputs.size());

  int64_t totalSize = 0, cur = 0;

  // If we're keeping stats, add up the input file sizes
  if(info)
    {
      for(int i=0; i<inputs.size(); i++)
        totalSize += bf::file_size(inputs[i]);
      info->setProgress(0, totalSize);
    }

  // Output list of files and hashes
  Hash::DirMap dirmap;

  // Start copying
  using namespace Mangle::Stream;
  for(int i=0; i<inputs.size(); i++)
    {
      const std::string &from = inputs[i];
      const std::string &to = outputs[i];

      parent(to);
      HashStreamPtr out(new HashStream(to, true));
      CopyStream::copy(FileStream::Open(from), out);

      Hash hash = out->finish();
      cur += hash.size();

      // Store both files in the output
      dirmap[from] = hash;
      dirmap[to] = hash;

      if(info)
        {
          // Update progress, and also check for user aborts
          info->setProgress(cur, totalSize);
          if(info->checkStatus())
            break;
        }
    }

  // Add the entries to the cache index
  ptr->cache.index.addMany(dirmap);
}

std::string SpreadLib::cacheCopy(const std::string &from, const std::string &to)
{
  using namespace Mangle::Stream;
  parent(to);
  Hash res;
  {
    HashStreamPtr out(new HashStream(to, true));
    CopyStream::copy(FileStream::Open(from), out);
    res = out->finish();
  }
  ptr->cache.index.addFile(from, res);
  ptr->cache.index.addFile(to, res);
  return res.toString();
}
