#include "dir_install.hpp"
#include <install_jobs/hashfinder.hpp>
#include <rules/arcruleset.hpp>
#include <dir/tools.hpp>
#include <stdexcept>
#include <set>

using namespace Spread;

typedef Hash::DirMap DirMap;
typedef std::set<Hash> HashSet;
typedef std::set<std::string> StrSet;

struct DirInstaller::_Internal
{
  RuleFinderPtr rulePtr;
  RuleSet *origRules;
  ArcRuleSet *arcRules;
  DirOwner *owner;

  HashSet hints;
};

static std::string addSlash(std::string input)
{
  if(input.size())
    {
      char c = input[input.size()-1];
      if(c != '\\' && c != '/')
        input += "/";
    }
  return input;
}

void DirInstaller::loadHint(const Hash &hint)
{
  log("Loading HINT=" + hint.toString());

  // Check if there is an archive associated with the given hash
  const ArcRuleData *arc = ptr->origRules->findArchive(hint);

  // If there is no archive rule, then ignore this hint.
  if(!arc)
    {
      log("  No hint archive found, skipping.");
      return;
    }

  log("  Found archive rule: dir=" + arc->dirHash.toString() +
      " arc=" + arc->arcHash.toString());
  DirMap tmp;
  DirPtr arcDir;
  try { arcDir = addDirFile(tmp, arc->dirHash, ""); }
  catch(...)
    {
      // Blind-index archive, then try again
      unpackBlind(arc->arcHash, "");
      // This time, allow the exception to fall through on failure
      arcDir = addDirFile(tmp, arc->dirHash, "");
    }

  // Add archive to the ArcRuleSet.
  ptr->arcRules->addArchive(arc->arcHash, arc->dirHash, arcDir,
                            arc->ruleString);
}

/* Check the rule system if there are any hints associated with a
   given dirHash, then load them.
 */
void DirInstaller::loadDirHints(const Hash &dirHash)
{
  const std::vector<Hash> *hints = ptr->arcRules->findHints(dirHash);
  if(!hints || hints->size() == 0)
    return;

  log("Found rule hints for dirHash=" + dirHash.toString());

  for(int i=0; i<hints->size(); i++)
    {
      const Hash &hint = (*hints)[i];
      loadHint(hint);
    }
}

/* Load the directory object 'dirHash' and store results in
   'out'. Fetches the dirfile if necessary, and takes care of dir
   cache bookkeeping. Returns a DirPtr to the loaded dir.

   The 'path' parameter only affects the filenames added to the 'out'
   list. It does not affect the returned DirPtr.
 */
DirPtr DirInstaller::addDirFile(DirMap &out, const Hash &dirHash,
                                const std::string &path)
{
  // Load any hints associated with the dir first
  loadDirHints(dirHash);

  // Fetch the dir file if possible
  std::string dirFile = fetchFile(dirHash);

  // Load and use it
  DirPtr dir(new DirMap);
  owner.loadDir(dirFile, *dir, dirHash);
  Dir::add(out, *dir, path);
  log("Loaded DIR dirHash=" + dirHash.toString());
  return dir;
}

void DirInstaller::handleHash(DirMap &out, const Hash &dirHash,
                              HashDir &blinds, const std::string &path)
{
  // Check if there is an archive associated with the given hash
  const ArcRuleData *arc = ptr->origRules->findArchive(dirHash);

  if(!arc)
    {
      log("Hash " + dirHash.toString() + " does NOT match any archive rule, assuming dirHash");

      /* Just load the dir and add it to 'out', along with any hints
         associated with it. We assume this is enough to find all the
         files we need. Throws on error.
       */
      addDirFile(out, dirHash, path);
      return;
    }

  /* We found an archive. Try loading the dirfile and injecting it
     into the ArcRuleSet. This will make the contained files directly
     available through the rule system, where the fetchFiles() system
     can find them. This is the preferred solution.
   */
  log("FOUND archive rule: dir=" + arc->dirHash.toString() +
      " arc=" + arc->arcHash.toString());

  try
    {
      DirPtr arcDir = addDirFile(out, arc->dirHash, path);
      ptr->arcRules->addArchive(arc->arcHash, arc->dirHash, arcDir,
                                arc->ruleString);
    }
  catch(std::exception &e)
    {
      /* If the dirfile loading fails for whatever reason (perhaps the
         dirfile is not available anywhere), then fall back to doing a
         "blind" unpack.
       */
      log("FAILED loading dirHash=" + arc->dirHash.toString() +
          "\n   Error message: " + std::string(e.what()) +
          "\n   Reverting to blind unpack");
      blinds.insert(HDValue(arc->arcHash, path));
    }
}

