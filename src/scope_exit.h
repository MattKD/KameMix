#ifndef SCOPE_EXIT_H
#define SCOPE_EXIT_H

namespace KameMix {

template <typename F>
class ScopeExit {
public:
  explicit ScopeExit(const F &func) : cancelDtor{false}, func{func} {}
  ~ScopeExit() { if (!cancelDtor) func(); }
  void cancel() { cancelDtor = true; }
private:
  bool cancelDtor;
  F func;
};

template <typename F>
ScopeExit<F> makeScopeExit(const F &func)
{
  return ScopeExit<F>(func);
}

}
#endif
