#include "shared_container.h"
#include <vector>
#include <assert.h>
#include <unistd.h>
#include <err.h>
#include <stdlib.h>
#include <time.h>
#include <sys/wait.h>
#include <iostream>

using namespace std;

int main()
{
  srand(time(NULL));
  global_shared_allocator::shm_open(NULL, O_RDWR | O_CREAT | O_TRUNC);
  atexit(global_shared_allocator::shm_close);

  // Reference.
  vector<vector<int>> v;

  // Shared containers.
  // Rebinding correctness is tested by list.
  shared_vector<shared_vector<shared_vector<int>>> ss(1);
  shared_vector<shared_vector<int>> &s = ss[0];
  shared_vector<shared_list<int>> &l = *new(shared) shared_vector<shared_list<int>>;

  // Perform the same operations randomly to v and s.
  for(int i = 0; i < 100; ++i) {
    if(rand() % 2) {
      v.emplace_back();
      s.emplace_back();
      for(int j = rand() % 100; j < 100; ++j) {
        v.back().push_back(rand());
        s.back().push_back(v.back().back());
      }
    } else if(!v.empty()) {
      int r = rand() % v.size();
      v.erase(v.begin() + r);
      s.erase(s.begin() + r);
    }
  }

  // Make sure shared vectors work well for single process.
  assert(s.size() == v.size());
  for(int i = 0; i < (int)v.size(); ++i) {
    assert(s[i].size() == v[i].size());
    for(int j = 0; j < (int)v[i].size(); ++j) {
      assert(s[i][j] == v[i][j]);
    }
    cout << s[i].size();
    if(s[i].size()) cout << "\t" << s[i].front() << "\t...\t" << s[i].back();
    cout << endl;
  }

  pid_t pid = fork();
  if(pid < 0) {
    err(EXIT_FAILURE, "fork");
  } else if(pid == 0) {  // child
    // Make sure another process can open shm via the name.
    global_shared_allocator::shm_close();
    global_shared_allocator::shm_open(NULL, O_RDWR | O_CREAT);
    global_shared_allocator::shm_unlink();

    // Show s content and move it to l.
    cout << string(80, '-') << endl;
    for(int i = 0; i < (int)v.size(); ++i) {
      cout << s[i].size();
      if(s[i].size()) cout << "\t" << s[i].front() << "\t...\t" << s[i].back();
      cout << endl;
      l.emplace_back(s[i].begin(), s[i].end());
    }
    s.clear();
    exit(0);
  }
  wait(NULL);

  // Make sure l == v, which verifies the cross-process applicability.
  assert(s.empty());
  assert(l.size() == v.size());
  for(int i = 0; i < (int)v.size(); ++i) {
    assert(l[i].size() == v[i].size());
    auto it = l[i].begin();
    for(int j = 0; j < (int)v[i].size(); ++j) {
      assert(*it++ == v[i][j]);
    }
  }
  return 0;
}
