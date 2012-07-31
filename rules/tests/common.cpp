#include "rulefinder.hpp"
#include "arcrule.hpp"
#include "urlrule.hpp"

#include <iostream>
using namespace std;
using namespace Spread;

std::string h(const Hash &hash)
{
  return hash.toString().substr(0,8);
}

void test(const Hash &hash, RuleFinder &rules)
{
  cout << h(hash) << " - ";
  const Rule *r = rules.findRule(hash);

  if(!r)
    {
      cout << "NOT FOUND\n";
      return;
    }

  cout << "Rule found:\n  Rule: \"" << r->ruleString << "\"\n";
  for(int i=0; i<r->deps.size(); i++)
    cout << "  <= " << h(r->deps[i]) << endl;
  for(int i=0; i<r->outputs.size(); i++)
    cout << "  => " << h(r->outputs[i]) << endl;

  if(r->type == RST_Archive)
    {
      const ArcRule *arc = ArcRule::get(r);
      cout << "  Produces " << arc->dir->dir.size() << " file(s)\n";
    }
  else if(r->type == RST_URL)
    {
      const URLRule *url = URLRule::get(r);
      assert(!url->isBroken);

      cout << "  URL: " << url->url
           << "\n  Priority: " << url->priority
           << "\n  Weight: " << url->weight
           << "\n\n";
    }
  else assert(0);
}