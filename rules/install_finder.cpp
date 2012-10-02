#include "install_finder.hpp"
#include "arcrule.hpp"
#include <assert.h>
#include <boost/filesystem.hpp>
#include <set>

using namespace Spread;
namespace bf = boost::filesystem;

//#define PRINT_DEBUG
#ifdef PRINT_DEBUG
#include <iostream>
#define PRINT(a) std::cout << __LINE__ << ": " << a << "\n";
#else
#define PRINT(a)
#endif

InstallFinder::InstallFinder(const RuleFinder &_rules, Cache::CacheIndex &_cache)
  : rules(_rules), cache(_cache) {}

static std::string abs(const bf::path &file)
{ return bf::absolute(file).string(); }

bool InstallFinder::handleDeps(const DepList &deps, ActionMap &output, bool baseLevel)
{
  PRINT("ENTER handleDeps()");

  bool isOk = true;

  // On the top level we also handle the blind unpack actions.
  if(baseLevel)
    {
      PRINT("Handling " << blinds.size() << " blind unpack(s)");
      DepList subdeps;
      for(int i=0; i<blinds.size(); i++)
        {
          std::string dir = blinds[i].first;
          const Hash &hash = blinds[i].second;

          assert(dir != "");
          dir = abs(dir);

          // Add the archive hash as a dependency
          subdeps.push_back(DepPair("",hash));

          /* Create a dummy hash to represent the archive. This isn't
             looked up anywhere and is never used as a dependency, so
             any unique hash that avoids collisions will do.
          */
          Hash dummy("ARC_" + hash.toString().substr(0,4));

          // TODO: This leaks memory. Fix later.
          ArcRule *rule = new ArcRule(hash, "Blind Archive Unpack Rule");

          // Finally add the Action representing the unpack itself.
          output[dummy] = Action(rule, dir);
        }

      // Expand the dependencies
      if(!handleDeps(subdeps, output))
        isOk = false;
    }

  for(int i=0; i<deps.size(); i++)
    {
      PRINT("LOOP");

      std::string dest = deps[i].first;

      if(dest != "")
        dest = abs(dest);

      const Hash &hash = deps[i].second;

      PRINT("DEST=" << dest << " HASH=" << hash);

      // Check if this hash has already been resolved.
      ActionMap::iterator it = output.find(hash);
      if(it != output.end())
        {
          // Add ourself to it's destination list, if we have one
          it->second.addDest(dest);
          continue;
        }

      // Hash not previously resolved. Check if the cache knows
      // anything about the hash.
      int stat = Cache::CI_ElseWhere;
      if(dest != "")
        stat = cache.getStatus(dest, hash);

      if(stat == Cache::CI_Match)
        {
          PRINT("MATCH");

          // This file already exists, ignore it.
          continue;
        }

      if(stat == Cache::CI_ElseWhere)
        {
          // File either exists elsewhere, or dest == "". Get cache
          // location, if any.
          const std::string &existing = cache.findHash(hash);

          if(existing != "")
            {
              PRINT("EXISTING: " << existing);
              // Check if this path is truely another file, or if
              // these are just two paths pointing to the same data on
              // disk.
              if(dest == "" || !bf::equivalent(dest, existing))
                // File exists in another location. Add a copy
                // operation.
                output[hash] = Action(existing, dest);

              continue;
            }
        }

      PRINT("RULE, calling findRule()");

      /* If we are here, no existing file met our dependency. Look
         up the ruleset to see how else we can fetch the file.
      */
      const Rule *r = rules.findRule(hash);

      PRINT("CHECKING R");

      if(r)
        {
          PRINT("FOUND RULE");

          bool found = false;

          // Set up the output action
          Action a(r);

          // Loop through and add the action for all the hashes it
          // produces, not just the one we are looking for.
          // Non-wanted targets are culled at a later stage.
          for(int k=0; k<r->outputs.size(); k++)
            {
              const Hash &h = r->outputs[k];
              output[h] = a;
              if(h == hash)
                {
                  // Add our output destination to the right output
                  output[h].addDest(dest);
                  found = true;
                }
            }
          assert(found);

          // Then loop through this rule's dependencies, and expand
          // those as well
          DepList subdeps;
          for(int k=0; k<r->deps.size(); k++)
            {
              const Hash &h = r->deps[k];
              subdeps.push_back(DepPair("",h));
              if(!handleDeps(subdeps, output))
                isOk = false;
            }

          /* TODO: Handle recursion. Recursion happens when a hash
             implicitly depends upon itself. We would also have to
             figure out what to do in the case of recursion. At the
             moment though, we just assume our ruleset is acyclic.
          */
          continue;
        }

      PRINT("NO MATCH FOUND");

      // There was no way to resolve this hash. An empty copy action
      // represents an unhandled dependency.
      output[hash] = Action("", dest);
      isOk = false;

      PRINT("END LOOP\n");
    }

  PRINT("Returning " << isOk);
  return isOk;
}