/* Load user-provided hints added through addHint(). These are loaded
   before anything else is done, so that we have a maximum chance of
   finding all the files necessary.
 */
void DirInstaller::loadUserHints()
{
  HashSet::const_iterator it;
  for(it = ptr->hints.begin(); it != ptr->hints.end(); it++)
    loadHint(*it);
}

void DirInstaller::sortInput()
{
  HashDir::const_iterator it;
  for(it = preHash.begin(); it != preHash.end(); it++)
    {
      handleHash(pre, it->first, preBlinds, it->second);
      if(checkStatus()) return;
    }
  for(it = postHash.begin(); it != postHash.end(); it++)
    {
      handleHash(post, it->first, postBlinds, it->second);
      if(checkStatus()) return;
    }
  preHash.clear();
  postHash.clear();
}

/* Fetch and unpack the archive 'arcHash' into the directory
   'path'. If path="", then just index the archive.
 */
void DirInstaller::unpackBlind(const Hash &arcHash, const std::string &path)
{
  TreePtr job = owner.unpackBlindTarget(path);
  job->addInput(arcHash);
  job->finder = finder;
  if(path != "")
    log("  Blind unpacking " + arcHash.toString() + " => " + path);
  else
    log("  Blind indexing " + arcHash.toString());
  execJob(job);
}

void DirInstaller::sortBlinds()
{
  assert(preHash.size() == 0 && postHash.size() == 0);

  // Do we have any pre- lists?
  if(pre.size() == 0 && preBlinds.size() == 0)
    {
      /* No pre-install dir listings. This means this is a pure
         install (or reinstall), not an upgrade.

         That means we do not need the directory (contents listing) of
         the archives we are installing, since we are just dumping the
         entire archive into the destination directory anyway.

         Just bind-install the archive to the destinatin directory.
       */
      if(postBlinds.size() == 0)
        return;

      log("Processing direct blind installs:");
      HashDir::const_iterator it;
      for(it = postBlinds.begin(); it != postBlinds.end(); it++)
        {
          unpackBlind(it->first, prefix+it->second);
          if(checkStatus()) return;
        }

      postBlinds.clear();
      return;
    }

  /* We have both pre and post listings, meaning that this is an
     upgrade not just an install. We cannot just dump archive contents
     into the destination directory, but have to be more careful.

     Our way of handling this is to INDEX the blind archives, meaning
     fetching them (downloading etc) then running through them and
     listing/hashing all the contents to a dirfile. (The dirfile
     generated and cached automatically by the sub-system.)

     This essentially turns a BLIND archive into a NON-BLIND
     archive. Then we can handle it on a file-by-file basis through
     sortInput() like other non-blind archives.
   */
  if(preBlinds.size() || postBlinds.size())
    log("Un-blinding archives for upgrade");

  HashDir::const_iterator it = preBlinds.begin();
  bool first=true;
  for(;; it++)
    {
      // Do some fancy loop trickery for fun and profit
      if(first && it == preBlinds.end())
        {
          first = false;
          it = postBlinds.begin();
        }
      if(!first && it == postBlinds.end())
        break;

      unpackBlind(it->first, "");
      if(checkStatus()) return;
    }

  // Move the archives back to the pre-processed state
  preHash = preBlinds;
  postHash = postBlinds;
  preBlinds.clear();
  postBlinds.clear();
}

