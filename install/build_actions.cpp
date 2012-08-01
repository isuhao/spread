#include "build_actions.hpp"

#include "rules/install_finder.hpp"
#include <stdexcept>
#include <boost/filesystem.hpp>

using namespace Spread;

void ActionBuilder::addHint(const Hash &hint)
{
  // Find the matching archive
  const ArcRuleData *data = rules.findArchive(hint);
  if(!data) return;

  // Load the directory object
  DirectoryCPtr dir = cache.loadDir(data->dirHash);
  if(!dir) return;

  // Prime the ArcRuleSet with the data
  arcs.addArchive(data->arcHash, dir, data->ruleString);
}

void ActionBuilder::addDep(const std::string &file, const Hash &hash)
{
  list.dir[file] = hash;
}

void ActionBuilder::addDir(const Directory *dir)
{
  Directory::DirMap::const_iterator it;
  for(it = dir->dir.begin(); it != dir->dir.end(); it++)
    addDep(it->first, it->second);
}

void ActionBuilder::addDir(const Hash &hash, bool alsoAsHint)
{
  DirectoryCPtr dir = cache.loadDir(hash);

  if(!dir)
    throw std::runtime_error("Directory not found: " + hash.toString());

  addDir(dir);

  if(alsoAsHint)
    addHint(hash);
}

void ActionBuilder::build(ActionMap &output)
{
  InstallFinder finder(arcs, cache.index);

  namespace bs = boost::filesystem;

  // Build dependency list
  {
    bs::path pdir = prefix;
    Directory::DirMap::const_iterator it;
    for(it = list.dir.begin(); it != list.dir.end(); it++)
      {
        // Add path prefix, if any
        bs::path file;
        if(prefix != "")
          file = pdir/it->first;
        else
          file = it->first;

        finder.addDep(file.string(), it->second);
      }
  }

  bool res = finder.perform(output);

  if(!res)
    {
      // Build an error message
      std::string err = "Don't know how to satisfy the following dependencies:";

      // Loop through output and add the names of all the missing files
      ActionMap::const_iterator it;
      for(it = output.begin(); it != output.end(); it++)
        {
          const Action &a = it->second;
          if(a.isNone())
            {
              err += "\n  " + it->first.toString();
              std::set<std::string>::const_iterator it2;
              for(it2 = a.destlist.begin(); it2 != a.destlist.end(); it2++)
                err += "\n    " + *it2;
            }
        }

      throw std::runtime_error(err);
    }
}