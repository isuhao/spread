#include "common.cpp"

struct MyTest : DirInstaller
{
  MyTest(const std::string &pref = "")
    : DirInstaller(own, rules, cache, pref, true) {}

  void doJob()
  {
    print(pre, "PRE");
    print(post, "POST");
    print(preHash, "PRE-HASH");
    print(postHash, "POST-HASH");
    cout << endl;
    setDone();
  }
};

int main()
{
  {
    MyTest test("blah");
    test.run();
  }
  {
    MyTest test("blah");
    test.addFile("hello", Hash("HELLO"));
    test.addFile("world", Hash("HELLO"));
    test.remFile("world", Hash("WORLD"));
    test.run();
  }
  {
    MyTest test("blah");
    test.updateFile("arne", Hash("OLD"), Hash("NEW"));
    test.ignoreFile("user.cfg");
    test.run();
  }
  {
    MyTest test("blah");
    test.addFile("first", Hash("FIRST"));
    Hash::DirMap dir;
    dir["first"] = Hash("FIRST2");
    dir["second"] = Hash("SECOND");
    test.addDir(dir);
    dir["second"] = Hash("OLD");
    dir["third"] = Hash("DELETED");
    test.remDir(dir);
    test.addDir(dir, "subdir/");
    test.remDir(dir, "prefix_");
    test.run();
  }
  {
    MyTest test("blah");
    test.addDir(Hash("DIR1"));
    test.addDir(Hash("DIR2"), "dir2");
    test.remDir(Hash("DIR3"), "dir3");
    test.run();
  }
  return 0;
}