void DirInstaller::sortAddDel(HashDir &add, HashDir &del, DirMap &upgrade)
{
  add.clear();
  del.clear();
  upgrade.clear();

  // 'post' lists files we are creating
  for(DirMap::const_iterator it = post.begin(); it != post.end(); it++)
    {
      const std::string &file = it->first;
      const std::string &full = prefix + file;
      const Hash &hash = it->second;
      assert(file != "");
      assert(hash.isSet());

      // Is there a matching file in the 'pre' directory?
      if(pre.find(file) != pre.end())
        {
          const Hash &pHash = pre[file];
          assert(pHash.isSet());

          // Do the hashes match?
          if(hash == pHash)
            /* Then this file is not to be upgraded. Ignore it
               completely (since it may contain user modifications
               that we are not going to overwrite.)
            */
            continue;

          // The hashes do not match, this is an upgrade. Store the
          // old hash.
          upgrade[full] = pHash;
        }

      // Add the file
      add.insert(HDValue(hash, full));
    }

  // 'pre' list are files already expected to be in the install
  // directory
  for(DirMap::const_iterator it = pre.begin(); it != pre.end(); it++)
    {
      const std::string &file = it->first;
      const Hash &hash = it->second;
      assert(hash.isSet());
      assert(file != "");

      // If post contains the same file, assume we have already
      // handled it above
      if(post.find(file) != post.end())
        continue;

      // A file only listed in the 'pre' dir means the file should be
      // deleted.
      const std::string &full = prefix + file;
      del.insert(HDValue(hash, full));
    }

  pre.clear();
  post.clear();
}

int DirInstaller::ask(const std::string &question, const std::string &opt0,
                      const std::string &opt1, const std::string &opt2,
                      const std::string &opt3, const std::string &opt4)
{
  StringAsk *a = new StringAsk(question);
  AskPtr p(a);
  std::vector<std::string> &opt = a->options;

  assert(opt0 != "");
  opt.push_back(opt0);
  if(opt1 != "")
    {
      opt.push_back(opt1);
      if(opt2 != "")
        {
          opt.push_back(opt2);
          if(opt3 != "")
            {
              opt.push_back(opt3);
              if(opt4 != "")
                opt.push_back(opt4);
            }
        }
    }

  log("Asking user question: " + question);
  for(int i=0; i<opt.size(); i++)
    log("OPTION: " + opt[i]);

  std::string oldMsg = setStatus("Waiting for answer to user question");
  bool abort = ptr->owner->askWait(p, getInfo());

  if(a->abort || checkStatus()) abort = true;
  if(abort) fail("User abort!");

  int &sel = a->selection;

  assert(sel >= 0 && sel < opt.size());
  log("RESPONSE: " + opt[sel]);

  setBusy(oldMsg);
  return sel;
}

