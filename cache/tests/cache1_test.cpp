#include <iostream>

#include "index.hpp"

using namespace Spread;
using namespace std;

Cache::CacheIndex index;

void add(const std::string &file, const Hash &h = Hash(), bool allowMissing=false)
{
  cout << "Adding " << file;
  if(!h.isNull()) cout << " (expecting " << h << ")";
  cout << endl;
  try
    {
      Hash out =index.addFile(file,h, allowMissing);
      cout << "  Got: " << out << endl;
    }
  catch(exception &e)
    {
      cout << "  Error: " << e.what() << endl;
    }
}

void find(const Hash &h)
{
  cout << "Searching for " << h << endl;
  cout << "  Found: " << index.findHash(h) << endl;
}

void status(const std::string &file, const Hash &h)
{
  using namespace Cache;

  cout << "Checking status for " << file << "   Hash: " << h << "\n  ";
  switch(index.getStatus(file,h))
    {
    case CI_None: cout << "No file found\n"; break;
    case CI_Match: cout << "File already matches\n"; break;
    case CI_Diff: cout << "File exists but is different:\n    "
                       << index.addFile(file) << endl; break;
    case CI_ElseWhere: cout << "Hash exists elsewhere: " << index.findHash(h) << endl;
      break;
    default:
      assert(0);
    }
}

int main()
{
  Hash hello("hello",5);
  Hash testsh("ctEjJBRstghw4_UpmjBdhwJZFl8faISyIeEk2sOH5LLfAQ");
  add("test.sh");
  add("hello.dat", hello);
  add("hello.dat", Hash("WRONG!",6));
  add("nofile.dat");
  add("nofile.dat", Hash(), true);
  add("hello2.dat");
  add("hello2.dat");

  find(testsh);
  index.removeFile("test.sh");
  find(testsh);

  find(hello);
  index.removeFile("hello.dat");
  find(hello);
  index.removeFile("hello.dat");
  find(hello);
  index.removeFile("hello2.dat");
  find(hello);

  status("nofile", hello);
  status("test.sh", hello);
  status("hello2.dat", hello);
  status("test.sh", hello);
  status("hello.dat", testsh);
  status("hello.dat", hello);

  // Dump to screen
  Cache::CIVector vec;
  index.getEntries(vec);
  for(int i=0; i<vec.size(); i++)
    {
      Cache::CIEntry &e = vec[i];
      cout << e.hash << " " << e.file << " " << e.writeTime << endl;
    }

  return 0;
}
