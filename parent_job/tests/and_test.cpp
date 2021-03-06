#include "andjob.hpp"
#include "common.cpp"

struct TestJob : ParentJob
{
  int i;

  TestJob(int _i) : i(_i) {}

  void doJob()
  {
    setProgress(i,4);
    if(i==3) throw runtime_error("Fail on 3");

    setDone();
  }
};

struct AbortJob : Job
{
  JobPtr jj;

  AbortJob(JobPtr j) : jj(j) {}

  void doJob()
  {
    jj->getInfo()->abort();
    Thread::sleep(5);
  }
};

int main()
{
  {
    AndJob *aj = new AndJob;
    JobPtr ptr(aj);

    aj->add(new TestJob(1));
    aj->add(new TestJob(2));
    aj->add(new TestJob(4));
    ptr->run();
    print(ptr);
  }
  {
    AndJob *aj = new AndJob;
    JobPtr ptr(aj);

    aj->add(new TestJob(1));
    aj->add(new TestJob(2));
    aj->add(new TestJob(3));
    aj->add(new TestJob(4));
    ptr->run();
    print(ptr);
  }
  // Regression: AndJob fails if aborted
  {
    AndJob *aj = new AndJob;
    JobPtr ptr(aj);
    aj->add(new AbortJob(ptr));
    ptr->run();
    print(ptr);
  }

  return 0;
}