void DirInstaller::resolveConflicts(HashDir &add, HashDir &del, const DirMap &upgrade,
                                    bool _doAsk)
{
  HashDir backups;

  // Create a list of all the file locations we want to check
  DirMap lookup;
  HashDir::iterator it, it2;
  for(it = add.begin(); it != add.end(); it++)
    lookup[it->second] = Hash();
  for(it = del.begin(); it != del.end(); it++)
    lookup[it->second] = Hash();

  // Check all the files in one run
  index.checkMany(lookup);

  // Go through the list of created elements
  for(it = add.begin(); it != add.end();)
    {
      // Keep two iterators so we can remove elements from the list
      // while iterating
      HashDir::iterator it2 = it++;

      const Hash &hash = it2->first;
      const std::string &file = it2->second;
      assert(hash.isSet());
      assert(file != "");

      const Hash &fHash = lookup[file];

      // Always overwrite missing files
      if(fHash.isNull()) continue;

      // Also overwrite if the file matches what we expected it to be
      bool upg = false;
      {
        DirMap::const_iterator res = upgrade.find(file);
        upg = (res != upgrade.end());
        if(upg && res->second == fHash)
          continue;
      }

      /* If a unexpected file was found, and it's not the file we are
         writing, then ask the user what to do.
      */
      if(fHash != hash)
        {
          int i = 1;
          if(_doAsk)
            {
              std::string text;
              if(upg) text = "File '" + file + "' contains modifications. Overwrite anyway?";
              else text = "File '" + file + "' already exists. Overwrite it?";
              i = ask(text, "Overwrite with backup (recommended)", "Overwrite without backup", "Keep file");
            }

          if(i == 0 || i == 1)
            {
              if(i == 0)
                backups.insert(HDValue(fHash, file + ".___backup___"));
              continue;
            }
          assert(i == 2);
        }

      // Remove the file addition entry
      add.erase(it2);
    }

  // Add backups back into the addition list
  for(it = backups.begin(); it != backups.end(); it++)
    add.insert(*it);

  // Do the same for the delete list
  for(it = del.begin(); it != del.end();)
    {
      it2 = it++;

      const Hash &hash = it2->first;
      const std::string &file = it2->second;
      assert(hash.isSet());
      assert(file != "");

      const Hash &fHash = lookup[file];

      // Delete file if it matches the expected hash
      if(fHash == hash)
        continue;

      if(fHash.isSet())
        {
          // There is a file, and it doesn't match what we want
          int i = 0;
          if(_doAsk) i = ask("Deleted file '" + file + "' has changes. Delete anyway?", "Delete file", "Keep file");
          if(i == 0)
            continue;
          assert(i == 1);
        }

      // Don't delete this file
      del.erase(it2);
    }
}

void DirInstaller::findMoves(HashDir &add, HashDir &del, StrMap &moves)
{
  HashDir::iterator it, it2, it3;
  for(it = add.begin(); it != add.end();)
    {
      it2 = it++;

      it3 = del.find(it2->first);
      if(it3 == del.end()) continue;

      // We have a match! A file that is both written and removed.
      assert(it2->first == it3->first);

      // Writing and deleting the same file should never happen
      assert(it2->second != it3->second);

      // Remove the entries and add the files to the move list
      moves[it3->second] = it2->second;
      add.erase(it2);
      del.erase(it3);
    }
}

void DirInstaller::doMovesDeletes(const StrMap &moves, const HashDir &del)
{
  DirMap created;
  StrSet removed;
  for(StrMap::const_iterator it = moves.begin(); it != moves.end(); it++)
    {
      ptr->owner->moveFile(it->first, it->second);
      created[it->second] = Hash();
      removed.insert(it->first);
    }
  for(HashDir::const_iterator it = del.begin(); it != del.end(); it++)
    {
      ptr->owner->deleteFile(it->second);
      removed.insert(it->second);
    }
  index.addMany(created, removed);
}

void DirInstaller::doJob()
{
  setStatus("Starting install into " + prefix);

  /* Load the hints added through addHint(), if any.
   */
  loadUserHints();

  /* Sort input from preHash/postHash into pre/post and
     preBlinds/postBlinds. This may involve fetching (downloading,
     unpacking etc) dir files.

     Clears preHash/postHash.
  */
  sortInput();
  if(checkStatus()) return;

  /* Processes the *Blinds lists. This will involve fetching the
     achives in question to tmp locations, and either installing them
     directly (in the non-patch case) or indexing them (producing the
     dirfile) so sortInput() will be able to use them as normal
     archives.

     Puts archives that need to be reprocessed back into
     preHash/postHash, so we can re-run sortInput(). Clears *Blinds
     lists.
   */
  sortBlinds();
  if(checkStatus()) return;

  /* Reprocess archives from sortBlinds(), if any. This time around
     they MUST be fully processed, or it is an error. IOW, we are not
     allowed to put archives back into the *Blinds lists.
   */
  sortInput();
  if(checkStatus()) return;

  if(preBlinds.size() || postBlinds.size())
    fail("Failed to process all archives.");
  assert(preHash.size() == 0 && postHash.size() == 0);

  /* Now we only have the 'pre' and 'post' lists of individual files
     left. Sort them into files to be added or deleted.

     Also spits out a complete list of existing files we are expecting
     to replace/patch into 'tmp'. This isn't used in the actual
     upgrade process, only to detect and resolve conflicts.
   */
  HashDir add, del;
  {
    bool isUpgrade = pre.size();
    DirMap upgrade;
    sortAddDel(add, del, upgrade);
    assert(isUpgrade || del.size() == 0);

    /* Go through the list and resolve conflicts between what we
       expect and what is ACTUALLY present in the file system
       (according to ICacheIndex.)

       Will ask the user for advice if necessary, and update add/del
       lists with results.
    */
    resolveConflicts(add, del, upgrade, doAsk);
  }

  /* Optimize add+delte pairs into moves, which are normally much
     faster.
   */
  StrMap moves;
  findMoves(add, del, moves);

  /* Perform main file install.
   */
  if(add.size())
    {
      /* Create a new version of our 'add' list, that is identical to
         the original except that all filenames have an extra string
         appended to them.

         This makes the install safer by not overwriting existing
         information, and brings a much more "transactional" nature to
         the whole procedure. Even more importantly, we avoid bugs
         where we try to read and write a file at the same time.

         It's not a perfect solution, but it helps. Without it, even
         simple actions such as swapping two files is a risky
         operation, because the system might end up trying to copy
         each file onto the other.
       */
      HashDir tmpAdd;
      for(HashDir::const_iterator it = add.begin(); it != add.end(); it++)
        {
          const std::string &newName = it->second+".___tmp";
          tmpAdd.insert(HDValue(it->first, newName));
          // Also add an entry to 'moves', so the file is moved into
          // place after a successful fetch
          moves[newName] = it->second;
        }

      try
        {
          HashMap tmp;
          fetchFiles(tmpAdd, tmp);
        }
      catch(std::exception &e)
        {
          // Clean up by killing all the files we created
          StrMap tmpMov;
          doMovesDeletes(tmpMov, tmpAdd);

          // Rethrow so our caller gets the error message
          throw std::runtime_error(("Failed installing into " + prefix + ":\n") + e.what());
        }
    }

  /* Perform file moves and deletes last.
   */
  doMovesDeletes(moves, del);

  setDone();
}

DirInstaller::DirInstaller(DirOwner &owner, RuleSet &rules,
                           Cache::ICacheIndex &_cache, const std::string &pref,
                           bool useAsks)
  : TreeBase(owner), doAsk(useAsks), index(_cache)
{
  ptr.reset(new _Internal);
  ptr->origRules = &rules;
  ptr->arcRules = new ArcRuleSet(ptr->origRules);
  ptr->rulePtr.reset(ptr->arcRules);
  ptr->owner = &owner;

  prefix = addSlash(pref);
  assert(prefix != "");

  finder.reset(new HashFinder(ptr->rulePtr, index));
}

void DirInstaller::addFile(const std::string &file, const Hash &hash)
{
  assert(!getInfo()->hasStarted());
  assert(hash.isSet());
  assert(file != "");
  post[file] = hash;
}

void DirInstaller::remFile(const std::string &file, const Hash &hash)
{
  assert(!getInfo()->hasStarted());
  assert(hash.isSet());
  assert(file != "");
  pre[file] = hash;
}

void DirInstaller::addDir(const DirMap &dir, const std::string &path)
{
  assert(!getInfo()->hasStarted());
  Dir::add(post, dir, addSlash(path));
}

void DirInstaller::remDir(const DirMap &dir, const std::string &path)
{
  assert(!getInfo()->hasStarted());
  Dir::add(pre, dir, addSlash(path));
}

void DirInstaller::addDir(const Hash &hash, const std::string &path)
{
  assert(!getInfo()->hasStarted());
  assert(hash.isSet());
  postHash.insert(HDValue(hash,addSlash(path)));
}

void DirInstaller::remDir(const Hash &hash, const std::string &path)
{
  assert(!getInfo()->hasStarted());
  assert(hash.isSet());
  preHash.insert(HDValue(hash,addSlash(path)));
}

void DirInstaller::addHint(const Hash &hash)
{
  assert(!getInfo()->hasStarted());
  assert(hash.isSet());
  ptr->hints.insert(hash);
}
